; RLE level unpack, platform physics, tile interactions and bounded enemy AI.
; New adventure/course: reset consumed tiles and select authored safe arrival.
st_newcourse:
 push ds
 pop es
 mov di,st_rewards
 xor ax,ax
 mov cx,ST_MAX_WIDTH*ST_MAP_ROWS/16
 rep stosw
 mov word [st_actor_rewards],0
 mov byte [st_actor_rewards+2],0
 mov ax,[st_level]
 mov [st_ledger_level],ax
 mov bx,ax
 shl bx,1
 mov si,[st_levels+bx]
 mov ax,[si+10]
 mov [st_checkpoint],ax
 mov ax,[si+12]
 mov [st_checkpoint_y],ax
 ret
st_load:
 mov ax,[st_level]
 cmp ax,[st_ledger_level]
 je .samecourse
 call st_newcourse
.samecourse:
 push ds
 pop es
 mov bx,[st_level]
 shl bx,1
 mov si,[st_levels+bx]
 mov ax,[si]
 mov [st_width],ax
 mov ax,[si+6]
 mov [st_name],ax
 mov al,[si+9]
 mov [st_world],al
 xor ax,ax
 mov al,[si+8]
 mov [st_ne],ax
 mov bp,[si+4]
 mov si,[si+2]
 mov di,st_map
 mov dx,[st_width]
 shl dx,1
 shl dx,1
 shl dx,1
.rle:
 lodsb
 xor cx,cx
 mov cl,al
 lodsb
 sub dx,cx
 rep stosb
 or dx,dx
 jnz .rle
 push bp
 call st_restore_rewards
 pop bp
 mov cx,[st_ne]
 mov di,st_enemies
 mov si,bp
 jcxz .spawn
.enemy:
 movsw
 movsw
 movsb
 movsb
 xor ax,ax
 stosw
 stosw
 stosw
 mov ax,[di-10]
 stosw
 stosw
 loop .enemy
.spawn:
 mov ax,[st_checkpoint]
 mov [st_x],ax
 mov ax,[st_checkpoint_y]
 mov cl,8
 shl ax,cl
 mov [st_y],ax
 mov word [st_vx],0
 mov word [st_vy],0
 mov word [st_frac],0
 mov word [st_gait],0
 mov byte [st_ground],1
 mov byte [st_coyote],ST_ASSIST
 mov byte [st_jumpbuf],0
 mov byte [st_land],0
 mov byte [st_invuln],ST_PROTECTION
 mov byte [st_facing],0
 mov byte [st_state],0
 mov byte [st_quit],0
 mov byte [st_mapdirty],1
 mov byte [st_huddirty],1
 mov word [st_drawcam],0ffffh
 mov byte [st_ephase],0
 mov byte [st_fx],0
 call st_camera
 ret
st_respawn:
 cmp byte [st_lives],1
 jbe .over
 dec byte [st_lives]
 call st_load
 mov al,3
 jmp st_effect
.over:
 mov byte [st_lives],0
 mov byte [st_state],2
 mov byte [st_huddirty],1
 mov al,3
 jmp st_effect
; AX=world x, DX=logical y; AL=tile, BX=map address. Out-of-world is empty.
; preserves CX, DX, SI, DI, BP. Negative y allows jumping above the screen.
st_tile:
 cmp ax,0
 jl .air
 cmp dx,0
 jl .air
 cmp dx,128
 jae .air
 push cx
 mov cl,4
 shr ax,cl
 cmp ax,[st_width]
 jae .outside
 shl ax,1
 shl ax,1
 shl ax,1
 mov bx,dx
 shr bx,cl
 add bx,ax
 add bx,st_map
 mov al,[bx]
 pop cx
 ret
.outside:
 pop cx
.air:
 xor al,al
 xor bx,bx
 ret
; ZF = solid for IDs 1..3; carry set means solid.
st_solid:
 cmp al,1
 jb .no
 cmp al,3
 ja .no
 stc
 ret
.no: clc
 ret
st_step:
 cmp byte [st_invuln],0
 je .land
 dec byte [st_invuln]
.land:
 cmp byte [st_land],0
 je .buf
 dec byte [st_land]
.buf:
 cmp byte [st_jumpbuf],0
 je .held
 dec byte [st_jumpbuf]
.held:
 test byte [st_keys],4
 jz .release
 cmp byte [st_jumpheld],0
 jne .coyote
 mov byte [st_jumpbuf],ST_ASSIST
 mov byte [st_jumpheld],1
 jmp .coyote
