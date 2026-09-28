; Adapter-native renderer. VGA latch restoration/page flipping; CGA packed runs.
; All ports and foreign-mode stores are confined to the FSX bracket (SPEC 99).
N_LIST_BYTES equ 1536
N_BACK equ 38400
N_CBUF equ 16384

n_palette:
    SAVE
    cmp byte [n_cga],0
    jne .cga
    mov dx,3c8h
    xor al,al
    out dx,al
    inc dx
    mov si,n_dac
    mov cx,N_COLORS*3
.loop:
    lodsb
    out dx,al
    loop .loop
    jmp .out
.cga:
    xor bx,bx
    mov bl,[n_profile]
    mov al,[n_cgaregs+bx]
    mov dx,3d9h
    out dx,al
.out:
    RESTORE
    ret

; Load and check the exact compiled bank before following any offset in it.
; ES=claim, SI=name, CX=expected size, BP=checksum. CF on any failure.
n_readbank:
    push cx
    push bp
    xor bx,bx
    xor dx,dx
    call OSAPI_FILE_READ
    pop bp
    pop cx
    jc .bad
    or dx,dx
    jnz .bad
    cmp ax,cx
    jne .bad
    xor bx,bx
    xor dx,dx
.sum:
    xor ax,ax
    mov al,[es:bx]
    add dx,ax
    inc bx
    loop .sum
    cmp dx,bp
    jne .bad
    clc
    ret
.bad:
    stc
    ret
n_loadgfx:
    cmp byte [n_cga],0
    jne .bank
    mov es,[n_canvas]
    mov si,n_lfile
    mov cx,N_L_SIZE
    mov bp,N_L_SUM
    call n_readbank
    jc .out
    push ds
    mov es,[n_fsi+FSI_SEG]
    mov ds,[n_canvas]
    xor si,si
    mov bx,1
.cacheplane:
    mov dx,3c4h
    mov al,2
    mov ah,bl
    out dx,ax
    mov di,57600
    mov cx,N_CACHE_BYTES
    rep movsb
    shl bl,1
    cmp bl,16
    jb .cacheplane
    pop ds
.bank:
    mov es,[n_graphics]
    mov si,n_vfile
    mov cx,N_V_SIZE
    mov bp,N_V_SUM
    cmp byte [n_cga],0
    je .read
    mov si,n_cfile
    mov cx,N_C_SIZE
    mov bp,N_C_SUM
.read:
    call n_readbank
.out:
    ret
n_lfile: db '1942L.GFX',0
n_vfile: db '1942V.GFX',0
n_cfile: db '1942C.GFX',0
n_scenefiles: dw n_seav,n_reefv,n_portv,n_seac,n_reefc,n_portc
n_scenesums: dw N_SEA_V_SUM,N_REEF_V_SUM,N_PORT_V_SUM,N_SEA_C_SUM,N_REEF_C_SUM,N_PORT_C_SUM
n_seav: db 'SEA.V42',0
n_reefv: db 'REEF.V42',0
n_portv: db 'PORT.V42',0
n_seac: db 'SEA.C42',0
n_reefc: db 'REEF.C42',0
n_portc: db 'PORT.C42',0
n_scenecheck:
    mov ax,[n_stage]
    dec ax
    shr ax,1
    shr ax,1
    xor dx,dx
    mov bx,3
    div bx
    cmp dx,[n_scene]
    je .same
    push dx
    mov bx,dx
    cmp byte [n_cga],0
    je .index
    add bx,3
.index:
    shl bx,1
    mov si,[n_scenefiles+bx]
    mov bp,[n_scenesums+bx]
    mov cx,61448
    cmp byte [n_cga],0
    je .read
    mov cx,16008
.read:
    mov es,[n_canvas]
    call n_readbank
    pop dx
    jc .bad
    mov [n_scene],dx
    cmp byte [n_cga],0
    jne .cga
    SAVE
    push ds
    mov es,[n_fsi+FSI_SEG]
    mov ds,[n_canvas]
    mov si,8
    mov bx,1
.plane:
    mov dx,3c4h
    mov al,2
    mov ah,bl
    out dx,ax
    mov di,N_BACK+8
    mov bp,240
