; Boratype optional local BTV1 PCM pack + phase-preserving game-kit pause.
; No ports, interrupts, blocking SND_PLAY, worker, or file I/O here.
; Source is a PINNED 28KB claim (C owns its lifetime); driver grant 4KB.
; PCM is unsigned 8-bit mono. 8kHz default, 4..11.025kHz accepted.
section .text

_bt_vcaps:
    call OSAPI_SND_CAPS
    and ax,SND_CAP_PCM_BG
    jz .out
    or bl,bl                      ; honour the OS speaker-only output choice
    jnz .out
    xor ax,ax
.out:
    ret
_bt_vset:
    mov bx,sp
    mov ax,[ss:bx+2]
    mov [btv_seg],ax
    mov byte [btv_text],0
    ret
_bt_vcaption:
    mov ax,btv_text
    ret
_bt_vmove:
    mov bx,sp
    mov ax,[ss:bx+2]
    mov [btv_seg],ax
    ret
; int bt_vvalidate(seg,len). Full structural + additive integrity check.
; Header16: BTV1,count:u8,format=0,rate:u16,total:u16,sum:u16,reserved=0:u32.
; Row56: id:u8,tag:u8,priority:u8,expression:u8,off:u16,len:u16,caption[48].
_bt_vvalidate:
    push bp
    mov bp,sp
    push es
    mov es,[bp+4]
    mov cx,[bp+6]
    xor ax,ax
    cmp cx,74
    jb .out
    cmp cx,28672
    ja .out
    cmp word [es:0],'BT'
    jne .out
    cmp word [es:2],'V1'
    jne .out
    cmp [es:8],cx
    jne .out
    cmp byte [es:5],0
    jne .out
    cmp word [es:6],4000
    jb .out
    cmp word [es:6],11025
    ja .out
    cmp word [es:12],0
    jne .out
    cmp word [es:14],0
    jne .out
    xor bx,bx
    mov bl,[es:4]
    cmp bx,1
    jb .out
    cmp bx,12
    ja .out
    mov ax,56
    mul bx
    add ax,16
    mov [btv_floor],ax
    cmp ax,cx
    jae .bad
    xor si,si
    xor dx,dx
.sum:
    cmp si,10
    je .skip
    cmp si,11
    je .skip
    xor ax,ax
    mov al,[es:si]
    add dx,ax
.skip:
    inc si
    loop .sum
    cmp dx,[es:10]
    jne .bad
    mov si,16
    mov cl,[es:4]
    xor ch,ch
    xor dx,dx                     ; ID bit mask: no duplicates
.row:
    push cx
    xor bx,bx
    mov bl,[es:si]
    cmp bx,12
    jae .rowbad
    mov cx,bx
    mov ax,1
    shl ax,cl
    test dx,ax
    jnz .rowbad
    or dx,ax
    cmp byte [es:si+1],1
    jb .rowbad
    cmp byte [es:si+1],10
    ja .rowbad
    cmp byte [es:si+2],3
    ja .rowbad
    cmp byte [es:si+3],6
    ja .rowbad
    mov ax,[es:si+6]
    cmp ax,2048
    jb .rowbad
    cmp ax,16384
    ja .rowbad
    test ax,2047
    jnz .rowbad                    ; exact driver half end, silence-padded
    mov bx,[es:si+4]
    cmp bx,[btv_floor]
    jne .rowbad                    ; contiguous, ordered, nonoverlapping
    add ax,bx
    jc .rowbad
    cmp ax,[es:8]
    ja .rowbad
    mov [btv_floor],ax
    lea di,[si+8]
    mov cx,48
.text:
    mov al,[es:di]
    or al,al
    jz .textok
    cmp al,32
    jb .rowbad
    cmp al,126
    ja .rowbad
    inc di
    loop .text
    jmp short .rowbad
.textok:
    cmp byte [es:si+8],0
    je .rowbad
    pop cx
    add si,56
    dec cx
    jz .rowsdone
    jmp .row
.rowsdone:
    mov ax,[btv_floor]
    cmp ax,[es:8]
    jne .bad
    mov ax,1
.out:
    pop es
    pop bp
    ret
.rowbad:
    pop cx
.bad:
    xor ax,ax
    jmp short .out

; bt_vplay(tag): no overlap/queue. Grant and stage max4KB, then return.
_bt_vplay:
    push bp
    mov bp,sp
    push es
    cmp byte [btv_live],0
    jne .no
    cmp word [btv_seg],0
    je .no
    call _bt_vcaps
    or ax,ax
    jz .no
    mov es,[btv_seg]
    mov al,[bp+4]
    mov cl,[es:4]
    xor ch,ch
    mov si,16
.find:
    cmp [es:si+1],al
    je .found
    add si,56
    loop .find
.no:
    xor ax,ax
    jmp .out
