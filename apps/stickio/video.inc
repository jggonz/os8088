; Byte-column cache and transparent 16x24 sprites, shared CGA/Hercules raster.
; CGA: 00 black, 11 white. Hercules: the same bits double logical x;
; each logical scanline is duplicated to maintain the character proportions.
st_video_init:
 mov es,[st_fsi+FSI_SEG]
 xor di,di
 mov ax,0ffffh
 mov cx,16384/2
 cmp byte [st_herc],0
 je .clear
 mov cx,32768/2
.clear:
 cld
 rep stosw
 push ds
 pop es
 mov di,st_bg
 mov cx,80*128/2
 rep stosw
 mov di,st_cache
 mov cx,80*8/2
 rep stosw
 mov di,st_groupcache
 mov cx,160
 rep stosw
 mov word [st_nboxes],0
 xor bp,bp
.row:
 mov ax,bp
 cmp byte [st_herc],0
 jne .herc
 mov bx,ax
 and bx,1
 mov cl,13
 shl bx,cl
 shr ax,1
 mov dx,80
 mul dx
 add ax,bx
 mov bx,bp
 shl bx,1
 mov [st_screenrows+bx],ax
 jmp .next
.herc:
 shl ax,1
 call st_hercrow
 mov bx,bp
 shl bx,1
 mov [st_screenrows+bx],ax
 mov ax,bp
 shl ax,1
 inc ax
 call st_hercrow
 mov bx,bp
 shl bx,1
 mov [st_screenrows+400+bx],ax
.next:
 inc bp
 cmp bp,200
 jb .row
 xor bp,bp
.game:
 mov bx,bp
 add bx,32
 shl bx,1
 mov ax,[st_screenrows+bx]
 mov di,bp
 shl di,1
 mov [st_rows+di],ax
 inc bp
 cmp bp,128
 jb .game
 ret
st_hercrow:
 mov bx,ax
 and bx,3
 mov cl,13
 shl bx,cl
 shr ax,1
 shr ax,1
 mov dx,90
 mul dx
 add ax,bx
 add ax,5
 ret
st_render:
st_render_begin equ $
 ; Build changed terrain before erasing sprites to shorten their invisible time.
 mov ax,[st_cam]
 cmp ax,[st_drawcam]
 jne .tiles
 cmp byte [st_mapdirty],0
 je .sprites
.tiles:
 mov [st_drawcam],ax
 mov byte [st_mapdirty],0
 mov cl,4
 shr ax,cl
 shl ax,1
 shl ax,1
 shl ax,1
 add ax,st_map
 mov [st_mptr],ax
 mov ax,[st_cam]
 shr ax,1
 shr ax,1
 and ax,3
 mov [st_phase],ax
 mov word [st_groupcol],0
.group:
 mov word [st_crow],0
.grouprow:
 ; A second cache rejects an entire 16x16 group before its four byte-columns.
 ; Key is {phase, left tile | right tile << 4}. Uniform sky/soil ignores phase.
 mov bx,[st_mptr]
 add bx,[st_crow]
 mov al,[bx]
 mov ah,al
 cmp word [st_phase],0
 je .rightkey
 mov ah,[bx+8]
.rightkey:
 mov dl,0
 cmp al,ah
 jne .phasekey
 cmp al,2
 jb .key
.phasekey:
 mov dl,[st_phase]
.key:
 mov cl,4
 shl ah,cl
 or al,ah
 mov ah,dl
 mov bx,[st_groupcol]
 shl bx,1
 add bx,[st_crow]
 shl bx,1
 cmp [st_groupcache+bx],ax
 je .groupskip
 mov [st_groupcache+bx],ax
 mov ax,[st_groupcol]
 mov [st_col],ax
 mov word [st_part],0
.part:
 mov ax,[st_phase]
 add ax,[st_part]
 mov dx,ax
 and dx,3
 mov bx,[st_mptr]
 cmp ax,4
 jb .mapleft
 add bx,8