.row:
    mov cx,32
    rep movsw
    add di,16
    dec bp
    jnz .row
    shl bl,1
    cmp bl,16
    jb .plane
    pop ds
    RESTORE
    jmp .refresh
.cga:
    push ds
    mov ds,[n_canvas]
    mov si,8
    xor di,di
    mov cx,8000
    rep movsw
    pop ds
.refresh:
    call n_refresh
.same:
    clc
    ret
.bad:
    mov byte [n_error],2
    stc
    ret

; Select this page's rectangle list. 256 records is more than all actors,
; bullets and message glyphs together, including old/new CGA footprints.
n_list:
    mov bx,[n_page]
    shl bx,1
    mov di,[n_counts+bx]
    mov bx,n_rects
    cmp word [n_page],0
    je .out
    add bx,N_LIST_BYTES
.out:
    ret
n_mark:
    SAVE
    cmp byte [n_background],0
    jne .out
    mov byte [n_rendered],1
    call n_list
    cmp di,N_LIST_BYTES
    jae .out
    add bx,di
    mov ax,[n_origin]
    mov [bx],ax
    mov ax,[n_width]
    mov [bx+2],ax
    mov ax,[n_height]
    mov [bx+4],ax
    mov bx,[n_page]
    shl bx,1
    add word [n_counts+bx],6
.out:
    RESTORE
    ret
; CX,DX position; BX,BP dimensions -> native offset/byte width/height/phase.
n_geometry:
    SAVE
    mov si,dx
    shl si,1
    mov ax,[n_yv+si]
    mov dx,cx
    mov [n_height],bp
    cmp byte [n_cga],0
    je .vga
    mov ax,[n_yc+si]
    mov si,cx
    shl si,1
    mov dx,[n_xc+si]
    mov cx,bx
    shl bx,1
    shl bx,1
    add bx,cx
    add bx,3
    shr bx,1
    shr bx,1
    push ax
    mov ax,bp
    mov cx,5
    mul cx
    add ax,5
    mov cx,6
    div cx
    mov [n_height],ax
    pop ax
    mov dx,[n_xc+si]
    jmp .common
.vga:
    add ax,8
.common:
    mov cx,dx
    and cx,3
    mov [n_phase],cx
    shr dx,1
    shr dx,1
    add ax,dx
    mov [n_origin],ax
    add bx,cx
    add bx,3
    shr bx,1
    shr bx,1
    mov [n_width],bx
    RESTORE
    ret

; SI=sprite number, CX/DX logical origin. Opaque spans are precompiled.
n_sprite:
    SAVE
    push ds
    cmp cx,256
    jae .out
    cmp dx,240
    jae .out
    mov bx,si
    shl bx,1
    mov ax,[n_cacheindex+bx]
    mov [n_cacheptr],ax
    cmp byte [n_cga],0
    jne .position
    or ax,ax
    jz .position
    and cx,0fffeh
.position:
    mov es,[n_graphics]
    mov si,[es:bx+8]
    xor bx,bx
    mov bl,[es:si]
    mov [n_compiled],bl
    and bx,127
    xor ax,ax
    mov al,[es:si+1]
    mov bp,ax
    add ax,dx
    cmp ax,240
    ja .out
    mov ax,bx
    add ax,cx
    jc .out
    cmp ax,256
    ja .out
    call n_geometry
    call n_mark
    mov [n_record],si
    cmp byte [n_cga],0
    jne .cga
    cmp word [n_cacheptr],0
    jne .cached
    mov es,[n_fsi+FSI_SEG]
    mov ds,[n_graphics]
    mov word [cs:n_plane],0
.plane:
    mov bx,[cs:n_plane]
    add bx,[cs:n_phase]
    mov di,[cs:n_origin]
    add di,[cs:n_drawbase]
    cmp bx,4
    jb .carry
    inc di
.carry:
    and bx,3
    mov cl,bl
    mov ah,1
    shl ah,cl
    mov al,2
    mov dx,3c4h
    out dx,ax
    mov [cs:n_vorigin],di
    mov bx,[cs:n_plane]
    shl bx,1
    add bx,[cs:n_record]
    mov si,[bx+2]
.run:
    lodsw
    cmp ax,0ffffh
    je .nextplane
    mov di,ax
    add di,[cs:n_vorigin]
    xor cx,cx
    lodsb
    mov cl,al
    rep movsb
    jmp .run
