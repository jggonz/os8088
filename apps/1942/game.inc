; Native simulation. All coordinates are unsigned logical pixels.
EX equ 0
EY equ 2
EVX equ 4
EHP equ 6
EKIND equ 8
EAGE equ 10
EON equ 12
ESIZE equ 14

n_rand:
    push bx
    push dx
    mov ax, [n_seed]
    mov bx, 25173
    mul bx
    add ax, 13849
    mov [n_seed], ax
    pop dx
    pop bx
    ret
n_new:
    SAVE
    mov byte [n_state], 1
    mov byte [n_paused], 0
    mov word [n_px], 116
    mov word [n_py], 204
    mov word [n_lives], 3
    mov word [n_rolls], 3
    mov word [n_roll], 0
    mov word [n_grace], 50
    mov word [n_fire], 0
    mov word [n_weapon], 1
    mov word [n_scorelo], 0
    mov word [n_scorehi], 0
    mov word [n_stage], 1
    mov word [n_frames], 0
    mov word [n_islandy], 24
    mov word [n_wavey], 60
    mov word [n_bank], n_playerart
    mov word [n_picky], 0ffffh
    mov word [n_spawned], 0
    mov word [n_spawnwait], 30
    mov byte [n_bossmade], 0
    mov byte [n_huddirty], 1
    mov byte [n_xheld], 0
    push ds
    pop es
    mov di, n_enemies
    xor ax, ax
    mov cx, (12*ESIZE)/2
    rep stosw
    mov di, n_shots
    mov ax, 0ffffh
    mov cx, 16*2+16*4
    rep stosw
    mov di, n_blasts
    xor ax, ax
    mov cx, 8*3
    rep stosw
    RESTORE
    ret

n_refresh:
    SAVE
    call n_clear
    mov byte [n_huddirty], 1
    cmp byte [n_state], 1
    jne .message
    call n_draw
    cmp byte [n_paused], 0
    je .out
    mov si, n_msgpause
    mov cx, 104
    mov dx, 104
    call n_text
    mov si, n_msgresume
    mov cx, 84
    mov dx, 120
    call n_text
    jmp .out
.message:
    mov si, n_msgtitle
    mov cx, 100
    mov dx, 56
    call n_text
    mov si, n_playerart
    mov cx, 120
    mov dx, 88
    call n_sprite
    mov si, n_msgstart
    mov cx, 64
    mov dx, 128
    cmp byte [n_state], 0
    je .headline
    mov si, n_msgover
    mov cx, 92
    cmp byte [n_state], 2
    je .headline
    mov si, n_msgwin
    mov cx, 64
.headline:
    call n_text
    mov si, n_msghint
    mov cx, 52
    mov dx, 152
    cmp byte [n_state], 0
    je .hint
    mov si, n_msgnew
    mov cx, 68
.hint:
    call n_text
    call n_scoretext
.out:
    RESTORE
    ret

n_update:
    SAVE
    inc word [n_frames]
    inc word [n_wavey]
    cmp word [n_wavey], 232
    jb .wave
    mov word [n_wavey], 24
.wave:
    test word [n_frames], 7
    jnz .island
    inc word [n_islandy]
    cmp word [n_islandy], 208
    jb .island
    mov word [n_islandy], 16
.island:
    cmp word [n_grace], 0
    je .grace
    dec word [n_grace]
.grace:
    cmp word [n_roll], 0
    je .roll
    dec word [n_roll]
.roll:
    mov al, 2dh                       ; X, edge triggered
    call OSAPI_KEY_DOWN
    jnc .xup
    cmp byte [n_xheld], 0
    jne .move
    mov byte [n_xheld], 1
    cmp word [n_rolls], 0
    je .move
    cmp word [n_roll], 0
    jne .move
    dec word [n_rolls]
    mov word [n_roll], 48
    mov byte [n_huddirty], 1
    mov ax, 900
    call n_tone
    jmp .move
.xup:
    mov byte [n_xheld], 0
.move:
    mov word [n_bank], n_playerart
    mov al, KSC_LEFT
    call OSAPI_KEY_DOWN
    jnc .right
    cmp word [n_px], 3
    jb .right
    sub word [n_px], 3
    mov word [n_bank], n_bankleftart
.right:
    mov al, KSC_RIGHT
    call OSAPI_KEY_DOWN
    jnc .up
    cmp word [n_px], 229
    ja .up
    add word [n_px], 3
    mov word [n_bank], n_bankrightart
