; Private near routines may destroy AX/BX/CX/DX/SI/DI/BP/ES.
; Board layout is top to bottom. High nibble: 0 single, 1 left, 2 right,
; 3 top, 4 bottom, 5 virus. Low two bits: blue=1, red=2, yellow=3.

dm_random:
    mov ax, [dm_seed]
    mov dl, al
    xor dl, ah
    shr dl, 1
    shr dl, 1
    rcr al, 1
    rcr ah, 1
    mov [dm_seed], ax
    ret

dm_newgame:
    call dm_audio_game
    call dm_animreset
    push ds
    pop es
    mov di, dm_board
    xor ax, ax
    mov cx, 128
    rep stosb
    mov byte [dm_started], 1
    mov byte [dm_paused], 0
    mov byte [dm_lr], 0
    mov byte [dm_pillcount], 0
    mov byte [dm_accel], 0
    mov byte [dm_seqindex], 0
    ; 82A0: generate backwards, adding the RNG nibble modulo nine.
    mov bx, 127
    xor cx, cx
.seq:
    call dm_random
    and al, 15
    add cl, al
.mod:
    cmp cl, 9
    jb .save
    sub cl, 9
    jmp .mod
.save:
    mov [dm_sequence+bx], cl
    dec bx
    jns .seq
    mov al, [dm_level]
    inc al
    shl al, 1
    shl al, 1
    mov [dm_viruses], al
    xor ch, ch
    mov cl, al
.place:
    push cx
.retry:
    call dm_random
    xor bx, bx
    mov bl, [dm_level]
    and al, 15
    cmp al, [dm_heights+bx]
    ja .retry
    mov bl, 15
    sub bl, al
    shl bl, 1
    shl bl, 1
    shl bl, 1
    and ah, 7
    or bl, ah
    cmp byte [dm_board+bx], 0
    jne .retry
    ; Color is chosen independently of the row bits. Using a second
    ; consecutive RNG low byte here correlates color with y and makes high
    ; levels impossible to fill. 9CFF chooses from the remaining count.
    mov al, cl
    and al, 3
    cmp al, 3
    jne .color
    call dm_random
    mov al, ah
    and al, 15
    push bx
    xor bx, bx
    mov bl, al
    mov al, [dm_viruscolors+bx]
    pop bx
.color:
    mov ah, 3
    sub ah, al
    mov al, ah
    mov di, 3
.colorcheck:
    ; Avoid the source randomizer's same-color neighbors two cells away.
    cmp bx, 16
    jb .below
    mov ah, [dm_board+bx-16]
    and ah, 3
    cmp al, ah
    je .badcolor
.below:
    cmp bx, 112
    jae .horiz
    mov ah, [dm_board+bx+16]
    and ah, 3
    cmp al, ah
    je .badcolor
.horiz:
    mov dl, bl
    and dl, 7
    cmp dl, 2
    jb .right
    mov ah, [dm_board+bx-2]
    and ah, 3
    cmp al, ah
    je .badcolor
.right:
    cmp dl, 5
    ja .put
    mov ah, [dm_board+bx+2]
    and ah, 3
    cmp al, ah
    je .badcolor
    jmp .put
.badcolor:
    dec di
    jz .retry
    inc al
    cmp al, 4
    jb .colorcheck
    mov al, 1
    jmp .colorcheck
.put:
    mov si, ax
    and si, 3
    inc byte [dm_colorcount+si]
    or al, 50h
    mov [dm_board+bx], al
    pop cx
    dec cx
    jnz .place
    mov bx, 3
.mascot:
    cmp byte [dm_colorcount+bx], 0
    jne .present
    mov byte [dm_actorpose+bx], 3
.present:
    dec bx
    jnz .mascot
    call dm_nextpair
    call dm_spawn
    mov byte [dm_dirty], 2
    mov byte [dm_huddirty], 1
    ret

dm_nextpair:
    xor bx, bx
    mov bl, [dm_seqindex]
    mov bl, [dm_sequence+bx]
    mov al, 3
    sub al, [dm_pair_a+bx]
    mov ah, 3
    sub ah, [dm_pair_b+bx]
    mov [dm_next], ax
    inc byte [dm_seqindex]
    and byte [dm_seqindex], 127
    ret

