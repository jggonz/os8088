; Intro animation uses prepared masks/native sprites, never the scene scaler.
; Text remains mirrored in gr_scene; these decorations are separate layers.
gr_frontsprites:
    call gr_frontprepare
    ; Planar blits cannot use an armed region. Disarm only after proving the
    ; entire scene drawable under this lock, and restore before returning.
    cmp byte [gr_fs], 0
    jne .draw
    mov ax, [gr_ox]
    mov bx, [gr_oy]
    mov cx, [gr_sx]
    mov dx, [gr_sy]
    mov ch, cl
    xor cl, cl
    push ax
    mov ax, 128
    mul dx
    mov dx, ax
    pop ax
    add cx, ax
    dec cx
    add dx, bx
    dec dx
    call OSAPI_WM_CLIP_TEST
    jc .draw
    call OSAPI_WM_CLIP_CLEAR
.draw:
    xor ax, ax
    mov al, [gr_animphase]
    mov cx, 448
    mul cx
    add ax, gr_front_lights
    mov si, ax
    xor ax, ax
    xor bx, bx
    mov cx, 256
    mov dx, 3
    call gr_lightblit
    add si, 96
    mov bx, 125
    call gr_lightblit
    xor bx, bx
    mov bl, [gr_animphase]
    shl bx, 1
    shl bx, 1
    mov di, [gr_lightruns+bx]
    push bx
    xor ax, ax
    call gr_lightside
    pop bx
    mov di, [gr_lightruns+bx+2]
    mov ax, 248
    call gr_lightside
    mov ax, 80
    xor bx, bx
    mov bl, [gr_pose]
    call gr_frontape
    mov ax, 160
    xor bx, 1
    call gr_frontape
    cmp byte [gr_fs], 0
    jne .out
    mov bx, [gr_win]
    call OSAPI_WM_CLIP_SET
.out:
    ret

; Only rows touched by the old or new lights need repainting. The generator
; coalesces neighboring old/new crosses, excluding the blank gaps and corners.
gr_lightside:
    xor dx, dx
    mov dl, [di+3]
    or dx, dx
    jz .out
    mov si, [di]
    add si, gr_front_lights
    xor bx, bx
    mov bl, [di+2]
    add di, 4
    mov cx, 8
    call gr_lightblit
    jmp gr_lightside
.out:
    ret

; Logical rectangle AX/BX/CX/DX and SI=packed 1bpp mask.
gr_lightblit:
    SAVE
    push ds
    pop es
    mov [gr_rx], ax
    mov [gr_ry], bx
    mov [gr_rw], cx
    mov [gr_rh], dx
    shr cx, 1
    shr cx, 1
    shr cx, 1
    mov [gr_frontcols], cx
    mov ax, [gr_sx]
    cmp byte [gr_cga], 0
    je .stride
    mov ax, 2
.stride:
    mov [gr_cellbytes], ax
    mul cx
    mov [gr_stride], ax
    mov ax, si
    sub ax, gr_front_lights
    mul word [gr_cacheratio]
    add ax, gr_lightcache
    mov [gr_frontsource], ax
    cmp byte [gr_depth], 4
    jne .coords
    cmp byte [gr_vga], 0
    jne .coords
    mov ax, (CBLUE << 8) | CRED
    call OSAPI_GFX_BLIT1_PEN
.coords:
    call gr_frontcoords
    cmp byte [gr_vga], 0
    jne .vga
    cmp byte [gr_cga], 0
    jne .cga
    call OSAPI_GFX_BLIT1
    jmp short .done
.cga:
    call gr_cgablit
    jmp short .done
.vga:
    mov byte [gr_frontplane], 6
    call gr_frontvgaplane
    ; Clear the two planes outside ink 6 with an empty band.
    push ds
    pop es
    mov di, gr_band
    mov ax, [gr_stride]
    mul word [gr_rh]
    mul word [gr_sy]
    mov cx, ax
    xor ax, ax
    rep stosb
    call gr_frontcoords
    mov si, gr_band
    mov byte [gr_frontplane], 9
    call gr_frontvgaplane