.release:
 mov byte [st_jumpheld],0
 cmp word [st_vy],ST_RELEASE
 jge .coyote
 mov word [st_vy],ST_RELEASE
.coyote:
 cmp byte [st_ground],0
 je .air
 mov byte [st_coyote],ST_ASSIST
 jmp .jump
.air:
 cmp byte [st_coyote],0
 je .jump
 dec byte [st_coyote]
.jump:
 cmp byte [st_jumpbuf],0
 je .accel
 cmp byte [st_coyote],0
 je .accel
 mov word [st_vy],ST_JUMP
 mov byte [st_ground],0
 mov byte [st_coyote],0
 mov byte [st_jumpbuf],0
 mov al,1
 call st_effect
.accel:
 mov ax,[st_vx]
 mov dx,ST_WALK
 test byte [st_keys],8
 jz .lr
 mov dx,ST_RUN
.lr:
 mov bl,[st_keys]
 and bl,3
 cmp bl,1
 je .left
 cmp bl,2
 je .right
 ; Friction crosses zero without oscillation.
 cmp ax,0
 je .velocity
 jl .frneg
 sub ax,ST_FRICTION
 jns .velocity
 xor ax,ax
 jmp .velocity
.frneg:
 add ax,ST_FRICTION
 js .velocity
 xor ax,ax
 jmp .velocity
.left:
 mov byte [st_facing],1
 sub ax,ST_ACCEL
 neg dx
 cmp ax,dx
 jge .velocity
 mov ax,dx
 jmp .velocity
.right:
 mov byte [st_facing],0
 add ax,ST_ACCEL
 cmp ax,dx
 jle .velocity
 mov ax,dx
.velocity:
 mov [st_vx],ax
 add ax,[st_frac]
 xor bx,bx
 mov bl,al
 mov [st_frac],bx
 mov al,ah
 cbw
 add ax,[st_x]
 cmp ax,0
 jge .limit
 xor ax,ax
.limit:
 mov bx,[st_width]
 mov cl,4
 shl bx,cl
 sub bx,16
 cmp ax,bx
 jbe .horizontal
 mov ax,bx
.horizontal:
 mov [st_cur],ax
 mov dx,[st_y]
 mov cl,8
 sar dx,cl
 add dx,ST_PLAYER_TOP
 cmp word [st_vx],0
 jl .edge
 add ax,ST_PLAYER_RIGHT+1
.edge:
 push ax
 call st_tile
 call st_solid
 pop ax
 jc .wall
 add dx,ST_PLAYER_BOTTOM-ST_PLAYER_TOP
 call st_tile
 call st_solid
 jc .wall
 mov ax,[st_cur]
 mov bx,ax
 sub bx,[st_x]
 jns .travel
 neg bx
.travel:
 add [st_gait],bx
 mov [st_x],ax
 jmp .vertical
.wall:
 mov word [st_vx],0
 mov word [st_frac],0
.vertical:
 mov ax,[st_y]
 mov [st_oldy],ax
 mov bx,[st_vy]
 add bx,ST_GRAVITY
 cmp bx,ST_FALL
 jle .fallcap
 mov bx,ST_FALL
.fallcap:
 mov [st_vy],bx
 add ax,bx
 jo .die
 mov [st_y],ax
 mov byte [st_ground],0
 mov dx,ax
 mov cl,8
 sar dx,cl
 cmp bx,0
 jl .head
 add dx,ST_PLAYER_FEET
 mov ax,[st_x]
 add ax,ST_PLAYER_LEFT
 call st_feet
 jc .floor
 mov ax,[st_x]
 add ax,ST_PLAYER_RIGHT
 call st_feet
 jnc .touch
.floor:
 push ax
 mov ax,dx
 and ax,0fff0h
 sub ax,ST_PLAYER_FEET
 mov cl,8
 shl ax,cl
 mov [st_y],ax
 mov word [st_vy],0
 mov byte [st_ground],1
 pop ax
 cmp al,6
 jne .impact
 mov word [st_vy],ST_SPRING
 mov byte [st_ground],0
 mov byte [st_coyote],0
 mov al,1
 call st_effect
 jmp .touch
.impact:
 mov ax,[st_oldy]
 sub ax,[st_y]
 cmp ax,-256
 jge .touch
 mov byte [st_land],ST_ASSIST
 jmp .touch