.up:
    mov al, KSC_UP
    call OSAPI_KEY_DOWN
    jnc .down
    cmp word [n_py], 27
    jb .down
    sub word [n_py], 3
.down:
    mov al, KSC_DOWN
    call OSAPI_KEY_DOWN
    jnc .fire
    cmp word [n_py], 213
    ja .fire
    add word [n_py], 3
.fire:
    cmp word [n_fire], 0
    je .ready
    dec word [n_fire]
    jmp .spawn
.ready:
    mov al, KSC_SPACE
    call OSAPI_KEY_DOWN
    jc .shoot
    mov al, 2ch                      ; Z
    call OSAPI_KEY_DOWN
    jnc .spawn
.shoot:
    mov word [n_fire], 5
    mov cx, [n_px]
    add cx, 11
    cmp word [n_weapon], 1
    je .single
    sub cx, 5
    call n_shot
    add cx, 10
    call n_shot
    cmp word [n_weapon], 3
    jb .shotdone
    sub cx, 5
.single:
    call n_shot
.shotdone:
    mov ax, 1400
    call n_tone
.spawn:
    call n_spawn
    call n_move_enemies
    call n_move_shots
    call n_move_bullets
    call n_move_pickup
    mov si, n_blasts
.blast:
    cmp word [si+4], 0
    je .bnext
    dec word [si+4]
.bnext:
    add si, 6
    cmp si, n_blasts+8*6
    jb .blast
    call n_nextstage
    RESTORE
    ret

n_shot:                              ; CX=x
    SAVE
    mov si, n_shots
.find:
    cmp word [si+2], 0ffffh
    je .slot
    add si, 4
    cmp si, n_shots+16*4
    jb .find
    jmp .out
.slot:
    mov [si], cx
    mov ax, [n_py]
    sub ax, 4
    mov [si+2], ax
.out:
    RESTORE
    ret
n_spawn:
    cmp word [n_spawned], 24
    jae .out
    cmp word [n_spawnwait], 0
    je .findslot
    dec word [n_spawnwait]
    ret
.findslot:
    mov si, n_enemies
.find:
    cmp word [si+EON], 0
    je .slot
    add si, ESIZE
    cmp si, n_enemies+12*ESIZE
    jb .find
    ret
.slot:
    call n_rand
    mov bx, ax
    and ax, 127
    add ax, 48
    mov [si+EX], ax
    mov word [si+EY], 18
    mov word [si+EVX], 1
    test bx, 8000h
    jz .dir
    mov word [si+EVX], -1
.dir:
    mov word [si+EON], 1
    mov word [si+EHP], 1
    mov word [si+EKIND], 0
    mov word [si+EAGE], 0
    mov ax, [n_spawned]
    and ax, 3
    cmp ax, 3
    jne .normal
    mov word [si+EKIND], 1
    mov word [si+EHP], 2
.normal:
    inc word [n_spawned]
    mov ax, 30
    sub ax, [n_stage]
    cmp ax, 12
    jge .delay
    mov ax, 12
.delay:
    mov [n_spawnwait], ax
.out:
    ret
n_move_enemies:
    mov si, n_enemies
.loop:
    cmp word [si+EON], 0
    je .next
    inc word [si+EAGE]
    mov ax, [si+EVX]
    add [si+EX], ax
    mov ax, 224
    cmp word [si+EKIND], 2
    jne .bound
    mov ax, 208
.bound:
    cmp [si+EX], ax
    jae .bounce
    cmp word [si+EX], 8
    ja .vertical
.bounce:
    neg word [si+EVX]
.vertical:
    cmp word [si+EKIND], 2
    jne .small
    cmp word [si+EY], 44
    jae .attack
    inc word [si+EY]
    jmp .attack
.small:
    add word [si+EY], 2
    cmp word [si+EY], 212
    jbe .attack
    mov word [si+EON], 0
    jmp .next
.attack:
    mov ax, [si+EAGE]
    and ax, 31
    jnz .contact
    call n_enemyfire
.contact:
    mov ax, [si+EX]
    sub ax, [n_px]
    add ax, 12
    cmp ax, 24
    jae .next
    mov ax, [si+EY]
    sub ax, [n_py]
    add ax, 12
    cmp ax, 24
    jae .next
    call n_hitplayer
    cmp word [si+EKIND], 2
    je .next
    mov word [si+EON], 0
    mov cx, [si+EX]
    mov dx, [si+EY]
    call n_blast
.next:
    add si, ESIZE
    cmp si, n_enemies+12*ESIZE
    jb .loop
    ret
n_enemyfire:
    SAVE
    mov di, n_bullets
