; Stickio: original XT platformer. SPEC.md 103. No 80186 instructions.
%include "os88api.inc"
OS88_HEADER 'Stickio', st_entry, 1, OS88_STACK_DEFAULT
OS88_ICON16
 dw 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
 dw 03c0h,0420h,0420h,03c0h,0180h,0180h,07e0h,0990h
 dw 1188h,2184h,0180h,0240h,0240h,0420h,0810h,1818h
OS88_ICON16_END
%macro SAVE 0
 push ax
 push bx
 push cx
 push dx
 push si
 push di
 push bp
 push es
%endmacro
%macro RESTORE 0
 pop es
 pop bp
 pop di
 pop si
 pop dx
 pop cx
 pop bx
 pop ax
%endmacro
st_entry:
 mov si,st_tpl
 call OSAPI_WM_CREATE
 jc .out
 mov [st_win],bx
 OS88_REGION_MOVABLE
 OS88_ALTENTER_ARM
 mov si,st_pref
 call OSAPI_WM_PREFER
 mov si,st_about
 call OSAPI_ABOUT_SET
 mov byte [st_lives],5
.out: ret
st_paint:
 SAVE
 mov bx,[st_win]
 call OSAPI_WM_CONTENT
 mov cx,ax
 add cx,8
 add dx,8
 mov si,st_title
 mov ax,(CWHITE << 8)|CBLACK
 call OSAPI_FONT_RUN
 add dx,14
 mov si,st_hint1
 call OSAPI_FONT_RUN
 add dx,12
 mov si,st_hint2
 call OSAPI_FONT_RUN
 add dx,12
 mov si,st_hint3
 call OSAPI_FONT_RUN
 add dx,14
 mov ax,[st_level]
 inc ax
 call st_dec2
 mov [st_select+6],ax
 mov si,st_select
 mov ax,(CWHITE << 8)|CBLACK
 call OSAPI_FONT_RUN
 add dx,12
 mov bx,[st_level]
 shl bx,1
 mov bx,[st_levels+bx]
 mov si,[bx+6]
 call OSAPI_FONT_RUN
 add dx,16
 mov si,st_hint4
 call OSAPI_FONT_RUN
 RESTORE
 ret
st_about:
 SAVE
 mov bx,[st_win]
 mov si,st_credits
 call os88ui_about
 RESTORE
 ret
st_click:
 ret
st_key:
 SAVE
 cmp ax,KEY_ALTENTER
 je .go
 cmp ah,KSC_ENTER
 je .go
 cmp al,'f'
 je .go
 cmp ah,KSC_LEFT
 jne .right
 cmp word [st_level],0
 je .out
 dec word [st_level]
 jmp .paint
.right:
 cmp ah,KSC_RIGHT
 jne .out
 cmp word [st_level],29
 jae .out
 inc word [st_level]
.paint:
 call st_paint
 jmp .out
.go:
 mov ax,st_main
 mov bx,[st_win]
 mov byte [st_fallback],0
 mov cx,FSXF_RATE
 mov dx,21845
 mov di,st_irq
 call OSAPI_FSX_RUN
 jnc .out
 mov byte [st_fallback],1
 mov ax,st_main
 mov bx,[st_win]
 mov cx,FSXF_FASTTICK
 call OSAPI_FSX_RUN
.out:
 RESTORE
 ret
st_main:
 mov byte [st_fs],0
 mov bx,[st_win]
 call OSAPI_FSX_CAPS
 mov byte [st_herc],0
 test ax,1 << FSXM_CGA320
 jnz .cga
 test ax,1 << FSXM_HERC
 jz .out
 mov byte [st_herc],1
 mov al,FSXM_HERC
 jmp .mode
.cga:
 mov al,FSXM_CGA320
.mode:
 push ds
 pop es
 mov di,st_fsi
 call OSAPI_FSX_MODE
 jc .out
 mov byte [st_fs],1
 OS88_ALTENTER_SEED
 cmp byte [st_herc],0
 jne .palette
 ; BIOS palette API also works on VGA/EGA emulating CGA; white is index 3.
 mov ax,0b00h
 mov bh,1
 mov bl,1
 int 10h
 mov ax,0b00h
 xor bh,bh
 mov bl,16
 int 10h