.head:
 mov ax,[st_x]
 add ax,ST_PLAYER_LEFT
 call st_tile
 call st_solid
 jc .bonk
 mov ax,[st_x]
 add ax,ST_PLAYER_RIGHT
 call st_tile
 call st_solid
 jnc .touch
.bonk:
 cmp al,3
 jne .ceiling
 call st_consume
 mov byte [bx],2
 mov byte [st_mapdirty],1
 call st_coin
.ceiling:
 mov ax,dx
 and ax,0fff0h
 add ax,16
 mov cl,8
 shl ax,cl
 mov [st_y],ax
 mov word [st_vy],0
.touch:
 mov dx,[st_y]
 mov cl,8
 sar dx,cl
 add dx,16
 mov ax,[st_x]
 add ax,8
 call st_tile
 cmp al,4
 jne .spike
 call st_consume
 mov byte [bx],0
 mov byte [st_mapdirty],1
 call st_coin
 jmp .enemies
.spike:
 cmp al,5
 je .die
 cmp al,6
 jne .flag
 cmp word [st_vy],0
 jl .enemies
 mov word [st_vy],ST_SPRING
 mov byte [st_ground],0
 mov byte [st_coyote],0
 mov al,1
 call st_effect
 jmp .enemies
.flag:
 cmp al,8
 jne .exit
 push bx
 mov bx,[st_level]
 shl bx,1
 mov bx,[st_levels+bx]
 mov ax,[bx+14]
 mov [st_checkpoint],ax
 mov ax,[bx+16]
 mov [st_checkpoint_y],ax
 pop bx
 call st_consume
 mov byte [bx],0
 mov byte [st_mapdirty],1
 mov al,5
 call st_effect
 jmp .enemies
.exit:
 cmp al,7
 jne .enemies
 mov byte [st_state],1
 cmp word [st_level],ST_LEVEL_COUNT-1
 jne .won
 mov byte [st_state],3
.won:
 add word [st_score],100
 mov byte [st_huddirty],1
 mov al,5
 call st_effect
.enemies:
 call st_enemy_step
 mov dx,[st_y]
 cmp dx,127*256
 jg .die
 inc byte [st_anim]
 call st_camera
 ret
.die:
 jmp st_respawn
; Foot collision includes one-way ledges, only crossed from above.
st_feet:
 call st_tile
 call st_solid
 jc .out
 cmp al,9
 je .oneway
 cmp al,6
 je .oneway
 clc
 ret
.oneway:
 push ax
 mov ax,[st_oldy]
 mov cl,8
 sar ax,cl
 add ax,ST_PLAYER_FEET
 mov bx,dx
 and bx,0fff0h
 cmp ax,bx
 pop ax
 ja .no
 stc
.out: ret
.no: clc
 ret
st_coin:
 SAVE
 inc word [st_coins]
 add word [st_score],10
 mov ax,[st_coins]
 xor dx,dx
 mov cx,50
 div cx
 or dx,dx
 jnz .tone
 cmp byte [st_lives],9
 jae .tone
 inc byte [st_lives]
.tone:
 mov byte [st_huddirty],1
 mov al,2
 call st_effect
 RESTORE
 ret
st_camera:
 mov ax,[st_x]
 sub ax,120
 jns .max
 xor ax,ax
.max:
 mov bx,[st_width]
 mov cl,4
 shl bx,cl
 sub bx,320
 cmp ax,bx
 jbe .align
 mov ax,bx
.align:
 and ax,0fffch
 mov [st_cam],ax
 ret
%include "actors.inc"

; BX=consumed tile address. One bit per map tile, stable across RLE reloads.
st_consume:
 SAVE
 mov ax,bx
 sub ax,st_map
 mov cx,ax
 and cx,7
 shr ax,1
 shr ax,1
 shr ax,1
 mov bx,ax
 mov al,1
 shl al,cl
 or [st_rewards+bx],al
 RESTORE
 ret
st_restore_rewards:
 mov di,st_map
 xor bp,bp
 mov dx,[st_width]
 shl dx,1
 shl dx,1
 shl dx,1
.next:
 mov bx,bp
 shr bx,1
 shr bx,1
 shr bx,1
 mov cx,bp
 and cx,7
 mov al,1
 shl al,cl
 test [st_rewards+bx],al
 jz .skip
 cmp byte [di],3
 jne .empty
 mov byte [di],2
 jmp .skip
.empty:
 mov byte [di],0
.skip:
 inc di
 inc bp
 cmp bp,dx
 jb .next
 ret