.done:
    RESTORE
    ret

gr_lightcompose:
    push ds
    pop es
    mov bp, [gr_rh]
.row:
    push di
    mov cx, [gr_frontcols]
.byte:
    lodsb
    cmp word [gr_cellbytes], 1
    je .single
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov ax, [gr_textdouble+bx]
    cmp byte [gr_cga], 0
    je .double
    and ax, 0aaaah            ; palette 2 for the red marquee
.double:
    stosw
    jmp short .next
.single:
    stosb
.next:
    loop .byte
    pop bx
    push si
    mov dx, [gr_sy]
    dec dx
    jz .nextrow
.repeat:
    mov si, bx
    mov cx, [gr_stride]
    rep movsb
    dec dx
    jnz .repeat
.nextrow:
    pop si
    dec bp
    jnz .row
    ret

; Prepare scaled masks once per surface geometry, before music/animation.
; The 15 KB cache replaces repeated expansion and scanline duplication.
gr_frontprepare:
    mov ax, [gr_sx]
    mov ah, [gr_sy]
    cmp byte [gr_cga], 0
    je .vga
    or al, 10h
.vga:
    cmp byte [gr_vga], 0
    je .key
    or al, 20h
.key:
    cmp ax, [gr_cachekey]
    je .out
    mov [gr_cachekey], ax
    mov ax, [gr_sx]
    cmp byte [gr_cga], 0
    je .ratio
    mov ax, 2
.ratio:
    mov [gr_cellbytes], ax
    mul word [gr_sy]
    mov [gr_cacheratio], ax
    mov si, gr_front_lights
    mov di, gr_lightcache
    xor bp, bp
.part:
    push bp
    mov word [gr_frontcols], 32
    mov word [gr_rh], 3
    test bp, 2
    jz .size
    mov word [gr_frontcols], 1
    mov word [gr_rh], 128
.size:
    mov ax, [gr_frontcols]
    mul word [gr_cellbytes]
    mov [gr_stride], ax
    call gr_lightcompose
    pop bp
    inc bp
    cmp bp, 20
    jb .part
    mov ax, [gr_sx]
    shl ax, 1
    mov [gr_stride], ax
    mov si, gr_front_planar1
    cmp word [gr_sx], 1
    je .apes
    mov si, gr_front_planar2
.apes:
    cmp byte [gr_vga], 0
    je .source
    mov si, gr_front_vga2
.source:
    mov di, gr_apecache
    mov bp, 160
.row:
    mov dx, [gr_sy]
.repeat:
    push si
    mov cx, [gr_stride]
    rep movsb
    pop si
    dec dx
    jnz .repeat
    add si, [gr_stride]
    dec bp
    jnz .row
.out:
    ret

; AX=logical x, BX=pose (0/1). The generator supplies native pixel order,
; palette conversion, horizontal scaling, and VGA plane separation.
gr_frontape:
    SAVE
    mov word [gr_frontsource], gr_band
    mov [gr_rx], ax
    mov word [gr_ry], 80
    mov word [gr_rw], 16
    mov word [gr_rh], 20
    cmp byte [gr_vga], 0
    jne .nativecache
    cmp byte [gr_depth], 4
    jne .fallback
    push bx
    call gr_frontcoords
    mov di, 8000h
    call OSAPI_GFX_BLITP
    pop bx
    jc .fallback
    call gr_frontplanar
    jnc .out
.fallback:
    mov byte [gr_frontplane], 1
    mov si, gr_front_mono1
    mov ax, 2
    cmp word [gr_sx], 1
    je .mode
    mov si, gr_front_mono2
    mov ax, 4