.palette:
 call st_video_init
 mov word [st_score],0
 mov word [st_coins],0
 mov word [st_checkpoint],32
 mov byte [st_lives],5
 mov byte [st_state],0
 mov byte [st_pause],0
 mov byte [st_jumpheld],1
 mov byte [st_keysold],0
 call st_load
 call st_audio_open
 call st_clock
 mov [st_clocklast],ax
.loop:
 call st_input
 cmp byte [st_quit],0
 jne .exit
 call st_clock
 mov bx,ax
 sub ax,[st_clocklast]
 jz .wait
 mov [st_clocklast],bx
 cmp ax,4
 jbe .steps
 mov ax,4
.steps:
 mov cx,ax
.step:
 push cx
 cmp byte [st_pause],0
 jne .nostep
 cmp byte [st_state],0
 jne .nostep
 call st_step
.nostep:
 call st_audio_tick
 pop cx
 loop .step
 call st_render
st_presented equ $
 inc word [st_frames]
 jmp .loop
.wait:
 mov al,FSXW_FRAME
 call OSAPI_FSX_WAIT
 jmp .loop
.exit:
 call st_audio_close
 mov byte [st_fs],0
.out: ret
st_irq:
 add word [cs:st_subs],ax
 ret
st_clock:
 cmp byte [st_fallback],0
 jne .ticks
 mov ax,[st_subs]
 ret
.ticks:
 call OSAPI_GET_TICKS
 mov dx,ax
 shl ax,1
 add ax,dx
 ret
st_input:
 call os88alt_edge
 jc .quit
 xor bx,bx
 %macro KEY 2
 mov al,%1
 call OSAPI_KEY_DOWN
 jnc %%up
 or bl,%2
 %%up:
 %endmacro
 KEY KSC_LEFT,1
 KEY 1eh,1
 KEY KSC_RIGHT,2
 KEY 20h,2
 KEY KSC_SPACE,4
 KEY 2ch,4
 KEY 2ah,8
 KEY 36h,8
 KEY 2dh,8
 mov [st_keys],bl
 KEY 19h,16
 KEY 32h,32
 KEY 13h,64
 KEY KSC_ENTER,128
 mov al,bl
 xor al,[st_keysold]
 and al,bl
 mov [st_keysold],bl
 mov [st_edges],al
 call st_buffered
 mov al,[st_edges]
 test al,16
 jz .mute
 xor byte [st_pause],1
 call st_audio_silence
 mov byte [st_huddirty],1
.mute:
 test byte [st_edges],32
 jz .retry
 xor byte [st_mute],1
 call st_audio_silence
 mov byte [st_huddirty],1
.retry:
 test byte [st_edges],64
 jz .enter
 cmp byte [st_state],0
 jne .enter
 call st_respawn
.enter:
 test byte [st_edges],128
 jz .esc
 cmp byte [st_state],0
 je .esc
 cmp byte [st_state],1
 jne .new
 cmp word [st_level],29
 jae .new
 inc word [st_level]
 mov word [st_checkpoint],32
 call st_load
 jmp .esc
.new:
 mov word [st_level],0
 mov byte [st_lives],5
 mov word [st_score],0
 mov word [st_coins],0
 mov word [st_checkpoint],32
 call st_load
.esc:
 mov al,KSC_ESC
 call OSAPI_KEY_DOWN
 jc .quit
 ret
.quit:
 mov byte [st_quit],1
 ret
 ; BIOS keeps taps that occur between two renders. Held movement stays on the map.
st_buffered:
 SAVE
.poll:
 mov ah,1
 int 16h
 jz .out
 xor ah,ah
 int 16h
 cmp ah,KSC_ESC
 jne .enter
 mov byte [st_quit],1
 jmp .poll
.enter:
 cmp ah,KSC_ENTER
 jne .letter
 mov bl,128
 jmp .command
.letter:
 or al,20h
 cmp al,'p'
 jne .mute
 mov bl,16
 jmp .command
.mute:
 cmp al,'m'
 jne .retry
 mov bl,32
 jmp .command
.retry:
 cmp al,'r'
 jne .jump
 mov bl,64
 jmp .command