dm_spawn:
    mov ax, [dm_next]
    mov [dm_colors], ax
    call dm_nextpair
    mov byte [dm_x], 3
    mov byte [dm_y], 0
    mov byte [dm_rotation], 0
    mov byte [dm_falltimer], 0
    mov byte [dm_state], 0
    mov byte [dm_dirty], 2
    mov byte [dm_huddirty], 1
    call dm_valid
    jnc .ok
    mov byte [dm_state], 3
    mov al, 6
    call dm_audio_song
.ok: ret

; Return BX=anchor (left or bottom), SI=other (right or top).
; CF=1 for any cell outside the bottle or occupied; preserves colors.
dm_valid:
    xor bx, bx
    mov bl, [dm_y]
    cmp bl, 16
    jae .bad
    shl bx, 1
    shl bx, 1
    shl bx, 1
    xor ax, ax
    mov al, [dm_x]
    cmp al, 8
    jae .bad
    add bx, ax
    mov si, bx
    test byte [dm_rotation], 1
    jnz .vertical
    cmp al, 7
    je .bad
    inc si
    jmp .test
.vertical:
    cmp bx, 8
    jb .bad
    sub si, 8
.test:
    cmp byte [dm_board+bx], 0
    jne .bad
    cmp byte [dm_board+si], 0
    jne .bad
    clc
    ret
.bad:
    stc
    ret

dm_move:
    push ax
    add [dm_x], al
    call dm_valid
    pop ax
    jnc .ok
    sub [dm_x], al
    ret
.ok:
    or byte [dm_dirty], 1
    mov al, DM_FX_MOVE
    jmp dm_effect

dm_rotate:
    mov ah, [dm_rotation]
    push ax
    add al, ah
    and al, 3
    mov [dm_rotation], al
    call dm_valid
    jnc .ok
    ; A vertical-to-horizontal turn may kick left by one cell.
    test byte [dm_rotation], 1
    jnz .fail
    dec byte [dm_x]
    call dm_valid
    jnc .ok
    inc byte [dm_x]
.fail:
    pop ax
    mov [dm_rotation], ah
    ret
.ok:
    pop ax
    or byte [dm_dirty], 1
    mov al, DM_FX_ROTATE
    jmp dm_effect

; AX=encoded halves for the current rotation; BX/SI from valid.
dm_halves:
    mov ax, [dm_colors]
    test byte [dm_rotation], 2
    jz .order
    xchg al, ah
.order:
    test byte [dm_rotation], 1
    jnz .v
    or ax, 2010h
    ret
.v:
    or ax, 3040h
    ret

dm_tick:
    cmp byte [dm_paused], 0
    jne .out
    call dm_animtick
    cmp byte [dm_state], 0
    je .fall
    cmp byte [dm_state], 1
    je .flash
    cmp byte [dm_state], 2
    je .gravity
.out: ret
.fall:
    inc byte [dm_falltimer]
    xor bx, bx
    mov bl, [dm_speed]
    mov bl, [dm_speedbase+bx]
    add bl, [dm_accel]
    mov al, [dm_speeds+bx]
    test word [dm_keys], 4
    jz .time
    mov al, 2
.time:
    cmp [dm_falltimer], al
    jb .out
    mov byte [dm_falltimer], 0
    inc byte [dm_y]
    call dm_valid
    jnc .piecechanged
    dec byte [dm_y]
    call dm_valid
    call dm_halves
    mov [dm_board+bx], al
    mov [dm_board+si], ah
    mov al, DM_FX_LAND
    call dm_effect
    inc byte [dm_pillcount]
    cmp byte [dm_pillcount], 10
    jb .match
    mov byte [dm_pillcount], 0
    cmp byte [dm_accel], 49
    jae .match
    inc byte [dm_accel]
    mov al, DM_FX_SPEED
    call dm_effect
.match:
    call dm_findmatches
    cmp byte [dm_matched], 0
    jne .beginflash
    call dm_spawn
.changed:
    mov byte [dm_dirty], 2
    ret
.piecechanged:
    or byte [dm_dirty], 1
    ret
.beginflash:
    mov byte [dm_state], 1
    mov byte [dm_delay], 12
    mov byte [dm_dirty], 2
    mov al, DM_FX_CLEAR
    jmp dm_effect
.flash:
    dec byte [dm_delay]
    jnz .out
    call dm_remove
    mov byte [dm_state], 2
    mov byte [dm_delay], 3
    ret
.gravity:
    dec byte [dm_delay]
    jnz .out
    mov byte [dm_delay], 3
    call dm_gravity
    cmp byte [dm_moved], 0
    je .settled
    mov al, DM_FX_FALL
    call dm_effect
    jmp .changed
