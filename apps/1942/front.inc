; Desktop-native splash. The idle fullscreen canvas holds the decoded art.
%include "1942front.inc"
n_frontgeom:
    mov bx,[n_win]
    call OSAPI_WM_CONTENT
    mov [n_frontx],ax
    mov [n_fronty],dx
    call OSAPI_WM_DISPLAY
    mov byte [n_frontwant],1
    cmp dh,1
    ja .size
    inc byte [n_frontwant]
.size:
    mov bx,[n_win]
    call OSAPI_WM_GEOM
    mov byte [n_frontscale],2
    cmp dx,260
    jae .kind
    mov byte [n_frontscale],1
    cmp byte [n_frontwant],2
    jne .kind
    inc byte [n_frontwant]
.kind:
    mov al,[n_frontwant]
    cmp al,[n_frontkind]
    je .ready
    mov [n_frontkind],al
    mov word [n_frontready],0
    mov bx,[n_win]
    mov ax,1
    call OSAPI_WM_TIMER
.ready:
    ret

; The cache is 24 independent six-row bands (12 on compact mono CGA).
; VGA: four native planes followed by the packed fallback, 2592 bytes/band.
; Mono: one native plane, 336 bytes/band (12 trailing pad bytes). Both strides are paragraph aligned.
n_frontsegment:
    mov ax,[n_frontband]
    mov bx,21
    cmp byte [n_frontwant],1
    jne .mono
    mov bx,162
.mono:
    mul bx
    add ax,[n_canvas]
    mov es,ax
    ret

n_frontload:
    call n_frontsegment
    xor bx,bx
    mov bl,[n_frontwant]
    dec bx
    shl bx,1
    mov si,[n_frontsources+bx]
    mov bx,[n_frontband]
    shl bx,1
    shl bx,1
    add si,bx
    mov cx,[si+2]
    mov si,[si]
    mov dx,336
    cmp byte [n_frontwant],1
    jne .size
    mov dx,2592
.size:
    xor di,di
    xor bx,bx
    mov al,OSAPI_LZ_LZ4
    call OSAPI_DECOMP
    ret
n_frontsources: dw n_front_vga,n_front_herc,n_front_cga

n_frontpaint:
    call n_frontgeom
    clc
    call OSAPI_GFX_PEN
    mov al,CBLACK
    call OSAPI_SET_COLOR
    mov ax,[n_frontx]
    mov bx,[n_fronty]
    add cx,ax
    add dx,bx
    dec cx
    dec dx
    call OSAPI_GFX_FILL
    mov word [n_frontband],0
.bands:
    mov ax,[n_frontband]
    cmp ax,[n_frontready]
    jae .menu
    call n_frontblit
    inc word [n_frontband]
    jmp .bands
.menu:
    jmp n_frontmenu

n_fronttimer:
    SAVE
    cmp byte [n_abon],0
    jne .again
    cmp byte [n_infs],0
    jne .out
    mov bx,[n_win]
    call OSAPI_WM_CLIP_SET
    jc .again
    call n_frontgeom
    mov ax,24
    cmp byte [n_frontwant],3
    jne .max
    mov ax,12
.max:
    cmp [n_frontready],ax
    jae .stop
    mov ax,[n_frontready]
    mov [n_frontband],ax
    call n_frontload
    jc .stop
    call n_frontblit
    inc word [n_frontready]
    mov ax,24
    cmp byte [n_frontwant],3
    jne .count
    mov ax,12
.count:
    cmp [n_frontready],ax
    jae .stop
.again:
    mov bx,[n_win]
    mov ax,1
    call OSAPI_WM_TIMER
    jmp .out
.stop:
    mov bx,[n_win]
    xor ax,ax
    call OSAPI_WM_TIMER
.out:
    RESTORE
    ret

n_frontblit:
    call n_frontsegment
    xor si,si
    mov bx,[n_frontband]
    mov ax,bx
    shl bx,1
    add bx,ax
    shl bx,1
    mov dx,6
    mov bp,N_FRONT_W/8
    cmp byte [n_frontwant],1
    jne .height
    cmp byte [n_frontscale],2
    je .height
    shr bx,1
    shr dx,1
    shl bp,1
