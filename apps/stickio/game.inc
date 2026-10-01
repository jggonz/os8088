; RLE level unpack, platform physics, tile interactions and bounded enemy AI.
st_load:
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
 loop .enemy
.spawn:
 mov ax,[st_checkpoint]
 mov [st_x],ax
 mov word [st_y],72*256
 mov word [st_vx],0
 mov word [st_vy],0
 mov word [st_frac],0
 mov word [st_gait],0
 mov byte [st_ground],1
 mov byte [st_coyote],5
 mov byte [st_jumpbuf],0
 mov byte [st_land],0
 mov byte [st_invuln],90
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
 mov byte [st_jumpbuf],5
 mov byte [st_jumpheld],1
 jmp .coyote
.release:
 mov byte [st_jumpheld],0
 cmp word [st_vy],-640
 jge .coyote
 mov word [st_vy],-640
.coyote:
 cmp byte [st_ground],0
 je .air
 mov byte [st_coyote],5
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
 mov word [st_vy],-1664
 mov byte [st_ground],0
 mov byte [st_coyote],0
 mov byte [st_jumpbuf],0
 mov al,1
 call st_effect
.accel:
 mov ax,[st_vx]
 mov dx,448
 test byte [st_keys],8
 jz .lr
 mov dx,768
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
 sub ax,64
 jns .velocity
 xor ax,ax
 jmp .velocity
.frneg:
 add ax,64
 js .velocity
 xor ax,ax
 jmp .velocity
.left:
 mov byte [st_facing],1
 sub ax,48
 neg dx
 cmp ax,dx
 jge .velocity
 mov ax,dx
 jmp .velocity
.right:
 mov byte [st_facing],0
 add ax,48
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
 add dx,3
 cmp word [st_vx],0
 jl .edge
 add ax,13
.edge:
 push ax
 call st_tile
 call st_solid
 pop ax
 jc .wall
 add dx,19
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
 add bx,96
 cmp bx,1536
 jle .fallcap
 mov bx,1536
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
 add dx,24
 mov ax,[st_x]
 add ax,3
 call st_feet
 jc .floor
 mov ax,[st_x]
 add ax,12
 call st_feet
 jnc .touch
.floor:
 push ax
 mov ax,dx
 and ax,0fff0h
 sub ax,24
 mov cl,8
 shl ax,cl
 mov [st_y],ax
 mov word [st_vy],0
 mov byte [st_ground],1
 pop ax
 cmp al,6
 jne .impact
 mov word [st_vy],-2176
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
 mov byte [st_land],5
 jmp .touch
.head:
 mov ax,[st_x]
 add ax,3
 call st_tile
 call st_solid
 jc .bonk
 mov ax,[st_x]
 add ax,12
 call st_tile
 call st_solid
 jnc .touch
.bonk:
 cmp al,3
 jne .ceiling
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
 mov word [st_vy],-2176
 mov byte [st_ground],0
 mov byte [st_coyote],0
 mov al,1
 call st_effect
 jmp .enemies
.flag:
 cmp al,8
 jne .exit
 mov ax,[st_x]
 and ax,0fff0h
 mov [st_checkpoint],ax
 mov byte [bx],0
 mov byte [st_mapdirty],1
 mov al,5
 call st_effect
 jmp .enemies
.exit:
 cmp al,7
 jne .enemies
 mov byte [st_state],1
 cmp word [st_level],29
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
 add ax,24
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
; Enemy record: x word, y word, type byte (0 dead), direction signed byte,
; age word, reserved word. At most six nearby enemies step/draw in a frame.
st_enemy_step:
 mov si,st_enemies
 mov cx,[st_ne]
 xor bp,bp
 or cx,cx
 jz .out
.next:
 cmp byte [si+4],0
 je .skip
 mov ax,[si]
 sub ax,[st_cam]
 cmp ax,-32
 jl .skip
 cmp ax,336
 jg .skip
 cmp bp,6
 jae .skip
 inc bp
 inc word [si+6]
 mov al,[si+5]
 cbw
 add [si],ax
 cmp byte [si+4],3
 je .fly
 ; Walking/hopping: turn at walls and at the edge of support.
 mov ax,[si]
 add ax,8
 cmp byte [si+5],0
 jl .edge
 add ax,8
.edge:
 mov dx,95
 call st_tile
 call st_solid
 jc .turn
 mov ax,[si]
 add ax,8
 cmp byte [si+5],0
 jl .support
 add ax,8
.support:
 mov dx,97
 call st_tile
 call st_solid
 jc .height
.turn:
 neg byte [si+5]
.height:
 mov word [si+2],72
 cmp byte [si+4],2
 jne .hit
 ; Triangular, bounded hop, six-step warning crouch between jumps.
 mov ax,[si+6]
 and ax,63
 cmp ax,32
 jae .hit
 cmp ax,16
 jbe .hop
 mov bx,32
 sub bx,ax
 mov ax,bx
.hop:
 shl ax,1
 sub [si+2],ax
 jmp .hit
.fly:
 mov ax,[si+6]
 and ax,63
 cmp ax,32
 jbe .wave
 mov bx,64
 sub bx,ax
 mov ax,bx
.wave:
 add ax,28
 mov [si+2],ax
 ; Flyers patrol a 64-pixel span instead of drifting through the level.
 mov ax,[si+6]
 and ax,63
 jnz .hit
 neg byte [si+5]
.hit:
 mov ax,[st_x]
 sub ax,[si]
 cmp ax,-12
 jl .skip
 cmp ax,12
 jg .skip
 mov dx,[st_y]
 mov cl,8
 sar dx,cl
 sub dx,[si+2]
 cmp dx,-22
 jl .skip
 cmp dx,18
 jg .skip
 cmp word [st_vy],0
 jle .hurt
 mov ax,[st_oldy]
 mov cl,8
 sar ax,cl
 add ax,24
 mov dx,[si+2]
 add dx,12
 cmp ax,dx
 ja .hurt
 mov byte [si+4],0
 mov word [st_vy],-1152
 add word [st_score],25
 mov byte [st_huddirty],1
 mov al,4
 call st_effect
 jmp .skip
.hurt:
 cmp byte [st_invuln],0
 jne .skip
 call st_respawn
 ret
.skip:
 add si,10
 ; The tile/hit work uses CL. Bound the loop by the end address instead.
 mov ax,si
 sub ax,st_enemies
 xor dx,dx
 mov bx,10
 div bx
 cmp ax,[st_ne]
 jb .next
.out: ret
