; Circular scanline cache. A frame replaces two incoming rows; the remaining
; terrain never shifts in RAM/VRAM. Presentation copies native packed pixels.
; The 16/14-line HUD is outside the ring and never scrolls.
n_scrollsetup:
    mov word [n_ringstart],16*80
    mov word [n_ringend],240*80
    mov word [n_ringbytes],224*80
    mov word [n_worldheight],240
    cmp byte [n_cga],0
    je .out
    mov word [n_ringstart],14*80
    mov word [n_ringend],200*80
    mov word [n_ringbytes],186*80
    mov word [n_worldheight],200
.out:
    ret
n_scrollstep:
    mov word [n_rowrepeat],2
.row:
    sub word [n_scroll],80
    jnc .head
    mov ax,[n_ringbytes]
    sub ax,80
    mov [n_scroll],ax
.head:
%if N_ROM
    dec word [n_worldy]
    jns .source
    mov ax,[n_worldheight]
    dec ax
    mov [n_worldy],ax
    inc word [n_worldpage]
    and word [n_worldpage],7
.source:
    call n_worldrow
    mov ax,[n_scroll]
    add ax,[n_ringstart]
    mov [n_rowdest],ax
    call n_writerow
%endif
    dec word [n_rowrepeat]
    jnz .row
    ret

%if N_ROM
; The bank occupies the unused upper half of the canvas claim. Its layout is
; header, 256 route IDs, 23*240 metatile IDs, 256*4 tile IDs, indexed 8x8 tiles.
N_WORLD equ 32768
N_ROUTE equ N_WORLD+8
N_MAPS equ N_ROUTE+256
N_META equ N_MAPS+23*240
N_TILES equ N_META+1024
n_worldfile: db 'WORLD.V42',0
n_worldcfile: db 'WORLD.C42',0
; Keep the checksum table named for the damage-rejection regression fixture.
n_scenesums: dw N_WORLD_V_SUM,N_WORLD_C_SUM
n_scenecheck:
    cmp word [n_scene],0ffffh
    jne .stage
    call n_loading
    mov ax,[n_canvas]
    add ax,N_WORLD/16
    mov es,ax
    mov si,n_worldfile
    mov cx,N_WORLD_V_SIZE
    mov bp,[n_scenesums]
    cmp byte [n_cga],0
    je .read
    mov si,n_worldcfile
    mov cx,N_WORLD_C_SIZE
    mov bp,[n_scenesums+2]
.read:
    call n_readbank
    jc .bad
    mov word [n_scene],0
    mov ax,[n_stage]
    cmp ax,[n_scrollstage]
    je .rebuild
.stage:
    cmp byte [n_rebuild],0
    je .stagecheck
    mov byte [n_rebuild],0
    jmp .rebuild
.stagecheck:
    mov ax,[n_stage]
    cmp ax,[n_scrollstage]
    je .same
    mov [n_scrollstage],ax
    call n_palette
    mov word [n_worldpage],0
    call n_scrollsetup
    xor dx,dx
    mov ax,[n_ringstart]
    mov bx,80
    div bx
    mov [n_worldy],ax
.rebuild:
    call n_scrollsetup
    mov word [n_scroll],0
    mov ax,[n_worldy]
    mov [n_toprow],ax
    mov ax,[n_worldpage]
    mov [n_toppage],ax
    mov ax,[n_ringstart]
    mov [n_rowdest],ax
.fill:
    call n_worldrow
    call n_writerow
    inc word [n_worldy]
    mov ax,[n_worldy]
    cmp ax,[n_worldheight]
    jb .nextrow
    mov word [n_worldy],0
    dec word [n_worldpage]
    and word [n_worldpage],7
.nextrow:
    add word [n_rowdest],80
    mov ax,[n_rowdest]
    cmp ax,[n_ringend]
    jb .fill
    mov ax,[n_toprow]
    mov [n_worldy],ax
    mov ax,[n_toppage]
    mov [n_worldpage],ax
    call n_refresh
