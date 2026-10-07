; Native simulation. Actor coordinates are signed logical pixels so scripted
; paths can enter and leave outside the playfield.
EX equ 0
EY equ 2
EVX equ 4
EHP equ 6
EKIND equ 8
EAGE equ 10
EON equ 12
EPATH equ 14
ETYPE equ 16
EGROUP equ 18
EAUX equ 20
EDY equ 22
ESIZE equ 24

; Cartridge limits: $DAC2 aircraft slots, $FABF enemy projectile slots.
N_ENEMY_LIMIT equ 8
N_BULLET_LIMIT equ 8
N_FIRE_NEAR equ 32
N_FIRE_FAR equ 112

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
    mov word [n_scrollstage], 0
    mov word [n_scroll], 0
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
    call n_campaignnew
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
%if N_ROM
    mov si,n_title0art
    xor cx,cx
    mov dx,56
.titlepart:
    call n_sprite
    add cx,64
    inc si
    cmp si,n_title3art
    jbe .titlepart
%else
    mov si,n_msgtitle
    mov cx,100
    mov dx,56
    call n_text
%endif
    mov si,n_msgstart
    mov cx,64
    mov dx,120
    cmp byte [n_state],0
    je .headline
    mov si,n_msgover
    mov cx,92
    cmp byte [n_state],2
    je .headline
    mov si,n_msgwin
    mov cx,64
.headline:
    call n_text
    cmp byte [n_state],0
    jne .endhint
    mov si,n_msgselect
    mov ax,[n_players]
    add al,'0'
    mov [n_msgselect+18],al
    mov cx,24
    mov dx,144
    call n_text
    mov si,n_msghint
    mov cx,52
    mov dx,168
    call n_text
    jmp .high
.endhint:
    mov si,n_msgnew
    mov cx,68
    mov dx,152
    call n_text
.high:
    call n_hightext
    call n_scoretext
.out:
    RESTORE
    ret

n_update:
    cmp word [n_flightphase],0
    jne n_flightphaseupdate
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
    mov ax,21
    call n_effect
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
    mov cx,[n_px]
    add cx,6
    call n_shot
    add cx,8
    call n_shot
    cmp word [n_weapon],2
    jb .shotdone
    sub cx,16
    call n_shot
    add cx,24
    call n_shot
.shotdone:
    call n_wingfire
    mov ax,18
    call n_effect
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
    jmp n_scripts
n_move_enemies:
    mov si, n_enemies
.loop:
    cmp word [si+EON], 0
    je .next
    inc word [si+EAGE]
%if N_ROM
    cmp word [si+EKIND],2
    je .native
    cmp word [si+ETYPE],49
    jae .native
    call n_cartmove
    jc .escaped
    mov bx,[si+EKIND]
    shl bx,1
    jmp .attack
.native:
%endif
    cmp word [si+EKIND],2
    je .waypoint
    cmp word [si+EKIND],6
    jae .waypoint
    cmp word [si+ETYPE],10
    jae .waypoint
    call n_smallmove
    jc .escaped
    mov bx,[si+EKIND]
    shl bx,1
    jmp .attack
.waypoint:
    cmp word [si+EPATH],0
    je .straight
    call n_pathmove
    jc .escaped
    mov bx,[si+EKIND]
    shl bx,1
    jmp .attack
.escaped:
    xor ax,ax
    call n_depart
    jmp .next
.straight:
%if N_ROM
    cmp word [si+EKIND],2
    jne .mobile
    mov bx,4
    jmp .attack
.mobile:
%endif
    mov ax, [si+EVX]
    add [si+EX], ax
    mov bx, [si+EKIND]
    shl bx, 1
    mov ax, 248
    sub ax, [n_enemywidths+bx]
.bound:
    cmp [si+EX], ax
    jae .bounce
    cmp word [si+EX], 8
    ja .vertical
.bounce:
    neg word [si+EVX]
.vertical:
    cmp word [si+EKIND],8
    jne .bosscheck
    sub word [si+EY],3
    cmp word [si+EY],18
    jb .escaped
    jmp .attack
.bosscheck:
    cmp word [si+EKIND], 2
    jne .small
    cmp word [si+EY], 44
    jae .attack
    inc word [si+EY]
    jmp .attack
