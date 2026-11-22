; BTP1: four unsigned music samples or four signed FX contributions per index.
; The 4KB SOUND.DRV grant and its modulo-65536 publication counters follow §34.5.
section .text
_bt_pcm_caps:
    call OSAPI_SND_CAPS
    and ax,SND_CAP_PCM_BG
    or bl,bl
    jnz .out
    xor ax,ax
.out: ret

; Mix 256 samples into resident scratch. ES = package throughout. Interrupts
; may preempt a pass. A clip request is consumed with a single exchange.
btp_mix:
    push ds
    push es
    push ds
    pop es
    cld
    test byte [_bt_pcm_wanted],1
    jz .quiet
    mov ds,[_bt_pcm_music]
    mov si,[es:btp_musicpos]
    mov dx,[8]                      ; end of file = end of sole track
    mov di,btp_stage
    mov cx,64
.music:
    cmp si,dx
    jb .musicvec
    mov si,1088
.musicvec:
    lodsb
    xor ah,ah
    shl ax,1
    shl ax,1
    mov bx,ax
    mov ax,[bx+64]
    stosw
    mov ax,[bx+66]
    stosw
    loop .music
    mov [es:btp_musicpos],si
    jmp short .effect
.quiet:
    mov di,btp_stage
    mov ax,8080h
    mov cx,128
    rep stosw
.effect:
    mov ds,[es:_bt_pcm_fx]
    xor ax,ax
    xchg al,[es:_bt_pcm_clip]
    test byte [es:_bt_pcm_wanted],2
    jnz .cue
    mov word [es:btp_fxleft],0
    jmp short .done
.cue:
    or al,al
    jz .existing
    dec ax
    shl ax,1
    shl ax,1
    mov bx,ax
    mov si,[bx+12]
    mov [es:btp_fxpos],si
    mov ax,[bx+14]
    mov [es:btp_fxleft],ax
.existing:
    mov cx,[es:btp_fxleft]
    jcxz .done
    cmp cx,64
    jbe .length
    mov cx,64
.length:
    sub [es:btp_fxleft],cx
    mov si,[es:btp_fxpos]
    mov di,btp_stage
.fx:
    lodsb
    xor ah,ah
    shl ax,1
    shl ax,1
    mov bx,ax
    mov ax,[bx+64]
    add [es:di],al
    add [es:di+1],ah
    mov ax,[bx+66]
    add [es:di+2],al
    add [es:di+3],ah
    add di,4
    loop .fx
    mov [es:btp_fxpos],si
.done:
    pop es
    pop ds
    ret

; Prepare one 256-byte sector fraction and stage it in the driver's grant.
btp_stage_one:
    call btp_mix
    mov di,[btp_total]
    and di,4095
    add di,[btp_grant]
    mov si,btp_stage
    mov cx,256
    mov ax,6
    call OSAPI_SND_STREAM
    or ax,ax
    jnz .bad
    add word [btp_total],256
    clc
    ret
.bad: stc
    ret

_bt_pcm_open:
    push bp
    push es
    mov ax,7
    mov cx,4096
    call OSAPI_SND_STREAM
    or ax,ax
    jnz .refuse
    mov [btp_grant],si
    mov byte [btp_owned],1
    mov word [btp_total],0
    cmp word [btp_musicpos],1088
    jae .prime
    mov word [btp_musicpos],1088
.prime:
    call btp_stage_one
    jc .release
    cmp word [btp_total],2048
    jb .prime
    mov si,[btp_grant]
    mov cx,2048
    mov dx,8000
    mov ax,SND_OPENF_RING << 8
    call OSAPI_SND_STREAM
    or al,al
    jnz .release
    mov [btp_handle],ah
    mov byte [_bt_pcm_broken],0
    mov byte [_bt_pcm_live],1
    mov ax,1
    jmp short .out
.release:
    call _bt_pcm_close
.refuse:
    xor ax,ax
.out:
    pop es
    pop bp
    ret

; Called once per worker tick. The UI can revoke live before taking the
; grant back. A pass stages at most four blocks and publishes after writing.
_bt_pcm_feed:
    cmp byte [_bt_pcm_live],0
    je .ret
    mov byte [btp_mixing],1
    cmp byte [_bt_pcm_live],0
    je .done
    mov ah,[btp_handle]
    mov al,3
    call OSAPI_SND_STREAM
    jc .broken
    cmp ax,SND_ST_ENDED
    jae .broken
    mov bx,[btp_total]
    sub bx,dx
    cmp bx,2048
    ja .done
    mov byte [btp_blocks],4
.fill:
    cmp byte [_bt_pcm_live],0
    je .done
    call btp_stage_one
    jc .broken
    dec byte [btp_blocks]
    jnz .fill
    mov cx,[btp_total]
    mov ah,[btp_handle]
    mov al,1
    call OSAPI_SND_STREAM
    jnc .done
.broken:
    mov byte [_bt_pcm_broken],1
.done:
    mov byte [btp_mixing],0
.ret: ret

_bt_pcm_close:
    cmp byte [_bt_pcm_live],0
    je .free
    mov byte [_bt_pcm_live],0
.drain:
    cmp byte [btp_mixing],0
    jne .drain
    mov al,2
    mov ah,[btp_handle]
    call OSAPI_SND_STREAM
.free:
    cmp byte [btp_owned],0
    je .done
    mov byte [btp_owned],0
    mov si,[btp_grant]
    mov ax,0107h
    call OSAPI_SND_STREAM
.done:
    mov byte [_bt_pcm_broken],0
    mov word [btp_fxleft],0
    mov byte [_bt_pcm_clip],0
    ret

section .bss
btp_stage: resb 256
btp_total: resw 1
btp_grant: resw 1
btp_musicpos: resw 1
btp_fxleft: resw 1
btp_fxpos: resw 1
btp_handle: resb 1
btp_blocks: resb 1
btp_owned: resb 1
btp_mixing: resb 1
section .text