.mode:
    cmp byte [gr_vga], 0
    jne .vga
    cmp byte [gr_cga], 0
    jne .cga
    cmp byte [gr_depth], 4
    jne .chosen
    mov si, gr_front_win1
    mov ax, 8
    cmp word [gr_sx], 1
    je .chosen
    mov si, gr_front_win2
    mov ax, 16
    jmp short .chosen
.cga:
    mov si, gr_front_cga1
    mov ax, 4
    jmp short .chosen
.vga:
    mov si, gr_front_vga2
    shl bx, 1
    shl bx, 1               ; four 80-byte planes per pose
    mov ax, 4
.chosen:
    mov [gr_stride], ax
    mov cx, 20
    mul cx
    mul bx
    add si, ax
.plane:
    push ds
    pop es
    mov di, gr_band
    mov bp, 20
.row:
    mov dx, [gr_sy]
.repeat:
    push si
    mov cx, [gr_stride]
    rep movsb
    pop si
    dec dx
    jnz .repeat
    add si, [gr_stride]
    dec bp
    jnz .row
    push si
    call gr_frontcoords
    cmp byte [gr_vga], 0
    jne .native
    cmp byte [gr_cga], 0
    jne .packed
    cmp byte [gr_depth], 4
    jne .mono
    call OSAPI_GFX_BLIT4
    jmp short .done
.mono:
    call OSAPI_GFX_BLIT1
    jmp short .done
.packed:
    call gr_cgablit
    jmp short .done
.native:
    call gr_frontvgaplane
    pop si
    shl byte [gr_frontplane], 1
    cmp byte [gr_frontplane], 16
    jb .plane
    jmp short .out
.done:
    pop si
.out:
    RESTORE
    ret
.nativecache:
    mov word [gr_stride], 4
    mov ax, 80
    mul word [gr_sy]
    mov bp, ax
    shl ax, 1
    shl ax, 1
    mul bx
    add ax, gr_apecache
    mov [gr_frontsource], ax
    mov byte [gr_frontplane], 1
.cachedplane:
    push bp
    call gr_frontcoords
    call gr_frontvgaplane
    pop bp
    add [gr_frontsource], bp
    shl byte [gr_frontplane], 1
    cmp byte [gr_frontplane], 16
    jb .cachedplane
    jmp short .out

; The OS accepts planar bands only when it can honor the complete rectangle.
; Probe first; packed BLIT4 above remains the clipped/cross-display fallback.
gr_frontplanar:
    push bx
    mov ax, [gr_sx]
    shl ax, 1
    mov [gr_stride], ax
    mul word [gr_sy]
    mov cx, 20
    mul cx
    mov di, ax
    shl ax, 1
    shl ax, 1
    mul bx
    add ax, gr_apecache
    mov [gr_frontsource], ax
    call gr_frontcoords
    push ds
    pop es
    call OSAPI_GFX_BLITP
    mov word [gr_frontsource], gr_band
    pop bx
    ret

gr_frontcoords:
    mov ax, [gr_rx]
    mul word [gr_sx]
    add ax, [gr_ox]
    push ax
    mov ax, [gr_ry]
    mul word [gr_sy]
    add ax, [gr_oy]
    mov bx, ax
    mov ax, [gr_rw]
    mul word [gr_sx]
    mov cx, ax
    mov ax, [gr_rh]
    mul word [gr_sy]
    mov dx, ax
    pop ax
    mov bp, [gr_stride]
    mov si, [gr_frontsource]
    ret

; One or more selected VGA planes, with all other planes left untouched.
gr_frontvgaplane:
    push dx
    shr ax, 1
    shr ax, 1
    shr ax, 1
    mov di, ax
    mov ax, bx
    mov cx, 80
    mul cx
    add di, ax
    mov ax, 0a000h
    mov es, ax
    mov al, 2
    mov ah, [gr_frontplane]
    mov dx, 03c4h
    out dx, ax
    pop bx
.row:
    mov cx, bp
    rep movsb
    add di, 80
    sub di, bp
    dec bx
    jnz .row
    ret