.settled:
    call dm_findmatches
    cmp byte [dm_matched], 0
    jne .beginflash
    cmp byte [dm_viruses], 0
    jne .spawn
    mov byte [dm_state], 4
    mov byte [dm_huddirty], 1
    mov al, [dm_music]
    and al, 1
    add al, 2
    jmp dm_audio_song
.spawn:
    call dm_spawn
    ret

dm_speedbase: db 15,25,31

; Scan 16 rows and 8 columns; intersecting runs mark a cell only once.
dm_findmatches:
    push ds
    pop es
    mov di, dm_marks
    xor ax, ax
    mov cx, 128
    rep stosb
    mov byte [dm_matched], 0
    xor bx, bx
.row:
    mov dx, 1
    mov cx, 8
    call dm_line
    add bx, 8
    cmp bx, 128
    jb .row
    xor bx, bx
.col:
    mov dx, 8
    mov cx, 16
    call dm_line
    inc bx
    cmp bx, 8
    jb .col
    ret

; BX first, DX stride, CX length. Keep BX for outer loop.
dm_line:
    push bx
    mov si, bx
    xor ax, ax               ; AL previous color, AH run length
.next:
    mov bl, [dm_board+si]
    and bl, 3
    cmp bl, al
    jne .endrun
    or bl, bl
    jz .advance
    inc ah
    jmp .advance
.endrun:
    call dm_markrun
    mov al, bl
    mov ah, 1
.advance:
    add si, dx
    loop .next
    call dm_markrun
    pop bx
    ret

dm_markrun:
    cmp ah, 4
    jb .out
    or al, al
    jz .out
    push ax
    push di
    mov di, si
.mark:
    sub di, dx
    mov byte [dm_marks+di], 1
    dec ah
    jnz .mark
    mov byte [dm_matched], 1
    pop di
    pop ax
.out: ret

; Detach before deleting, so a crossed match cannot leave an orphan link.
dm_remove:
    mov word [dm_award], 100
    xor bx, bx
.cell:
    cmp byte [dm_marks+bx], 0
    je .next
    mov al, [dm_board+bx]
    mov ah, al
    and ah, 0f0h
    cmp ah, 50h
    je .virus
    cmp ah, 10h
    jne .right
    and byte [dm_board+bx+1], 3
    jmp .clear
.right:
    cmp ah, 20h
    jne .top
    and byte [dm_board+bx-1], 3
    jmp .clear
.top:
    cmp ah, 30h
    jne .bottom
    and byte [dm_board+bx+8], 3
    jmp .clear
.bottom:
    cmp ah, 40h
    jne .clear
    and byte [dm_board+bx-8], 3
    jmp .clear
.virus:
    dec byte [dm_viruses]
    mov si, ax
    and si, 3
    dec byte [dm_colorcount+si]
    mov byte [dm_hits+si], 2
    xor ax, ax
    mov al, [dm_speed]
    inc ax
    mul word [dm_award]
    add [dm_score], ax
    adc [dm_score+2], dx
    cmp word [dm_award], 3200
    jae .clear
    shl word [dm_award], 1
.clear:
    mov byte [dm_board+bx], 0
.next:
    inc bx
    cmp bx, 128
    jb .cell
    mov byte [dm_dirty], 2
    mov byte [dm_huddirty], 1
    ret

; Bottom up: each capsule falls at most one row per step, as one unit.
dm_gravity:
    mov byte [dm_moved], 0
    mov bx, 119
.cell:
    mov al, [dm_board+bx]
    or al, al
    jz .next
    mov ah, al
    and ah, 0f0h
    cmp ah, 50h
    je .next
    cmp ah, 20h               ; handled by left partner
    je .next
    cmp ah, 30h               ; handled by bottom partner
    je .next
    cmp byte [dm_board+bx+8], 0
    jne .next
    cmp ah, 10h
    je .pair
    cmp ah, 40h
    je .vertical
    mov [dm_board+bx+8], al
    mov byte [dm_board+bx], 0
    jmp .moved
.pair:
    cmp byte [dm_board+bx+9], 0
    jne .next
    mov ax, [dm_board+bx]
    mov [dm_board+bx+8], ax
    mov word [dm_board+bx], 0
    jmp .moved
.vertical:
    mov [dm_board+bx+8], al
    mov al, [dm_board+bx-8]
    mov [dm_board+bx], al
    mov byte [dm_board+bx-8], 0
.moved:
    mov byte [dm_moved], 1
.next:
    dec bx
    jns .cell
    ret