.height:
    add bx,[n_fronty]
    mov ax,[n_frontx]
    add ax,8
    mov cx,N_FRONT_W
    cmp byte [n_frontwant],1
    je .color
    call OSAPI_GFX_BLIT1
    ret
.color:
    mov di,N_FRONT_PLANE | 4000h
    call OSAPI_GFX_BLITP
    jnc .out
    mov si,N_FRONT_PACKED
    shl bp,1
    shl bp,1
    call OSAPI_GFX_BLIT4
.out: ret

; AX=logical half-height coordinate -> screen BX. Keeps AX intact.
n_frontrow:
    mov bx,ax
    cmp byte [n_frontscale],2
    jne .add
    shl bx,1
.add:
    add bx,[n_fronty]
    ret

n_frontmenu:
    call n_frontgeom
    clc
    call OSAPI_GFX_PEN
    mov al,CBLACK
    call OSAPI_SET_COLOR
    mov ax,76
    call n_frontrow
    mov ax,[n_frontx]
    add cx,ax
    add dx,[n_fronty]
    dec cx
    dec dx
    call OSAPI_GFX_FILL
    cmp byte [n_fronthelp],0
    jne .help
    xor di,di
    cmp byte [n_state],1
    jne .labels
    mov di,8
.labels:
    mov ax,84
    call .label
    add di,4
    mov ax,100
    call .label
    call n_frontpointer
    jmp .footer
.help:
    mov si,n_fronthelp1
    mov ax,80
    call .text
    mov si,n_fronthelp2
    mov ax,90
    call .text
    mov si,n_fronthelp3
    mov ax,100
    call .text
    mov si,n_fronthelp4
    mov ax,110
    call .text
.footer:
    mov si,n_fronthint
    cmp byte [n_state],1
    jne .hint
    mov si,n_frontresumehint
.hint:
    cmp byte [n_error],0
    je .footertext
    mov si,n_errmsg
    cmp byte [n_error],2
    jne .footertext
    mov si,n_asseterr
.footertext:
    mov ax,120
    call .text
    ret
.label:
    call n_frontrow
    push bx
    mov bx,di
    cmp byte [n_frontscale],2
    jne .small
    add bx,2
.small:
    mov si,[n_frontlabels+bx]
    pop bx
    push ds
    pop es
    mov ax,[n_frontx]
    add ax,136
    mov cx,192
    xor dx,dx
    mov dl,[n_frontscale]
    shl dx,1
    shl dx,1
    shl dx,1
    mov bp,24
    call OSAPI_GFX_BLIT1
    ret
.text:
    call n_frontrow
    mov dx,bx
    mov cx,[n_frontx]
    add cx,32
    mov ax,CWHITE
    call OSAPI_FONT_RUN
    ret
n_frontpointer:
    mov ax,82
    cmp byte [n_state],1
    je .pointer
    cmp word [n_players],2
    jne .pointer
    add ax,16
.pointer:
    call n_frontrow
    mov si,n_front_plane1
    mov dx,12
    cmp byte [n_frontscale],2
    jne .plane
    mov si,n_front_plane2
    shl dx,1
.plane:
    push ds
    pop es
    mov ax,CCYAN
    cmp byte [n_frontwant],1
    je .ink
    mov ax,CWHITE
.ink:
    call OSAPI_GFX_BLIT1_PEN
    mov ax,[n_frontx]
    add ax,104
    mov bp,3
    mov cx,24
    call OSAPI_GFX_BLIT1
    ret

n_frontlabels: dw n_front_one1,n_front_one2,n_front_two1,n_front_two2
    dw n_front_resume1,n_front_resume2,n_front_new1,n_front_new2
n_fronthint: db '1/2 SELECT   ENTER START   H CONTROLS',0
n_frontresumehint: db 'ENTER RESUME   N NEW GAME   H CONTROLS',0
n_fronthelp1: db 'ARROWS MOVE   SPACE / Z FIRE   X ROLL',0
n_fronthelp2: db 'P PAUSE   M SOUND   C CGA PALETTE',0
n_fronthelp3: db 'ESCAPE / F RETURNS TO DESKTOP',0
n_fronthelp4: db 'H / ESC CLOSE HELP   ENTER TAKE OFF',0