.mapleft:
 add bx,[st_crow]
 xor ax,ax
 mov al,[bx]
 shl ax,1
 shl ax,1
 add ax,dx
 mov bx,ax
 mov al,[st_tilekeys+bx]
 mov bx,[st_col]
 shl bx,1
 shl bx,1
 shl bx,1
 add bx,[st_crow]
 cmp [st_cache+bx],al
 je .partskip
 mov [st_cache+bx],al
 xor ah,ah
 mov cl,4
 shl ax,cl
 add ax,st_columns
 mov si,ax
 mov ax,[st_crow]
 shl ax,1
 shl ax,1
 shl ax,1
 shl ax,1
 mov bp,ax
 mov dx,80
 mul dx
 add ax,[st_col]
 add ax,st_bg
 mov di,ax
 shl bp,1
 mov cx,16
 mov es,[st_fsi+FSI_SEG]
.line:
 lodsb
 mov [di],al
 mov bx,[ds:st_rows+bp]
 add bx,[st_col]
 mov [es:bx],al
 cmp byte [st_herc],0
 je .single
 mov bx,[ds:st_screenrows+400+64+bp]
 add bx,[st_col]
 mov [es:bx],al
.single:
 add di,80
 add bp,2
 loop .line
.partskip:
 inc word [st_col]
 inc word [st_part]
 cmp word [st_part],4
 jb .part
.groupskip:
 inc word [st_crow]
 cmp word [st_crow],8
 jb .grouprow
 add word [st_mptr],8
 add word [st_groupcol],4
 cmp word [st_groupcol],80
 jb .group
.sprites:
 ; Restore footprints from the updated background immediately before drawing.
 mov si,st_oldboxes
 mov cx,[st_nboxes]
 jcxz .restored
.restore:
 push cx
 push si
 mov ax,[si]
 mov dx,[si+2]
 mov word [st_sprite],0
 call st_box
 pop si
 pop cx
 add si,4
 loop .restore
.restored:
 mov word [st_nboxes],0
 ; Gait cadence follows actual travel speed, and has an explicit impact pose.
 mov bx,8
 cmp byte [st_ground],0
 je .air
 cmp byte [st_land],0
 jne .landing
 cmp word [st_vx],0
 je .facing
 mov ax,[st_gait]
 xor dx,dx
 mov bx,3
 div bx
 mov bx,ax
 and bx,7
 jmp .facing
.landing:
 mov bx,11
 jmp .facing
.air:
 mov bx,9
 cmp word [st_vy],0
 jl .facing
 inc bx
.facing:
 cmp byte [st_facing],0
 je .player
 add bx,12
.player:
 cmp byte [st_invuln],0
 je .drawplayer
 test byte [st_anim],4
 jnz .enemies
.drawplayer:
 mov ax,[st_x]
 sub ax,[st_cam]
 mov dx,[st_y]
 mov cl,8
 sar dx,cl
 call st_drawsprite
.enemies:
 mov si,st_enemies
 mov bp,[st_ne]
 xor di,di
 or bp,bp
 jz .hud
.enemy:
 cmp byte [si+4],0
 je .enext
 cmp di,6
 jae .hud
 mov ax,[si]
 sub ax,[st_cam]
 cmp ax,0
 jl .enext
 cmp ax,304
 jg .enext
 inc di
 mov dx,[si+2]
 xor bx,bx
 mov bl,[si+4]
 dec bx
 shl bx,1
 add bx,24
 test byte [si+6],8
 jz .epose
 inc bx
.epose:
 push si
 push bp
 push di
 call st_drawsprite
 pop di
 pop bp
 pop si
.enext:
 add si,10
 dec bp
 jnz .enemy
.hud:
 cmp byte [st_huddirty],0
 je .out
 call st_hudpaint
.out: ret
st_drawsprite:
 cmp ax,0
 jl .out
 cmp ax,304
 jg .out
 cmp dx,-23
 jl .out
 cmp dx,127
 jg .out
 and ax,0fffch
 push ax
 push dx
 mov ax,bx
 mov cx,96
 mul cx
 add ax,st_sprites
 mov [st_sprite],ax
 pop dx
 pop ax
 mov bx,[st_nboxes]
 shl bx,1
 shl bx,1
 mov [st_oldboxes+bx],ax
 mov [st_oldboxes+bx+2],dx
 inc word [st_nboxes]
 call st_box