.same:
    clc
    ret
.bad:
    stc
    ret

; Build only one incoming logical scanline. The host already resolved CHR
; bitplanes and metatile palettes. The guest makes 32 eight-byte copies.
n_worldrow:
    SAVE
    push ds
    mov ax,[n_worldy]
    cmp byte [n_cga],0
    je .logical
    mov bx,6
    mul bx
    mov bx,5
    div bx
.logical:
    mov [n_logicalrow],ax
    mov dx,ax
    and dx,7
    shl dx,1
    shl dx,1
    shl dx,1
    mov [n_rowtile],dx
    mov dx,ax
    and dx,8
    shr dx,1
    shr dx,1
    mov [n_rowhalf],dx
    and ax,0fff0h
    mov [n_rowmap],ax
    mov ax,[n_stage]
    dec ax
    shl ax,1
    shl ax,1
    shl ax,1
    add ax,[n_worldpage]
    mov bx,ax
    mov ds,[n_canvas]
    xor ax,ax
    mov al,[N_ROUTE+bx]
    cmp al,2
    jne .map
    cmp byte [cs:n_bossmade],0
    je .map
    cmp byte [cs:n_bosslive],0
    jne .map
    mov al,1
.map:
    mov bx,240
    mul bx
    add ax,N_MAPS
    add ax,[cs:n_rowmap]
    mov [cs:n_rowmap],ax
    push cs
    pop es
    mov di,n_scanline
    mov bp,16
    cmp byte [cs:n_cga],0
    jne .packed
.meta:
    mov si,[cs:n_rowmap]
    xor bx,bx
    mov bl,[si]
    inc word [cs:n_rowmap]
    shl bx,1
    shl bx,1
    add bx,[cs:n_rowhalf]
    mov dx,[N_META+bx]
    call .tile
    mov dl,dh
    call .tile
    dec bp
    jnz .meta
    jmp .done
.packed:
    mov ax,[cs:n_logicalrow]
    and ax,15
    mov bx,5
    mul bx
    mov dx,ax
.cmeta:
    mov si,[cs:n_rowmap]
    xor ax,ax
    lodsb
    mov [cs:n_rowmap],si
    mov bx,80
    push dx
    mul bx
    pop dx
    add ax,dx
    add ax,N_TILES
    mov si,ax
    movsw
    movsw
    movsb
    dec bp
    jnz .cmeta
.done:
    pop ds
    RESTORE
    ret
.tile:
    xor bx,bx
    mov bl,dl
    mov cl,6
    shl bx,cl
    add bx,[cs:n_rowtile]
    lea si,[N_TILES+bx]
    movsw
    movsw
    movsw
    movsw
    ret

n_writerow:
    SAVE
    cmp byte [n_cga],0
    jne .cga
    mov es,[n_fsi+FSI_SEG]
    mov bx,1
    xor bp,bp
.plane:
    mov dx,3c4h
    mov al,2
    mov ah,bl
    out dx,ax
    mov di,[n_rowdest]
    add di,N_BACK+8
    mov si,n_scanline
    add si,bp
    mov cx,64
.pixels:
    mov al,[si]
    stosb
    add si,4
    loop .pixels
    inc bp
    shl bl,1
    cmp bl,16
    jb .plane
    jmp .out
.cga:
    mov es,[n_canvas]
    mov di,[n_rowdest]
    mov si,n_scanline
    mov cx,40
    rep movsw
.out:
    RESTORE
    ret
%endif

; Available before the first disk read. Draw directly into page zero from
; 176 bytes embedded in the package, then leave it visible throughout I/O.
n_loading:
    SAVE
    mov es,[n_fsi+FSI_SEG]
    xor di,di
    xor ax,ax
    mov cx,8192
    cmp byte [n_cga],0
    jne .clear
    mov dx,3ceh
    mov ax,4005h
    out dx,ax
    mov dx,3c4h
    mov ax,0f02h
    out dx,ax
    xor ax,ax
    mov cx,9600