.jump:
 cmp al,'z'
 je .jumpkey
 cmp al,' '
 jne .poll
.jumpkey:
 test byte [st_keys],4
 jnz .poll
 mov byte [st_jumpbuf],5
 jmp .poll
.command:
 test [st_keysold],bl
 jnz .poll
 or [st_edges],bl
 jmp .poll
.out:
 RESTORE
 ret
st_dec2:
 push bx
 push dx
 xor dx,dx
 mov bx,10
 div bx
 add al,'0'
 add dl,'0'
 mov ah,dl
 pop dx
 pop bx
 ret
st_tpl: dw 48,32,340,166,st_title,st_paint,st_key,st_click
OS88_PREFER st_pref,340,166,340,166,340,166
st_title: db 'Stickio',0
st_hint1: db 'A BLACK AND WHITE PLATFORM ADVENTURE',0
st_hint2: db '30 COURSES - SIX WORLDS',0
st_hint3: db 'LEFT / RIGHT SELECT   ENTER TO PLAY',0
st_select: db 'LEVEL 01',0
st_hint4: db 'ARROWS MOVE  Z JUMP  X RUN  P PAUSE',0
st_credits: dw st_title,st_credit1,st_credit2,st_credit3,0
st_credit1: db 'Original Stickio art, levels and score',0
st_credit2: db 'Articulated animation; native 8086 engine',0
st_credit3: db 'PC speaker / AdLib / Sound Blaster',0
%include "game.inc"
%include "video.inc"
%include "audio.inc"
%include "assets.inc"
%define OS88UI_ABOUT
%define OS88UI_NOBTN
%include "os88ui.inc"
%include "os88alt.inc"
%assign ST_BSS 0
%macro VAR 2
 %1 equ os88_image_end + ST_BSS
 %assign ST_BSS ST_BSS + %2
%endmacro
VAR st_win,2
VAR st_level,2
VAR st_fs,1
VAR st_herc,1
VAR st_fsi,FSI_SIZE
VAR st_fallback,1
VAR st_subs,2
VAR st_last,2
VAR st_clocklast,2
VAR st_frames,2
VAR st_quit,1
VAR st_keys,1
VAR st_keysold,1
VAR st_edges,1
VAR st_jumpheld,1
VAR st_jumpbuf,1
VAR st_coyote,1
VAR st_ground,1
VAR st_land,1
VAR st_invuln,1
VAR st_pause,1
VAR st_state,1
VAR st_mute,1
VAR st_lives,1
VAR st_score,2
VAR st_coins,2
VAR st_checkpoint,2
VAR st_width,2
VAR st_name,2
VAR st_world,1
VAR st_x,2
VAR st_frac,2
VAR st_y,2
VAR st_vx,2
VAR st_vy,2
VAR st_oldy,2
VAR st_facing,1
VAR st_anim,1
VAR st_gait,2
VAR st_cam,2
VAR st_drawcam,2
VAR st_mapdirty,1
VAR st_huddirty,1
VAR st_map,160*8
VAR st_enemies,20*10
VAR st_ne,2
VAR st_ephase,1
VAR st_cur,2
VAR st_bg,80*128
VAR st_cache,80*8
VAR st_groupcache,20*8*2
VAR st_groupcol,2
VAR st_part,2
VAR st_rows,128*2
VAR st_screenrows,200*4
VAR st_oldboxes,7*4
VAR st_nboxes,2
VAR st_screenx,2
VAR st_screeny,2
VAR st_sprite,2
VAR st_col,2
VAR st_mptr,2
VAR st_phase,2
VAR st_crow,2
VAR st_cp,2
VAR st_bgp,2
VAR st_vp,2
VAR st_glyphx,2
VAR st_glyphy,2
VAR st_hud,64
VAR st_caps,2
VAR st_pcm_source,2
VAR st_fm,1
VAR st_pcm,1
VAR st_grant,2
VAR st_handle,1
VAR st_stream,1
VAR st_musicpos,2
VAR st_musicwait,1
VAR st_musicptr,2
VAR st_fx,1
VAR st_fxwait,1
VAR st_fxpos,1
VAR st_audio_last,2
OS88_BSS ST_BSS
OS88_IMAGE_END