.small:
    inc word [si+EY]
    cmp word [si+EKIND], 6
    jae .bottom                     ; bombers are slower and tougher
    inc word [si+EY]
    cmp word [si+EKIND], 3
    jne .weave
    inc word [si+EY]                 ; interceptor makes a fast pass
.weave:
    cmp word [si+EKIND], 4
    jb .bottom
    test word [si+EAGE], 15
    jnz .bottom
    neg word [si+EVX]
.bottom:
    mov ax, 238
    sub ax, [n_enemyheights+bx]
    cmp [si+EY], ax
    jbe .attack
    xor ax,ax
    call n_depart
    jmp .next
.attack:
    call n_enemyfire
.contact:
    cmp word [n_wings],0
    je .playercontact
    call n_wingcontact
    jc .contacted
.playercontact:
    mov ax, [n_px]
    add ax, 24
    cmp [si+EX], ax
    jae .next
    mov ax, [si+EX]
    add ax, [n_enemywidths+bx]
    cmp ax, [n_px]
    jbe .next
    mov ax, [n_py]
    add ax, 24
    cmp [si+EY], ax
    jae .next
    mov ax, [si+EY]
    add ax, [n_enemyheights+bx]
    cmp ax, [n_py]
    jbe .next
    call n_hitplayer
.contacted:
    cmp word [si+EKIND], 2
    je .next
    xor ax,ax
    call n_depart
    mov cx, [si+EX]
    mov dx, [si+EY]
    call n_blast
.next:
    cmp word [si+EON],0
    je .advance
    push si
    push di
    mov di,si
    call n_enemyframe
    mov [di+EAUX],si
    pop di
    pop si
.advance:
    add si, ESIZE
    cmp si, n_enemies+12*ESIZE
    jb .loop
    ret
n_enemyfire:
    SAVE
    cmp word [si+EKIND],8          ; secret bonus aircraft
    je .out
    cmp word [si+EX],232
    ja .out
    cmp word [si+EY],216
    ja .out
    cmp word [si+EKIND],2
    je .position
    cmp word [si+ETYPE],48
    je .position
    ; $EF49/$EF6E set the no-fire flag on formations and entry aircraft.
    cmp word [si+ETYPE],9
    jb .fighter
    cmp word [si+ETYPE],33
    jbe .out
.fighter:
    ; $FA43: one shared opportunity counter, not a volley per aircraft age.
    call n_firerank
    shr ax,1
    shr ax,1
    inc word [n_enemyfirecount]
    cmp [n_enemyfirecount],ax
    jbe .out
    mov word [n_enemyfirecount],0ffffh
.position:
    mov bp,[si+EX]
    add bp,12
    mov dx,[si+EY]
    add dx,12
%if N_ROM
    cmp word [si+EKIND],2
    jne .muzzle
    add bp,60
    add dx,64
.muzzle:
%endif
    mov ax,[n_px]
    add ax,12
    sub ax,bp
    mov bx,[n_py]
    add bx,12
    sub bx,dx
    ; $FA55..$FA88: inside the 112px box, outside the 32px box.
    ; Apply the same proximity rule to bombers so distant aircraft stay quiet.
    mov cx,ax
    add cx,N_FIRE_FAR
    cmp cx,N_FIRE_FAR*2
    jae .out
    mov cx,bx
    add cx,N_FIRE_FAR
    cmp cx,N_FIRE_FAR*2
    jae .out
    mov cx,ax
    add cx,N_FIRE_NEAR
    cmp cx,N_FIRE_NEAR*2
    jae .range
    mov cx,bx
    add cx,N_FIRE_NEAR
    cmp cx,N_FIRE_NEAR*2
    jb .out
.range:
    call n_fireangle
    mov cx,ax
    cmp word [si+EKIND],2
    je .bomber
    cmp word [si+ETYPE],48
    je .bomber
    ; $FA89..$FABE: downward aim sectors 6..10 and current heading gate.
    cmp cx,6
    jb .out
    cmp cx,10
    ja .out
    mov ax,[si+EVX]
    mov bx,[si+EDY]
    call n_fireangle
    cmp word [si+ETYPE],0
    jne .heading
    mov bx,ax
    sub bx,3
    and bx,15
    cmp bx,cx
    ja .out