.clear:
    rep stosw
    mov si,n_loadingbits
    mov bp,8
    mov di,112*80+19
    cmp byte [n_cga],0
    je .row
    mov di,48*80+19
.row:
    mov cx,N_LOAD_WIDTH
.char:
    lodsb
    mov bl,al
    push cx
    mov cx,8
.bit:
    xor al,al
    shl bl,1
    jnc .pixel
    mov al,4
.pixel:
    cmp byte [n_cga],0
    jne .cga
    ; Draw one logical pixel, one plane at a time.
    push ax
    mov ax,0102h
    mov dx,3c4h
    mov bh,cl
    neg bh
    and bh,3
    push cx
    mov cl,bh
    shl ah,cl
    pop cx
    out dx,ax
    pop ax
    mov [es:di],al
    cmp bh,3
    jne .next
    inc di
    jmp .next
.cga:
    ; Eight monochrome pixels make two packed CGA bytes.
    or al,al
    jz .black
    mov al,3
.black:
    shl dh,1
    shl dh,1
    or dh,al
    mov bh,cl
    dec bh
    test bh,3
    jnz .next
    mov [es:di],dh
    inc di
.next:
    loop .bit
    pop cx
    loop .char
    add di,80-N_LOAD_WIDTH*2
    cmp byte [n_cga],0
    je .advance
    ; Convert linear row advancement to the two interleaved CGA banks.
    sub di,80
    xor di,2000h
    test di,2000h
    jnz .advance
    add di,80
.advance:
    dec bp
    jnz .row
    cmp byte [n_cga],0
    je .vpage
    mov word [n_cgastart],0
    mov dx,3d4h
    mov ax,000ch
    out dx,ax
    inc ax
    out dx,ax
    jmp .out
.vpage:
    mov word [n_showbase],0
    xor ax,ax
    call OSAPI_FSX_PAGE
.out:
    RESTORE
    ret


; Union CGA old/new actor rectangles and the fixed HUD into one interval per
; scanline. Merge equal adjacent intervals, restore each region once, and
; present that same list once. Overlapping actors never multiply VRAM traffic.
n_damagebuild:
    SAVE
    push ds
    pop es
    mov di,n_spans
    mov ax,0050h                    ; min=80, max=0: empty
    mov cx,200
    rep stosw
    mov si,n_rects
    mov bp,[n_counts]
.rect:
    or bp,bp
    jz .merge
    mov ax,[si]
    xor dx,dx
    mov bx,80
    div bx
    shl ax,1
    mov di,n_spans
    add di,ax
    mov ax,dx
    mov ah,al
    add ah,[si+2]
    mov cx,[si+4]
.span:
    cmp [di],al
    jbe .left
    mov [di],al
.left:
    cmp [di+1],ah
    jae .right
    mov [di+1],ah
.right:
    add di,2
    loop .span
    add si,6
    sub bp,6
    jmp .rect
.merge:
    mov word [n_damagecount],0
    mov word [n_damageend],0ffffh
    mov si,n_spans
    mov di,n_rects+N_LIST_BYTES
    xor dx,dx
    mov bp,200
.row:
    lodsw
    cmp al,ah
    jae .next
    mov bl,ah
    sub bl,al
    xor bh,bh
    xor ah,ah
    add ax,dx
    cmp ax,[n_damageend]
    jne .new
    cmp bx,[di-4]
    jne .new
    inc word [di-2]
    jmp .end
.new:
    stosw
    mov [di],bx
    mov word [di+2],1
    add di,4
    add word [n_damagecount],6
.end:
    add ax,80
    mov [n_damageend],ax
.next:
    add dx,80
    dec bp
    jnz .row
    mov byte [n_damagevalid],1
    cmp byte [n_prepare],1
    jne .out
    mov si,n_rects+N_LIST_BYTES
    mov bp,[n_damagecount]
.restore:
    or bp,bp
    jz .out
    call n_restore
    add si,6
    sub bp,6
    jmp .restore
.out:
    RESTORE
    ret