.nextplane:
    inc word [cs:n_plane]
    cmp word [cs:n_plane],4
    jb .plane
    jmp .out
.cached:
    mov bx,[n_cacheptr]
    add bx,[n_phase]
    mov bp,[bx]
    mov ax,[n_origin]
    add ax,[n_drawbase]
    mov [n_vorigin],ax
    mov dx,3ceh
    mov ax,4105h
    out dx,ax
    mov dx,3c4h
    mov ax,[n_fsi+FSI_SEG]
    mov es,ax
    mov ds,ax
.group:
    mov ax,[cs:bp]
    add bp,2
    or ax,ax
    jz .cachedone
    out dx,ax
    mov bx,[cs:bp]
    add bp,2
.latches:
    mov di,[cs:bp]
    add di,[cs:n_vorigin]
    mov si,[cs:bp+2]
    mov cx,[cs:bp+4]
    rep movsb
    add bp,6
    dec bx
    jnz .latches
    jmp .group
.cachedone:
    mov dx,3ceh
    mov ax,4005h
    out dx,ax
    jmp .out
.cga:
    mov bx,[n_phase]
    shl bx,1
    add bx,si
    mov si,[es:bx+2]
    mov ax,[n_origin]
    cmp byte [n_background],0
    jne .back
    add ax,N_CBUF
.back:
    mov [n_vorigin],ax
    mov es,[n_canvas]
    test byte [n_compiled],128
    jnz .compiled
    mov ds,[n_graphics]
.crun:
    lodsw
    cmp ax,0ffffh
    je .out
    mov di,ax
    add di,[cs:n_vorigin]
    lodsw                         ; AL=count, AH=masked flag
    xor cx,cx
    mov cl,al
    or ah,ah
    jnz .masked
    rep movsb
    jmp .crun
.masked:
    lodsw
    and al,[es:di]
    or al,ah
    stosb
    loop .masked
    jmp .crun
.compiled:
    mov [n_blitptr],si
    mov ax,[n_graphics]
    mov [n_blitptr+2],ax
    mov di,[n_vorigin]
    call far [n_blitptr]
    jmp .out
.out:
    pop ds
    RESTORE
    ret

n_text:
    SAVE
.char:
    lodsb
    or al,al
    jz .out
    cmp al,32
    jb .char
    cmp al,90
    ja .char
    push si
    xor ah,ah
    sub ax,32
    add ax,n_font32art
    mov si,ax
    call n_sprite
    pop si
    add cx,8
    jmp .char
.out:
    RESTORE
    ret

; Restore one native rectangle from the stage cache. SI -> offset,width,height.
n_restore:
    SAVE
    push ds
    mov di,[si]
    mov bx,[si+2]
    mov bp,[si+4]
    mov si,di
    cmp byte [n_cga],0
    jne .cga
    add si,N_BACK
    add di,[n_drawbase]
    mov dx,3ceh
    mov ax,4105h
    out dx,ax                     ; write mode 1: all four latches
    mov dx,3c4h
    mov ax,0f02h
    out dx,ax
    mov ax,[n_fsi+FSI_SEG]
    mov es,ax
    mov ds,ax
    jmp .rows
.cga:
    add di,N_CBUF
    mov ax,[n_canvas]
    mov es,ax
    mov ds,ax
.crows:
    mov cx,bx
    shr cx,1
    rep movsw
    jnc .ctail
    movsb
.ctail:
    add si,80
    sub si,bx
    add di,80
    sub di,bx
    dec bp
    jnz .crows
    jmp .restored
.rows:
    mov cx,bx
    rep movsb
    add si,80
    sub si,bx
    add di,80
    sub di,bx
    dec bp
    jnz .rows
.restored:
    pop ds
    cmp byte [n_cga],0
    jne .out
    mov dx,3ceh
    mov ax,4005h
    out dx,ax
.out:
    RESTORE
    ret
n_fullrect: dw 0,80,240
n_cfullrect: dw 0,80,200
n_hudrect: dw 8,64,16
n_chudrect: dw 0,80,14
n_clear:
    SAVE
    mov word [n_counts],0
    mov word [n_counts+2],0
    mov word [n_oldcount],0
    cmp byte [n_cga],0
    jne .cga
    mov si,n_fullrect
    mov word [n_drawbase],0
    call n_restore
    mov word [n_drawbase],19200
    call n_restore
    jmp .done
