; Optional 8kHz one-shots, played directly by the Sound Blaster's DMA engine.
; One page-safe 13KB claim: three immutable rings prepared once at load.
n_pcm_open:
    call n_pushall
    cmp word [n_pcmseg],0
    jne .out
    call OSAPI_SND_CAPS
    test ax,SND_CAP_PCM_BG
    jz .out
    or bl,bl
    jz .out
    mov byte [n_pcmflags],SND_OPENF_RING + SND_OPENF_EXT
    test ax,SND_CAP_EXTBLK
    jz .claim
    or byte [n_pcmflags],3 << SND_OPENF_BLKSH
.claim:
    mov ax,13
    mov cx,13
    call OSAPI_MEM_CLAIM_DMA_HI
    jc .out
    mov [n_pcmseg],dx
    ; Optional, and fetched inside the bracket: ask first, so a busy heap is
    ; FM effects as it always was and never op_fetch's toast (n_fsxroom).
    call op_clbget
    jc .bad
    mov al,N_P_SFX
    call op_row
    call op_lazykb
    jc .bad
    xchg ax,bx
    call OSAPI_MEM_AVAIL
    cmp ax,bx
    jb .bad
    ; The bank is a part of 1942.O88 (N_P_SFX), built with the package, so the
    ; header's lengths are the ones the expansion below is written for and
    ; there is no stale pair to check for. A refused fetch has toasted why;
    ; the game goes on with FM effects, as with no card.
    mov al,N_P_SFX
    call n_readbank
    jc .bad
    push ds
    push es
    mov es,[n_pcmseg]
    pop ds
    xor si,si
    xor di,di
    mov cx,8208/2
    rep movsw
    pop ds
    mov al,N_P_SFX
    call op_drop
    ; Expand in place without overwriting unread source bytes. Source bank:
    ; header, shot, explosion, loss. Move loss first, then explosion, then shot.
    push ds
    mov ax,es
    mov ds,ax
    mov si,4112
    mov di,8224
    mov cx,2048
    rep movsw
    mov si,1040
    mov di,4112
    mov cx,1536
    rep movsw
    mov si,16
    xor di,di
    mov cx,512
    rep movsw
    mov ax,8080h
    mov cx,1536
    rep stosw
    mov di,7184
    mov cx,512
    rep stosw
    pop ds
    jmp .out
.bad:
    call n_pcm_close
.out:
    jmp n_popall

; Cancel hardware before overwriting or releasing its ring. Pending stays.
n_pcm_halt:
    SAVE
    cmp byte [n_pcmactive],0
    je .out
    mov ah,[n_pcmhandle]
    mov al,2
    call OSAPI_SND_STREAM
    mov byte [n_pcmactive],0
.out:
    mov byte [n_pcmpri],0
    RESTORE
    ret

n_pcm_close:
    call n_pushall
    call n_pcm_halt
    mov dx,[n_pcmseg]
    or dx,dx
    jz .out
    call OSAPI_MEM_FREE
    mov word [n_pcmseg],0
.out:
    mov byte [n_pcmpending],0
    jmp n_popall

n_pcm_tick:
    cmp byte [n_pcmactive],0
    je .pending
    mov al,SND_V_PPOS
    mov ah,[n_pcmhandle]
    call OSAPI_SND_STREAM
    jc .ended
    cmp ax,SND_ST_PLAYING
    jne .ended
    cmp dx,[n_pcmlen]
    jb .pending
.ended:
    call n_pcm_halt
.pending:
    xor bx,bx
    mov bl,[n_pcmpending]
    or bx,bx
    jz .out
    mov bp,bx
    call n_pcm_halt
    mov byte [n_pcmpending],0
    dec bx
    shl bx,1
    mov si,[n_pcmoffset+bx]
    mov cx,[n_pcmlength+bx]
    mov [n_pcmlen],cx
    cmp cx,2048
    jae .length
    mov cx,2048                 ; legacy driver's minimum initial valid data
.length:
    mov di,[n_pcmseg]
    mov es,di
    mov bx,si
    mov [es:bx+4096+SND_EXT_TOTAL],cx
    mov word [es:bx+4096+SND_EXT_CONS],0
    mov dx,8000
    mov ah,[n_pcmflags]
    xor al,al
    call OSAPI_SND_STREAM
    jc .failed
    mov [n_pcmhandle],ah
    mov byte [n_pcmactive],1
    mov ax,bp
    mov [n_pcmpri],al
.out:
    ret
.failed:
    ; Unsupported DSP, IRQ or stream: stop trying until the next entry.
    call n_pcm_close
    mov ax,bp
    add ax,17
    call n_effect
    ret

n_pcmoffset: dw 0,4112,8224
n_pcmlength: dw 1024,3072,4096