.find:
    cmp word [di+2], 0ffffh
    je .slot
    add di, 8
    cmp di, n_bullets+16*8
    jb .find
    jmp .out
.slot:
    mov ax, [si+EX]
    add ax, 12
    mov [di], ax
    mov bx, [n_px]
    add bx, 12
    sub bx, ax
    mov word [di+4], 0
    cmp bx, 12
    jle .left
    mov word [di+4], 1
    jmp .y
.left:
    cmp bx, -12
    jge .y
    mov word [di+4], -1
.y:
    mov ax, [si+EY]
    add ax, 16
    mov [di+2], ax
    mov word [di+6], 3
.out:
    RESTORE
    ret
n_move_shots:
    mov di, n_shots
.loop:
    cmp word [di+2], 0ffffh
    je .next
    cmp word [di+2], 24
    jbe .remove
    sub word [di+2], 6
    mov si, n_enemies
.enemy:
    cmp word [si+EON], 0
    je .enext
    mov bx, 24
    cmp word [si+EKIND], 2
    jne .size
    mov bx, 40
.size:
    mov ax, [di]
    sub ax, [si+EX]
    cmp ax, bx
    jae .enext
    mov ax, [di+2]
    add ax, 5
    cmp ax, [si+EY]
    jb .enext
    mov ax, [si+EY]
    add ax, bx
    cmp [di+2], ax
    jae .enext
    dec word [si+EHP]
    jnz .remove
    call n_kill
    jmp .remove
.enext:
    add si, ESIZE
    cmp si, n_enemies+12*ESIZE
    jb .enemy
    jmp .next
.remove:
    mov word [di+2], 0ffffh
.next:
    add di, 4
    cmp di, n_shots+16*4
    jb .loop
    ret
n_kill:
    SAVE
    mov word [si+EON], 0
    mov cx, [si+EX]
    mov dx, [si+EY]
    call n_blast
    mov ax, 100
    cmp word [si+EKIND], 0
    je .score
    mov ax, 500
    cmp word [si+EKIND], 1
    jne .boss
    mov [n_pickx], cx
    mov [n_picky], dx
    jmp .score
.boss:
    mov ax, 3000
.score:
    call n_addscore
    mov ax, 180
    call n_tone
    RESTORE
    ret
n_addscore:
    add [n_scorelo], ax
    adc word [n_scorehi], 0
    mov byte [n_huddirty], 1
    ret
n_blast:                            ; CX/DX position
    SAVE
    mov si, n_blasts
.find:
    cmp word [si+4], 0
    je .slot
    add si, 6
    cmp si, n_blasts+8*6
    jb .find
    mov si, n_blasts
.slot:
    mov [si], cx
    mov [si+2], dx
    mov word [si+4], 8
    RESTORE
    ret
n_hitplayer:
    SAVE
    cmp word [n_grace], 0
    jne .out
    cmp word [n_roll], 0
    jne .out
    mov cx, [n_px]
    mov dx, [n_py]
    call n_blast
    dec word [n_lives]
    mov word [n_grace], 75
    mov word [n_weapon], 1
    mov word [n_px], 116
    mov word [n_py], 204
    mov byte [n_huddirty], 1
    mov ax, 80
    call n_tone
    cmp word [n_lives], 0
    jne .out
    mov byte [n_state], 2
.out:
    RESTORE
    ret
n_move_bullets:
    mov si, n_bullets
.loop:
    cmp word [si+2], 0ffffh
    je .next
    mov ax, [si+4]
    add [si], ax
    mov ax, [si+6]
    add [si+2], ax
    cmp word [si], 252
    ja .remove
    cmp word [si+2], 236
    ja .remove
    mov ax, [si]
    sub ax, [n_px]
    cmp ax, 24
    jae .next
    mov ax, [si+2]
    sub ax, [n_py]
    cmp ax, 24
    jae .next
    call n_hitplayer
.remove:
    mov word [si+2], 0ffffh
.next:
    add si, 8
    cmp si, n_bullets+16*8
    jb .loop
    ret
n_move_pickup:
    cmp word [n_picky], 0ffffh
    je .out
    inc word [n_picky]
    cmp word [n_picky], 228
    ja .remove
    mov ax, [n_pickx]
    sub ax, [n_px]
    add ax, 12
    cmp ax, 24
    jae .out
    mov ax, [n_picky]
    sub ax, [n_py]
    add ax, 12
    cmp ax, 24
    jae .out
    cmp word [n_weapon], 3
    jae .score
    inc word [n_weapon]