.heading:
    add ax,2
    cmp ax,cx
    jb .out
    jmp .allocate
.bomber:
    ; $FB00/$FD2F: shared sequenced bursts, not three simultaneous shots.
    cmp word [n_burstleft],0
    jne .burst
    call n_firerank
    shr ax,1
    shr ax,1
    shr ax,1
    shr ax,1
    inc word [n_bigfirecount]
    cmp [n_bigfirecount],ax
    jbe .out
    mov word [n_bigfirecount],0
    call n_firerank
    neg al
    test al,40h
    mov bx,0
    jz .pattern
    mov bx,2
.pattern:
    cmp word [si+EKIND],2
    jne .patternptr
    mov bx,4
.patternptr:
    mov bx,[n_firepatterns+bx]
    xor ax,ax
    mov al,[bx]
    mov [n_burstleft],ax
    mov al,[bx+1]
    mov [n_burstperiod],ax
    mov [n_burstwait],ax
    add bx,2
    mov [n_burstptr],bx
    mov [n_burstangle],cx
    jmp .out
.burst:
    dec word [n_burstwait]
    jnz .out
    mov ax,[n_burstperiod]
    mov [n_burstwait],ax
    mov bx,[n_burstptr]
    mov al,[bx]
    ; Pattern bytes are signed offsets, including $FF = aim minus one.
    cbw
    add ax,[n_burstangle]
    and ax,15
    mov cx,ax
.allocate:
    mov di,n_bullets
.find:
    cmp word [di+2],0ffffh
    je .slot
    add di,8
    cmp di,n_bullets+N_BULLET_LIMIT*8
    jb .find
    jmp .out
.slot:
    mov [di],bp
    mov [di+2],dx
    mov bx,cx
    shl bx,1
    mov ax,[n_firevx+bx]
    mov [di+4],ax
    mov ax,[n_firevy+bx]
    mov [di+6],ax
    cmp word [si+EKIND],2
    je .consumed
    cmp word [si+ETYPE],48
    jne .out
.consumed:
    inc word [n_burstptr]
    dec word [n_burstleft]
.out:
    RESTORE
    ret

; NES $03A1 counts down through eight route pages per played stage.
n_firerank:
    push bx
    push dx
    mov ax,[n_distance]
.page:
    xor dx,dx
    mov bx,240
    div bx
    cmp ax,7
    jbe .rank
    mov ax,7
.rank:
    mov bx,ax
    mov ax,32
    sub ax,[n_stage]
    shl ax,1
    shl ax,1
    shl ax,1
    add ax,7
    sub ax,bx
    pop dx
    pop bx
    ret

; $ADC9 quadrant/quantization algorithm. AX=dx, BX=dy; AX=0..15 heading.
n_fireangle:
    push bx
    push cx
    push dx
    xor cx,cx
    test ax,ax
    js .left
    test bx,bx
    js .upright
    mov cx,4
    jmp .absolute
.upright:
    xchg ax,bx
    jmp .absolute
.left:
    mov cx,12
    test bx,bx
    js .absolute
    mov cx,8
    xchg ax,bx
.absolute:
    test ax,ax
    jns .y
    neg ax
.y:
    test bx,bx
    jns .scale
    neg bx
.scale:
    mov dx,ax
    or dx,bx
    cmp dx,16
    jb .lookup
    shr ax,1
    shr bx,1
    jmp .scale
.lookup:
    shl bx,1
    shl bx,1
    shl bx,1
    shl bx,1
    add bx,ax
    xor ax,ax
    mov al,[n_aimtable+bx]
    add ax,cx
    and ax,15
    pop dx
    pop cx
    pop bx
    ret
n_firevx: dw 0,1,2,3,3,3,2,1,0,-1,-2,-3,-3,-3,-2,-1
n_firevy: dw -3,-3,-2,-1,0,1,2,3,3,3,2,1,0,-1,-2,-3
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
    mov bx, [si+EKIND]
    shl bx, 1
    mov bp, [n_enemyheights+bx]
    mov bx, [n_enemywidths+bx]
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
    add ax, bp
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
    mov ax,1
    call n_depart
    inc word [n_kills]
    inc word [n_secretkills]
    mov cx,[si+EX]
    mov dx,[si+EY]
    call n_blast
    cmp word [si+EKIND],8
    jne .ordinary
    mov [n_pickx],cx
    mov [n_picky],dx
    mov word [n_picktype],5