.cga:
    mov si,n_cfullrect
    call n_restore
    ; A full CGA update is needed after a scene change or menu.
    mov word [n_rects],0
    mov word [n_rects+2],80
    mov word [n_rects+4],200
    mov word [n_counts],6
.done:
    mov word [n_page],0
    mov word [n_drawbase],0
    mov byte [n_rendered],1
    RESTORE
    ret
n_erase:
    SAVE
    call n_list
    mov si,bx
    mov bp,di
    mov [n_oldcount],di
.loop:
    or bp,bp
    jz .done
    call n_restore
    add si,6
    sub bp,6
    jmp .loop
.done:
    cmp byte [n_cga],0
    jne .keep
    mov bx,[n_page]
    shl bx,1
    mov word [n_counts+bx],0
.keep:
    mov byte [n_rendered],1
    RESTORE
    ret
n_present:
    SAVE
    cmp byte [n_rendered],0
    je .out
    cmp byte [n_cga],0
    jne .cga
    mov ax,[n_page]
    call OSAPI_FSX_PAGE
    xor word [n_page],1
    mov ax,19200
    cmp word [n_page],0
    jne .base
    xor ax,ax
.base:
    mov [n_drawbase],ax
    jmp .done
.cga:
    push ds
    mov bp,[n_counts]
    mov bx,n_rects
.rect:
    or bp,bp
    jz .compact
    push bp
    push bx
    mov ax,[bx]
    xor dx,dx
    mov cx,80
    div cx
    mov di,ax
    shr ax,1
    mul cx
    test di,1
    jz .even
    add ax,2000h
.even:
    ; remainder of the original divide is recomputed without DIV.
    mov dx,[bx]
    mov si,di
    shl si,1
    mov cx,[n_yv+si]
    sub dx,cx
    mov di,ax
    add di,dx
    mov si,[bx]
    add si,N_CBUF
    mov bp,[bx+4]
    mov bx,[bx+2]
    mov es,[n_fsi+FSI_SEG]
    mov ds,[n_canvas]
.row:
    mov cx,bx
    shr cx,1
    rep movsw
    jnc .tail
    movsb
.tail:
    add si,80
    sub si,bx
    sub di,bx
    xor di,2000h
    test di,2000h
    jnz .bank
    add di,80
.bank:
    dec bp
    jnz .row
    push cs
    pop ds
    pop bx
    pop bp
    add bx,6
    sub bp,6
    jmp .rect
.compact:
    pop ds
    push ds
    pop es
    mov si,n_rects
    add si,[n_oldcount]
    mov di,n_rects
    mov cx,[n_counts]
    sub cx,[n_oldcount]
    mov [n_counts],cx
    shr cx,1
    rep movsw
    mov word [n_oldcount],0
.done:
    mov byte [n_rendered],0
.out:
    RESTORE
    ret

; Paint the HUD in the cache only when it changes. Clear the exact native band.
n_hudbegin:
    SAVE
    mov byte [n_background],1
    mov ax,[n_drawbase]
    mov [n_savedbase],ax
    mov word [n_drawbase],N_BACK
    cmp byte [n_cga],0
    jne .cga
    mov es,[n_fsi+FSI_SEG]
    mov dx,3c4h
    mov ax,0f02h
    out dx,ax
    mov di,N_BACK+8
    mov bp,16
    mov ax,0101h
.vrow:
    mov cx,32
    rep stosw
    add di,16
    dec bp
    jnz .vrow
    jmp .out
.cga:
    mov es,[n_canvas]
    xor di,di
    xor ax,ax
    mov cx,560
    rep stosw
.out:
    RESTORE
    ret
n_hudend:
    mov byte [n_background],0
    mov ax,[n_savedbase]
    mov [n_drawbase],ax
    ret
n_hudcopy:
    SAVE
    mov si,n_hudrect
    cmp byte [n_cga],0
    je .copy
    mov si,n_chudrect
.copy:
    call n_restore
    mov ax,[si]
    mov [n_origin],ax
    mov ax,[si+2]
    mov [n_width],ax
    mov ax,[si+4]
    mov [n_height],ax
    call n_mark
    RESTORE
    ret