.score:
    mov ax, 500
    call n_addscore
    mov ax, 1800
    call n_tone
.remove:
    mov word [n_picky], 0ffffh
.out:
    ret
n_nextstage:
    cmp byte [n_state], 1
    jne .out
    cmp word [n_spawned], 24
    jb .out
    mov si, n_enemies
.loop:
    cmp word [si+EON], 0
    jne .out
    add si, ESIZE
    cmp si, n_enemies+12*ESIZE
    jb .loop
    test word [n_stage], 3
    jnz .advance
    cmp byte [n_bossmade], 0
    jne .advance
    mov byte [n_bossmade], 1
    mov si, n_enemies
    mov word [si+EX], 112
    mov word [si+EY], 18
    mov word [si+EVX], 1
    mov ax, [n_stage]
    shl ax, 1
    add ax, 30
    mov [si+EHP], ax
    mov word [si+EKIND], 2
    mov word [si+EAGE], 0
    mov word [si+EON], 1
    ret
.advance:
    inc word [n_stage]
    cmp word [n_stage], 32
    jbe .stage
    mov byte [n_state], 3
    mov word [n_stage], 32
    ret
.stage:
    mov word [n_spawned], 0
    mov word [n_spawnwait], 60
    mov byte [n_bossmade], 0
    mov word [n_rolls], 3
    mov byte [n_huddirty], 1
.out:
    ret

n_draw:
    SAVE
    ; Small water wakes move independently over the cached terrain.
    mov si, n_wakeart
    mov cx, 48
    mov dx, [n_wavey]
    call n_sprite
    mov cx, 120
    call n_sprite
    mov cx, 192
    call n_sprite
    mov di, n_enemies
.enemy:
    cmp word [di+EON], 0
    je .enext
    mov si, n_enemyart
    cmp word [di+EKIND], 0
    je .eart
    mov si, n_eliteart
    cmp word [di+EKIND], 1
    je .eart
    mov si, n_bossart
.eart:
    mov cx, [di+EX]
    mov dx, [di+EY]
    call n_sprite
.enext:
    add di, ESIZE
    cmp di, n_enemies+12*ESIZE
    jb .enemy
    cmp word [n_grace], 0
    je .player
    test word [n_frames], 2
    jnz .shots
.player:
    mov si, [n_bank]
    cmp word [n_roll], 0
    je .part
    test word [n_roll], 8
    jz .part
    mov si, n_rollart
.part:
    mov cx, [n_px]
    mov dx, [n_py]
    call n_sprite
.shots:
    mov di, n_shots
.sloop:
    mov cx, [di]
    mov dx, [di+2]
    mov si, n_shotart
    call n_sprite
    add di, 4
    cmp di, n_shots+16*4
    jb .sloop
    mov di, n_bullets
.bloop:
    mov cx, [di]
    mov dx, [di+2]
    mov si, n_bulletart
    call n_sprite
    add di, 8
    cmp di, n_bullets+16*8
    jb .bloop
    mov cx, [n_pickx]
    mov dx, [n_picky]
    mov si, n_pickart
    call n_sprite
    mov di, n_blasts
.xloop:
    cmp word [di+4], 0
    je .xnext
    mov cx, [di]
    mov dx, [di+2]
    mov si, 8
    sub si, [di+4]
    shr si, 1
    add si, n_blast0art
    call n_sprite
.xnext:
    add di, 6
    cmp di, n_blasts+8*6
    jb .xloop
    cmp byte [n_huddirty], 0
    je .hud
    call n_scoretext
    mov byte [n_huddirty], 0
.hud:
    call n_hudcopy
.out:
    RESTORE
    ret
n_scoretext:
    SAVE
    call n_hudbegin
    mov bx, [n_scorelo]
    mov bp, [n_scorehi]
    mov si, n_hud+5
    mov cx, 10
.digit:
    mov ax, bp
    xor dx, dx
    div cx
    mov bp, ax
    mov ax, bx
    div cx
    mov bx, ax
    add dl, '0'
    mov [si], dl
    dec si
    cmp si, n_hud
    jae .digit
    mov ax, [n_lives]
    add al, '0'
    mov [n_hud+9], al
    mov ax, [n_rolls]
    add al, '0'
    mov [n_hud+12], al
    mov ax, [n_stage]
    xor dx, dx
    div cx
    add al, '0'
    add dl, '0'
    mov [n_hud+21], al
    mov [n_hud+22], dl
    mov si, n_hud
    mov cx, 8
    mov dx, 4
    call n_text
    call n_hudend
    call n_hudcopy
    RESTORE
    ret
