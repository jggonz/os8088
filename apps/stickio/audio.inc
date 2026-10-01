; Original six-world score. OPL2 plays three voices; speaker follows melody.
; Optional SB DMA effects use prepared samples, one grant for the session.
st_audio_open:
 SAVE
 mov byte [st_fm],0
 mov byte [st_pcm],0
 mov byte [st_stream],0
 mov byte [st_musicwait],0
 mov byte [st_fxwait],0
 call OSAPI_SND_CAPS
 mov [st_caps],ax
 test ax,SND_CAP_FM
 jz .pcm
 or bl,bl
 jz .pcm
 xor cx,cx
.patch:
 mov si,st_patch
 cmp cl,1
 jne .load
 mov si,st_basspatch
.load:
 mov al,2
 call OSAPI_SND_FM
 jc .partial
 inc cl
 cmp cl,4
 jb .patch
 mov byte [st_fm],1
 jmp .pcm
.partial:
 mov al,3
 call OSAPI_SND_FM
.pcm:
 test word [st_caps],SND_CAP_PCM_BG
 jz .out
 mov ax,7
 mov cx,2048
 call OSAPI_SND_STREAM
 or ax,ax
 jnz .out
 mov [st_grant],si
 mov byte [st_pcm],1
.out:
 call OSAPI_GET_TICKS
 mov [st_audio_last],ax
 RESTORE
 ret
st_audio_silence:
 SAVE
 cmp byte [st_fm],0
 je .speaker
 xor cx,cx
.off:
 mov al,1
 call OSAPI_SND_FM
 inc cl
 cmp cl,4
 jb .off
.speaker:
 xor ax,ax
 xor cx,cx
 mov dl,40h
 call OSAPI_SND_TONE
 call st_pcm_close
 mov byte [st_musicwait],0
 mov byte [st_fxwait],0
 mov byte [st_fx],0
 RESTORE
 ret
st_audio_close:
 call st_audio_silence
 SAVE
 cmp byte [st_fm],0
 je .pcm
 mov al,3
 call OSAPI_SND_FM
.pcm:
 cmp byte [st_pcm],0
 je .out
 mov ax,0107h
 mov si,[st_grant]
 call OSAPI_SND_STREAM
 mov byte [st_pcm],0
.out:
 RESTORE
 ret
st_pcm_close:
 cmp byte [st_stream],0
 je .out
 mov al,2
 mov ah,[st_handle]
 call OSAPI_SND_STREAM
 mov byte [st_stream],0
.out: ret
; Called at simulation speed but sequenced on the system tick, never per draw.
st_audio_tick:
 SAVE
 call OSAPI_GET_TICKS
 cmp ax,[st_audio_last]
 je .out
 mov [st_audio_last],ax
 cmp byte [st_mute],0
 jne .out
 cmp byte [st_pause],0
 jne .out
 cmp byte [st_musicwait],0
 je .note
 dec byte [st_musicwait]
 jnz .effect
.note:
 mov byte [st_musicwait],3
 xor ax,ax
 mov al,[st_world]
 shl ax,1
 mov bx,ax
 mov si,[st_tunes+bx]
 mov bx,[st_musicpos]
 and bx,31
 shl bx,1
 add si,bx
 mov [st_musicptr],si
 inc word [st_musicpos]
 cmp byte [st_fm],0
 je .tone
 xor cx,cx
.voice:
 mov bx,[si]
 mov al,1
 call OSAPI_SND_FM
 or bx,bx
 jz .advance
 xor al,al
 call OSAPI_SND_FM
.advance:
 add si,64
 inc cl
 cmp cl,3
 jb .voice
 jmp .effect
.tone:
 cmp byte [st_fxwait],0
 jne .effect
 mov ax,[si]
 mov cx,3
 mov dl,40h
 call OSAPI_SND_TONE
.effect:
 cmp byte [st_fxwait],0
 je .out
 dec byte [st_fxwait]
 jnz .fxnote
 cmp byte [st_fm],0
 je .out
 mov al,1
 mov cl,3
 call OSAPI_SND_FM
 jmp .out
.fxnote:
 mov al,[st_fx]
 xor ah,ah
 dec ax
 shl ax,1
 shl ax,1
 shl ax,1
 mov bx,ax
 xor ax,ax
 mov al,[st_fxpos]
 cmp al,3
 jae .out
 shl ax,1
 add bx,ax
 mov ax,[st_fxnotes+bx]
 inc byte [st_fxpos]
 cmp byte [st_fm],0
 je .fxtone
 mov bx,ax
 mov cl,3
 mov al,1
 call OSAPI_SND_FM
 mov al,0
 call OSAPI_SND_FM
 jmp .out
.fxtone:
 mov cx,1
 mov dl,60h
 call OSAPI_SND_TONE
.out:
 RESTORE
 ret
; AL=jump/coin/hurt/stomp/clear, 1..5. Preserve caller's collision registers.
st_effect:
 SAVE
 cmp byte [st_mute],0
 jne .out
 mov [st_fx],al
 mov byte [st_fxwait],4
 mov byte [st_fxpos],0
 cmp byte [st_pcm],0
 je .out
 dec al
 xor ah,ah
 mov cl,11
 shl ax,cl
 add ax,st_pcm0
 mov [st_pcm_source],ax
 call st_pcm_close
 mov ax,6
 mov si,[st_pcm_source]
 mov di,[st_grant]
 mov cx,2048
 call OSAPI_SND_STREAM
 or ax,ax
 jnz .out
 xor ax,ax
 mov si,[st_grant]
 mov cx,2048
 mov dx,8000
 call OSAPI_SND_STREAM
 or al,al
 jnz .out
 mov [st_handle],ah
 mov byte [st_stream],1
.out:
 RESTORE
 ret
st_patch: db 21h,18h,0f2h,65h,0,21h,0,0f2h,65h,0,2
st_basspatch: db 21h,12h,0f2h,76h,0,21h,4,0f2h,76h,0,0
st_fxnotes:
 dw 350,525,700,0
 dw 1047,1319,1568,0
 dw 220,147,73,0
 dw 620,390,260,0
 dw 523,659,784,0