.found:
    mov ax,[es:si+4]
    mov [btv_source],ax
    mov ax,[es:si+6]
    mov [btv_length],ax
    mov ax,[es:6]
    mov [btv_rate],ax
    push ds
    push es
    pop ds
    pop es                        ; DS=pack ES=package, copy caption locally
    add si,8
    mov di,btv_text
    mov cx,48
    cld
    rep movsb
    push es
    pop ds
    mov ax,7
    mov cx,4096
    call OSAPI_SND_STREAM
    or ax,ax
    jnz .no
    mov [btv_grant],si
    mov byte [btv_granted],1
    mov ax,[btv_length]
    cmp ax,4096
    jbe .initial
    mov ax,4096
.initial:
    mov [btv_total],ax
    mov cx,ax
    mov di,[btv_grant]
    mov si,[btv_source]
    push ds
    mov ds,[btv_seg]
    mov ax,6
    call OSAPI_SND_STREAM
    pop ds
    or ax,ax
    jnz .free
    mov si,[btv_grant]
    mov cx,[btv_total]
    mov dx,[btv_rate]
    mov ax,SND_OPENF_RING << 8
    call OSAPI_SND_STREAM
    or al,al
    jnz .free
    mov [btv_handle],ah
    mov byte [btv_live],1
    mov ax,1
    jmp short .out
.free:
    call btv_release
    jmp .no
.out:
    pop es
    pop bp
    ret

; bt_vtick(): poll + at most2KB stage per OS tick. No disk, no busy wait.
_bt_vtick:
    push es
    cmp byte [btv_live],0
    je .no
    mov ah,[btv_handle]
    mov al,3
    call OSAPI_SND_STREAM
    jc .stop
    cmp ax,SND_ST_ENDED
    jae .stop
    mov [btv_state],ax
    cmp dx,[btv_length]
    jae .stop
    mov ax,[btv_total]
    cmp ax,[btv_length]
    jae .yes
    sub ax,dx                     ; queued bytes <=4096
    cmp ax,4096
    ja .stop
    mov cx,4096
    sub cx,ax
    cmp cx,2048
    jb .yes                       ; one whole half, no fragmented work
    mov cx,[btv_length]
    sub cx,[btv_total]
    cmp cx,2048
    jbe .stage
    mov cx,2048
.stage:
    mov [btv_chunk],cx
    mov si,[btv_source]
    add si,[btv_total]
    mov di,[btv_total]
    and di,4095
    add di,[btv_grant]
    push ds
    mov ds,[btv_seg]
    mov ax,6
    call OSAPI_SND_STREAM
    pop ds
    or ax,ax
    jnz .stop
    mov cx,[btv_chunk]
    add [btv_total],cx             ; publish AFTER all samples are staged
    mov cx,[btv_total]
    mov ah,[btv_handle]
    mov al,1
    call OSAPI_SND_STREAM
    jc .stop
.yes:
    mov ax,1
    pop es
    ret
.stop:
    call _bt_vstop
.no:
    xor ax,ax
    pop es
    ret
_bt_vstop:
    cmp byte [btv_live],0
    je btv_release
    mov byte [btv_live],0
    mov ah,[btv_handle]
    mov al,2
    call OSAPI_SND_STREAM
btv_release:
    cmp byte [btv_granted],0
    je .out
    mov byte [btv_granted],0
    mov si,[btv_grant]
    mov ax,0107h
    call OSAPI_SND_STREAM
.out:
    ret

; Preserve phase while releasing FM channels at focus loss/pause/speech.
; Save the sequencer's per-voice stream/duration state, reacquire patches
; on resume, then restore only the first12 bytes (the new channels stay).
_bt_mhold:
    push bp
    mov bp,sp
    push es
    mov ax,[bp+4]
    or ax,ax
    jz .resume
    cmp byte [btm_held],0
    jne .out
    mov byte [btm_held],1
    mov ax,[gkm_cur]
    mov [btm_song],ax
    mov al,[gkm_loop]
    mov [btm_loop],al
    push ds
    pop es
    mov si,gkm_v
    mov di,btm_voices
    mov cx,3*GKM_VSZ
    cld
    rep movsb
    call gkm_fxend
    call _gkm_stop
    jmp short .out
.resume:
    cmp byte [btm_held],0
    je .out
    mov byte [btm_held],0
    mov ax,[btm_song]
    or ax,ax
    jz .out
    dec ax
    xor bx,bx
    mov bl,[btm_loop]
    push bx
    push ax
    call _gkm_play
    add sp,4
    cmp word [gkm_cur],0
    je .out                       ; refused: do not resurrect a lost claim
    push ds
    pop es
    mov si,btm_voices
    mov di,gkm_v
    mov bx,3
.copy:
    mov cx,12
    rep movsb
    add si,2
    add di,2                      ; newly granted FM channel IDs retained
    dec bx
    jnz .copy
.out:
    cld
    pop es
    pop bp
    ret
section .bss
btv_seg: resw 1
btv_floor: resw 1
btv_source: resw 1
btv_length: resw 1
btv_rate: resw 1
btv_grant: resw 1
btv_total: resw 1
btv_chunk: resw 1
btv_state: resw 1
btv_handle: resb 1
btv_live: resb 1
btv_granted: resb 1
btv_text: resb 48
btm_held: resb 1
btm_song: resw 1
btm_loop: resb 1
btm_voices: resb 3*GKM_VSZ
section .text