.out: ret
; AX=aligned x, DX=logical y, sprite=0 restore, otherwise AND mask.
st_box:
 mov [st_screenx],ax
 mov [st_screeny],dx
 shr ax,1
 shr ax,1
 mov [st_col],ax
 mov si,[st_sprite]
 mov cx,24
 mov es,[st_fsi+FSI_SEG]
.row:
 push cx
 mov ax,[st_screeny]
 cmp ax,0
 jl .off
 cmp ax,128
 jae .off
 mov dx,80
 mul dx
 add ax,[st_col]
 add ax,st_bg
 mov di,ax
 mov bx,[st_screeny]
 shl bx,1
 mov bp,bx
 mov bx,[st_rows+bx]
 add bx,[st_col]
 mov ax,[di]
 mov dx,[di+2]
 or si,si
 jz .write
 and ax,[si]
 and dx,[si+2]
.write:
 mov [es:bx],ax
 mov [es:bx+2],dx
 cmp byte [st_herc],0
 je .off
 mov bx,[ds:st_screenrows+400+64+bp]
 add bx,[st_col]
 mov [es:bx],ax
 mov [es:bx+2],dx
.off:
 or si,si
 jz .restore
 add si,4
.restore:
 inc word [st_screeny]
 pop cx
 loop .row
 ret
; SI=NUL upper-case string, AX=logical x/4, DX=logical screen y.
st_text:
 mov [st_glyphx],ax
 mov [st_glyphy],dx
.char:
 lodsb
 or al,al
 jz .out
 cmp al,32
 jb .char
 cmp al,90
 ja .char
 push si
 sub al,32
 xor ah,ah
 mov cl,4
 shl ax,cl
 mov si,st_font
 add si,ax
 mov bp,[st_glyphy]
 shl bp,1
 mov cx,8
 mov es,[st_fsi+FSI_SEG]
.row:
 lodsw
 mov bx,[ds:st_screenrows+bp]
 add bx,[st_glyphx]
 mov [es:bx],ax
 cmp byte [st_herc],0
 je .next
 mov bx,[ds:st_screenrows+400+bp]
 add bx,[st_glyphx]
 mov [es:bx],ax
.next:
 add bp,2
 loop .row
 add word [st_glyphx],2
 pop si
 jmp .char
.out: ret
st_hudpaint:
 mov byte [st_huddirty],0
 mov ax,[st_level]
 inc ax
 call st_dec2
 mov [st_hudline+6],ax
 xor ax,ax
 mov al,[st_lives]
 add al,'0'
 mov [st_hudline+17],al
 mov ax,[st_coins]
 xor dx,dx
 mov bx,100
 div bx
 mov ax,dx
 call st_dec2
 mov [st_hudline+25],ax
 mov si,st_hudline
 mov ax,2
 mov dx,4
 call st_text
 mov si,[st_name]
 mov ax,2
 mov dx,16
 call st_text
 mov si,st_controls
 mov ax,2
 mov dx,164
 call st_text
 cmp byte [st_pause],0
 je .state
 mov si,st_paused
 jmp .message
.state:
 mov si,st_blank
 cmp byte [st_state],0
 je .message
 mov si,st_clear
 cmp byte [st_state],1
 je .message
 mov si,st_over
 cmp byte [st_state],2
 je .message
 mov si,st_ending
.message:
 mov ax,2
 mov dx,24
 call st_text
 ret
st_hudline: db 'LEVEL 01  LIVES  5  COIN 00  STICKIO',0
st_controls: db 'ARROWS MOVE  Z JUMP  X RUN  P M R ESC',0
st_blank: db '                                      ',0
st_paused: db 'PAUSED - P TO CONTINUE                 ',0
st_clear: db 'COURSE CLEAR - ENTER FOR NEXT          ',0
st_over: db 'GAME OVER - ENTER TO START AGAIN       ',0
st_ending: db 'ALL 30 CLEAR! THANK YOU - ENTER        ',0
