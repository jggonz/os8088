; Bounded terrain-aware actors. Record layout and limits: SPEC.md 103.
st_enemy_step:
 mov byte [st_contact_count],0
 mov ax,[st_ne]
 mov cl,4
 shl ax,cl
 add ax,st_enemies
 mov [st_enemy_end],ax
 mov si,st_enemies
.next:
 cmp si,[st_enemy_end]
 jae .out
 cmp byte [si+4],0
 je .skip
 mov ax,[si]
 sub ax,[st_cam]
 cmp ax,ST_ACTIVE_LEFT
 jl .skip
 cmp ax,ST_ACTIVE_RIGHT
 jg .skip
 mov ax,[si+2]
 mov [si+12],ax
 inc word [si+6]
 cmp byte [si+4],3
 je .fly
 call st_actor_ground
 jmp .hit
.fly:
 mov al,[si+5]
 cbw
 add [si],ax
 mov ax,[si+6]
 and ax,63
 cmp ax,32
 jbe .wave
 mov bx,64
 sub bx,ax
 mov ax,bx
.wave:
 add ax,[si+14]
 mov [si+2],ax
 test word [si+6],63
 jnz .hit
 neg byte [si+5]
.hit:
 cmp word [si+2],128
 jl .visible
 mov byte [si+4],0
 jmp .skip
.visible:
 ; Match the current renderer until P1 adds horizontal clipping.
 mov ax,[si]
 sub ax,[st_cam]
 cmp ax,0
 jl .skip
 cmp ax,ST_DRAW_RIGHT
 jg .skip
 ; All nearby records simulate, including overflow. Only the same first six
 ; origins drawn by video.inc can contact the player; report invalid density.
 cmp byte [st_contact_count],ST_NEAR_ENEMIES
 jb .admitted
 inc word [st_actor_overflow]
 jmp .skip
.admitted:
 inc byte [st_contact_count]
 mov ax,[st_x]
 sub ax,[si]
 cmp ax,ST_ENEMY_LEFT-ST_PLAYER_RIGHT
 jl .skip
 cmp ax,ST_ENEMY_RIGHT-ST_PLAYER_LEFT
 jg .skip
 mov dx,[st_y]
 mov cl,8
 sar dx,cl
 sub dx,[si+2]
 cmp dx,ST_ENEMY_TOP-ST_PLAYER_FEET
 jl .skip
 cmp dx,ST_ENEMY_FEET-ST_PLAYER_TOP
 jg .skip
 cmp word [st_vy],0
 jle .hurt
 mov ax,[st_oldy]
 mov cl,8
 sar ax,cl
 add ax,ST_PLAYER_FEET
 mov dx,[si+12]
 add dx,ST_ENEMY_TOP
 cmp ax,dx
 ja .hurt
 mov byte [si+4],0
 mov word [st_vy],ST_STOMP
 call st_actor_reward
 mov al,4
 call st_effect
 jmp .skip
.hurt:
 cmp byte [st_invuln],0
 jne .skip
 call st_respawn
 ret
.skip:
 add si,ST_ENEMY_SIZE
 jmp .next
.out: ret

st_actor_ground:
 ; Advance x only after two body probes accept it. Preserve fractional y.
 mov al,[si+5]
 cbw
 add ax,[si]
 mov [st_cur],ax
 cmp byte [si+5],0
 jl .left
 add ax,ST_ENEMY_RIGHT
 jmp .wall
.left:
 add ax,ST_ENEMY_LEFT
.wall:
 mov dx,[si+2]
 add dx,ST_ENEMY_TOP
 push ax
 call st_tile
 call st_solid
 pop ax
 jc .turn
 mov dx,[si+2]
 add dx,ST_ENEMY_FEET-1
 call st_tile
 call st_solid
 jc .turn
 ; Cautious hoppers turn at unsupported edges while on support.
 cmp byte [si+4],2
 jne .move
 cmp word [si+8],0
 jne .move
 mov ax,[st_cur]
 cmp byte [si+5],0
 jl .ledgeleft
 add ax,ST_ENEMY_RIGHT
 jmp .ledge
.ledgeleft:
 add ax,ST_ENEMY_LEFT
.ledge:
 mov dx,[si+2]
 add dx,ST_ENEMY_FEET
 call st_tile
 call st_solid
 jc .move
 cmp al,9
 je .move
 cmp al,6
 je .move
.turn:
 neg byte [si+5]
 jmp .vertical
.move:
 mov ax,[st_cur]
 mov [si],ax
.vertical:
 mov ax,[si+8]
 add ax,ST_GRAVITY
 cmp ax,ST_FALL
 jle .cap
 mov ax,ST_FALL
.cap:
 mov [si+8],ax
 add ax,[si+10]
 xor bx,bx
 mov bl,al
 mov [si+10],bx
 mov al,ah
 cbw
 add [si+2],ax
 mov dx,[si+2]
 cmp word [si+8],0
 jl .head
 add dx,ST_ENEMY_FEET
 mov ax,[si]
 add ax,ST_ENEMY_LEFT
 call st_actor_feet
 jc .land
 mov ax,[si]
 add ax,ST_ENEMY_RIGHT
 call st_actor_feet
 jnc .out
.land:
 mov ax,dx
 and ax,0fff0h
 sub ax,ST_ENEMY_FEET
 mov [si+2],ax
 mov word [si+8],0
 mov word [si+10],0
 cmp byte [si+4],2
 jne .out
 mov ax,[si+6]
 and ax,ST_HOP_PERIOD-1
 cmp ax,6                    ; six-step grounded warning before launch
 jne .out
 mov word [si+8],ST_HOP
 ret
.head:
 add dx,ST_ENEMY_TOP
 mov ax,[si]
 add ax,ST_ENEMY_LEFT
 call st_tile
 call st_solid
 jc .ceiling
 mov ax,[si]
 add ax,ST_ENEMY_RIGHT
 call st_tile
 call st_solid
 jnc .out
.ceiling:
 mov ax,dx
 and ax,0fff0h
 add ax,16-ST_ENEMY_TOP
 mov [si+2],ax
 mov word [si+8],0
 mov word [si+10],0
.out: ret

; DX=current feet, old feet from the actor record. Solid and one-way support.
st_actor_feet:
 call st_tile
 call st_solid
 jc .out
 cmp al,9
 je .crossed
 cmp al,6
 jne .no
.crossed:
 mov ax,[si+12]
 add ax,ST_ENEMY_FEET
 mov bx,dx
 and bx,0fff0h
 cmp ax,bx
 ja .no
 stc
.out: ret
.no: clc
 ret

; Reconstructed encounters may be fought again, but their score pays once.
st_actor_reward:
 SAVE
 mov bx,si
 sub bx,st_enemies
 mov cl,4
 shr bx,cl
 mov cx,bx
 and cx,7
 shr bx,1
 shr bx,1
 shr bx,1
 mov al,1
 shl al,cl
 test [st_actor_rewards+bx],al
 jnz .out
 or [st_actor_rewards+bx],al
 add word [st_score],25
 mov byte [st_huddirty],1
.out:
 RESTORE
 ret