.ordinary:
    mov ax,50
    cmp word [si+EKIND],1
    jne .large
    mov ax,100
    jmp .score
.large:
    cmp word [si+EKIND],2
    jne .medium
    mov ax,20000
    jmp .score
.medium:
    cmp word [si+EKIND],6
    jb .score
    mov ax,1000
    cmp word [si+EKIND],7
    jne .score
    mov ax,1500
.score:
    call n_addscore
    mov ax,19
    call n_effect
    RESTORE
    ret
n_addscore:
    push ax
    push bx
    push dx
    add [n_scorelo],ax
    adc word [n_scorehi],0
    ; Extend counter is in 50-point units, so 80,000 fits one word.
    xor dx,dx
    mov bx,50
    div bx
    sub [n_nextlife],ax
    jg .high
    cmp word [n_lives],9
    jae .threshold
    inc word [n_lives]
.threshold:
    cmp word [n_extended],0
    jne .later
    mov word [n_extended],1
    add word [n_nextlife],1200
    jmp .high
.later:
    add word [n_nextlife],1600
.high:
    mov ax,[n_scorehi]
    cmp ax,[n_highhi]
    jb .done
    ja .record
    mov ax,[n_scorelo]
    cmp ax,[n_highlo]
    jbe .done
.record:
    mov ax,[n_scorelo]
    mov [n_highlo],ax
    mov ax,[n_scorehi]
    mov [n_highhi],ax
.done:
    mov byte [n_huddirty],1
    pop dx
    pop bx
    pop ax
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
    mov word [n_wings],0
    mov word [n_px], 116
    mov word [n_py], 204
    mov byte [n_huddirty], 1
    mov ax,20
    call n_effect
    call n_turn
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
    mov cx,[si]
    mov dx,[si+2]
    call n_winghit
    jc .remove
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
    call n_awardpow
.remove:
    mov word [n_picky], 0ffffh
.out:
    ret
n_nextstage:
    cmp byte [n_state],1
    jne .out
    cmp word [n_flightphase],0
    jne .out
    cmp word [n_distance],1824      ; $DCB3: last page, Y < $61
    jb .out
    cmp byte [n_bosslive],0
    jne .out
    mov word [n_flightphase],2
    mov si,n_enemies
.clear:
    xor ax,ax
    call n_depart
    add si,ESIZE
    cmp si,n_enemies+12*ESIZE
    jb .clear
    push ds
    pop es
    mov di,n_bullets
    mov ax,0ffffh
    mov cx,16*4
    rep stosw
    mov [n_picky],ax
.out:
    ret

n_draw:
    cmp byte [n_cga],0
    je n_drawactors
    mov byte [n_prepare],1
    call n_drawactors
    call n_damagebuild
    mov byte [n_prepare],2
    call n_drawactors
    mov byte [n_prepare],0
    ret
n_drawactors:
    SAVE
    mov di, n_enemies
.enemy:
    cmp word [di+EON], 0
    je .enext
%if N_ROM
    cmp word [di+EKIND],2
    je .enext
%endif
    mov si,[di+EAUX]
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
    mov ax,48
    sub ax,[n_roll]
    xor dx,dx
    mov bx,7
    div bx
    cmp ax,6
    jbe .loopframe
    mov ax,6
.loopframe:
    mov si,n_loop0art
    add si,ax
.part:
    mov cx, [n_px]
    cmp word [n_roll],0
    je .playerpos
    sub cx,4
    jnc .rollright
    xor cx,cx
.rollright:
    cmp cx,224
    jbe .playerpos
    mov cx,224
.playerpos:
    mov dx, [n_py]
    call n_sprite
.shots:
    call n_wingdraw
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
    mov si,n_pow0art
    add si,[n_picktype]
    cmp word [n_picktype],5
    jne .pickup
    mov si,n_bonusart
.pickup:
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
    call n_flightphaseart
    cmp byte [n_huddirty], 0
    je .hud
    call n_scoretext
    mov byte [n_huddirty], 0
    jmp .out
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
    mov ax,33
    sub ax,[n_stage]
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