n_frontupdate:
    mov bx,[n_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    call n_frontmenu
.out: ret

n_click:
    SAVE
    cmp byte [n_abon],0
    je .normal
    call n_frontdismiss
    jmp .out
.normal:
    push cx
    push dx
    call n_frontgeom
    pop di
    pop cx
    sub cx,[n_frontx]
    sub di,[n_fronty]
    cmp byte [n_frontscale],2
    jne .hit
    shr di,1
.hit:
    cmp byte [n_fronthelp],0
    jne .back
    cmp cx,96
    jb .out
    cmp cx,336
    jae .out
    cmp di,80
    jb .out
    cmp di,112
    jae .out
    cmp byte [n_state],1
    jne .select
    cmp di,96
    jb .run
    mov byte [n_state],0
    jmp .back
.select:
    mov word [n_players],1
    cmp di,96
    jb .run
    inc word [n_players]
.run:
    call n_launch
    jmp .out
.back:
    mov byte [n_fronthelp],0
    call n_frontupdate
.out:
    RESTORE
    ret

n_key:
    SAVE
    cmp byte [n_abon],0
    je .normal
    call n_frontdismiss
    jmp .out
.normal:
    cmp al,13
    je .run
    cmp al,' '
    je .run
    cmp ax,4800h
    je .one
    cmp ax,5000h
    je .two
    cmp al,'1'
    je .one
    cmp al,'2'
    je .two
    cmp al,27
    je .back
    or al,20h
    cmp al,'f'
    je .run
    cmp al,'h'
    je .help
    cmp al,'n'
    jne .out
    mov byte [n_state],0
.back:
    mov byte [n_fronthelp],0
    jmp .paint
.help:
    xor byte [n_fronthelp],1
    jmp .paint
.one:
    cmp byte [n_state],1
    je .out
    mov word [n_players],1
    jmp .selection
.two:
    cmp byte [n_state],1
    je .out
    mov word [n_players],2
.selection:
    call n_frontselect
    jmp .out
.paint:
    call n_frontupdate
    jmp .out
.run:
    call n_launch
.out:
    RESTORE
    ret

n_launch:
    mov bx,[n_win]
    call OSAPI_FSX_CAPS
    test ax,(1 << FSXM_MODEX) | (1 << FSXM_CGA320)
    jz .error
    cmp byte [n_state],1
    je .resume
    call n_new
.resume:
    mov byte [n_error],0
    mov byte [n_fronthelp],0
    mov byte [n_frontkind],0
    mov ax,n_exclusive
    mov bx,[n_win]
    mov cx,FSXF_FASTTICK
    call OSAPI_FSX_RUN
    jnc .paint
.error:
    mov byte [n_error],1
.paint:
    call n_frontprepare
    mov bx,[n_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    call n_frontpaint
.out: ret

; Small kernels have no window timer. Preload on entry (no gfx lock), then
; keep the same cache for ordinary paints; FSX return must rebuild it once.
n_frontprepare:
    cmp byte [n_frontstatic],0
    je .out
    call n_frontgeom
    mov bp,24
    cmp byte [n_frontwant],3
    jne .loop
    mov bp,12
.loop:
    mov ax,[n_frontready]
    cmp ax,bp
    jae .out
    mov [n_frontband],ax
    call n_frontload
    jc .out
    inc word [n_frontready]
    jmp .loop
.out: ret

n_frontdismiss:
    mov byte [n_abon],0
    mov bx,[n_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    call n_paint
.out: ret

; Selection changes only two 24-pixel pointer cells, never the logo or text.
n_frontselect:
    cmp byte [n_fronthelp],0
    jne .out
    mov bx,[n_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    call n_frontgeom
    clc
    call OSAPI_GFX_PEN
    mov ax,110
    call n_frontrow
    mov dx,bx
    dec dx
    mov ax,82
    call n_frontrow
    mov ax,[n_frontx]
    add ax,104
    mov cx,ax
    add cx,23
    call OSAPI_GFX_FILL
    call n_frontpointer
.out: ret
