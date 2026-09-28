; 256x240 canvas, dirty 32-pixel blocks, and two hardware presenters.
; VGA canvas is four contiguous planes; CGA canvas is chunky indices.
; Drawing inputs: CX=x, DX=y, BX=width, BP=height, AL=ink (rectangle).
n_dirtyall:
    SAVE
    push ds
    pop es
    mov di, n_dirty
    mov ax, 0ffffh
    mov cx, 120
    rep stosw
    RESTORE
    ret
n_clean:
    SAVE
    push ds
    pop es
    mov di, n_dirty
    xor ax, ax
    mov cx, 120
    rep stosw
    RESTORE
    ret
n_clear:
    SAVE
    mov es, [n_canvas]
    xor di, di
    mov ax, 0101h
    mov cx, 30720
    rep stosw
    call n_dirtyall
    RESTORE
    ret
n_leftmask: db 255,254,252,248,240,224,192,128
n_rightmask: db 1,3,7,15,31,63,127,255
n_mark:
    SAVE
    cmp cx, 256
    jae .out
    cmp dx, 240
    jae .out
    add bx, cx
    cmp bx, 256
    jbe .xok
    mov bx, 256
.xok:
    dec bx
    mov si, cx
    mov cl, 5
    shr bx, cl
    shr si, cl
    mov al, [n_leftmask+si]
    and al, [n_rightmask+bx]
    mov si, dx
    add bp, dx
    cmp bp, 240
    jbe .yok
    mov bp, 240
.yok:
    cmp si, bp
    jae .out
    or [n_dirty+si], al
    inc si
    jmp .yok
.out:
    RESTORE
    ret
n_rect:
    cmp byte [n_cga], 0
    je n_prect
    SAVE
    cmp cx, 256
    jae .out
    cmp dx, 240
    jae .out
    mov [n_color], al
    call n_mark
    mov ax, 240
    sub ax, dx
    cmp bp, ax
    jbe .height
    mov bp, ax
.height:
    mov ax, 256
    sub ax, cx
    cmp bx, ax
    jbe .width
    mov bx, ax
.width:
    mov di, dx
    mov ax, di
    mov ah, al
    xor al, al
    mov di, ax
    add di, cx
    mov es, [n_canvas]
    mov al, [n_color]
.row:
    mov cx, bx
    rep stosb
    add di, 256
    sub di, bx
    dec bp
    jnz .row
.out:
    RESTORE
    ret
; SI points to width,height, then row-major indices. Zero is transparent.
; All game sprites fit within the canvas; the bottom is clipped defensively.
n_sprite:
    cmp byte [n_cga], 0
    je n_psprite
    SAVE
    cmp cx, 256
    jae .out
    cmp dx, 240
    jae .out
    xor bx, bx
    mov bl, [si]
    xor ax, ax
    mov al, [si+1]
    mov bp, ax
    add si, 2
    mov [n_sw], bx
    mov ax, 256
    sub ax, cx
    cmp bx, ax
    jbe .width
    mov bx, ax
.width:
    mov [n_sh], bx
    mov ax, 240
    sub ax, dx
    cmp bp, ax
    jbe .height
    mov bp, ax
.height:
    call n_mark
    mov di, dx
    mov ax, di
    mov ah, al
    xor al, al
    mov di, ax
    add di, cx
    mov es, [n_canvas]
.row:
    mov cx, [n_sh]
.pixel:
    lodsb
    or al, al
    jz .skip
    mov [es:di], al
.skip:
    inc di
    loop .pixel
    mov ax, [n_sw]
    sub ax, [n_sh]
    add si, ax
    add di, 256
    sub di, [n_sh]
    dec bp
    jnz .row
.out:
    RESTORE
    ret
; SI=NUL ASCII, CX/DX origin. Always opaque cream over ocean.
n_text:
    cmp byte [n_cga], 0
    je n_ptext
    SAVE
.char:
    lodsb
    or al, al
    jz .out
    cmp al, 32
    jb .char
    cmp al, 90
    ja .char
    cmp cx, 248
    ja .out
    cmp dx, 232
    ja .out
    SAVE
    sub al, 32
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov si, ax
    add si, n_font
    mov bx, 8
    mov bp, 8
    call n_mark
    mov ax, dx
    mov ah, al
    xor al, al
    mov di, ax
    add di, cx
    mov es, [n_canvas]
.row:
    lodsb
    mov ah, al
    mov cx, 8
.bit:
    mov al, 1
    shl ah, 1
    jnc .paper
    mov al, 4
.paper:
    stosb
    loop .bit
    add di, 248
    dec bp
    jnz .row
    RESTORE
    add cx, 8
    jmp .char
.out:
    RESTORE
    ret
n_palette:
    SAVE
    cmp byte [n_cga], 0
    jne .cga
    mov dx, 3c8h
    xor al, al
    out dx, al
    inc dx
    mov si, n_dac
    mov cx, 48
.dac:
    lodsb
    out dx, al
    loop .dac
    jmp .out
.cga:
    xor bx, bx
    mov bl, [n_profile]
    mov al, [n_cgaregs+bx]
    mov dx, 3d9h
    out dx, al
    ; A source word holds two 4-bit color indices, one per byte. Its direct
    ; 4096-entry lookup avoids shifting indices in every CGA pixel loop.
    mov ax, bx
    mov cl, 4
    shl ax, cl
    add ax, n_cgamaps
    mov bp, ax
    xor bx, bx
.pairs:
    mov si, bx
    and si, 15
    mov al, [ds:bp+si]
    shl al, 1
    shl al, 1
    mov dl, al
    mov ax, bx
    mov al, ah
    xor ah, ah
    mov si, ax
    mov al, [ds:bp+si]
    or al, dl
    mov [n_pairs+bx], al
    inc bx
    cmp bx, 4096
    jb .pairs
.out:
    RESTORE
    ret
n_present:
    SAVE
    push ds
    cmp byte [n_cga], 0
    jne n_present_cga
    mov ax, [n_fsi+FSI_SEG]
    mov es, ax
    mov ax, [n_canvas]
    mov ds, ax
    mov word [cs:n_vplane], 0
    mov byte [cs:n_vmask], 1
.plane:
    mov dx, 3c4h
    mov al, 2
    out dx, al
    inc dx
    mov al, [cs:n_vmask]
    out dx, al
    xor bp, bp                     ; source row
.row:
    mov dl, [cs:n_dirty+bp]
    or dl, dl
    jz .next
    mov [cs:n_vbits], dl
    mov ax, bp
    mov dx, 80
    mul dx
    mov di, ax
    add di, 8                      ; 32-pixel black border
    mov ax, [cs:n_vplane]
    mov dx, 15360
    mul dx
    mov si, ax
    mov ax, bp
    mov cl, 6
    shl ax, cl
    add si, ax
    mov dx, 8
.block:
    shr byte [cs:n_vbits], 1
    jnc .skip
    mov cx, 4
    rep movsw
    jmp .blockend
.skip:
    add si, 8
    add di, 8
.blockend:
    dec dx
    jnz .block
.next:
    inc bp
    cmp bp, 240
    jb .row
    inc word [cs:n_vplane]
    shl byte [cs:n_vmask], 1
    cmp word [cs:n_vplane], 4
    jb .plane
.done:
    pop ds
    call n_clean
    RESTORE
    ret

; Expand four logical colors a,b,c,d into the ten bits a,a,b,c,d.
; The four consecutive groups pack into five CGA bytes (16 -> 20 pixels).
%macro PACK4 0
    lodsw
    mov bx, ax
    mov dl, [cs:n_pairs+bx]
    lodsw
    mov bx, ax
    mov al, [cs:n_pairs+bx]
    shl dl, 1
    shl dl, 1
    shl dl, 1
    shl dl, 1
    or al, dl
    mov ah, al
    rol ah, 1
    rol ah, 1
    and ah, 3
%endmacro
n_present_cga:
    mov ax, [n_fsi+FSI_SEG]
    mov es, ax
    mov ax, [n_canvas]
    mov ds, ax
    mov word [cs:n_vrow], 0
.row:
    mov ax, [cs:n_vrow]
    mov bx, 6
    mul bx
    mov bx, 5
    div bx
    mov si, ax
    mov al, [cs:n_dirty+si]
    or al, al
    jz .next
    mov [cs:n_vbits], al
    mov byte [cs:n_vblocks], 8
    mov ax, si
    mov ah, al
    xor al, al
    mov si, ax
    mov ax, [cs:n_vrow]
    shr ax, 1
    mov bx, 80
    mul bx
    mov di, ax
    test word [cs:n_vrow], 1
    jz .even
    add di, 2000h
.even:
.testblock:
    shr byte [cs:n_vbits], 1
    jc .dirty
    add si, 32
    add di, 10
    jmp .blockend
.dirty:
    mov cx, 2
.block:
    PACK4
    mov bp, ax
    shr ax, 1
    shr ax, 1
    stosb
    PACK4
    xchg ax, bp
    and al, 3
    ror al, 1
    ror al, 1
    mov dx, bp
    shr dx, 1
    shr dx, 1
    shr dx, 1
    shr dx, 1
    or al, dl
    stosb
    PACK4
    xchg ax, bp
    and al, 15
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1
    mov dx, bp
    ; Ten bits >> 6: rotate low word right six, keep its low four.
    ror dx, 1
    ror dx, 1
    ror dx, 1
    ror dx, 1
    ror dx, 1
    ror dx, 1
    and dl, 15
    or al, dl
    stosb
    PACK4
    xchg ax, bp
    shl al, 1
    shl al, 1
    mov dx, bp
    or al, dh
    stosb
    mov ax, bp
    stosb
    dec cx
    jnz .block
.blockend:
    dec byte [cs:n_vblocks]
    jnz .testblock
.next:
    inc word [cs:n_vrow]
    cmp word [cs:n_vrow], 200
    jb .row
    jmp n_present.done

; VGA drawing stays in planar RAM. The hardware present is then a word copy.
; n_planeaddr uses the staged rect and plane; DI=destination, BX=source delta.
n_planeaddr:
    mov bx, [n_pplane]
    sub bx, [n_sx]
    and bx, 3
    mov ax, [n_pplane]
    mov cx, 15360
    mul cx
    mov di, ax
    mov ax, [n_sy]
    mov cl, 6
    shl ax, cl
    add di, ax
    mov ax, [n_sx]
    add ax, bx
    shr ax, 1
    shr ax, 1
    add di, ax
    ret
n_prect:
    SAVE
    cmp cx, 256
    jae .out
    cmp dx, 240
    jae .out
    mov [n_color], al
    mov [n_sx], cx
    mov [n_sy], dx
    mov ax, 256
    sub ax, cx
    cmp bx, ax
    jbe .width
    mov bx, ax
.width:
    mov ax, 240
    sub ax, dx
    cmp bp, ax
    jbe .height
    mov bp, ax
.height:
    mov [n_sw], bx
    mov [n_sh], bp
    call n_mark
    mov es, [n_canvas]
    mov word [n_pplane], 0
.plane:
    call n_planeaddr
    mov ax, [n_sw]
    sub ax, bx
    jbe .next
    add ax, 3
    shr ax, 1
    shr ax, 1
    mov bx, ax
    mov bp, [n_sh]
    mov al, [n_color]
.row:
    mov cx, bx
    rep stosb
    add di, 64
    sub di, bx
    dec bp
    jnz .row
.next:
    inc word [n_pplane]
    cmp word [n_pplane], 4
    jb .plane
.out:
    RESTORE
    ret
n_psprite:
    SAVE
    cmp cx, 256
    jae .out
    cmp dx, 240
    jae .out
    mov [n_sx], cx
    mov [n_sy], dx
    xor bx, bx
    mov bl, [si]
    mov [n_sw], bx
    xor ax, ax
    mov al, [si+1]
    mov bp, ax
    add si, 2
    mov [n_src], si
    mov ax, 240
    sub ax, dx
    cmp bp, ax
    jbe .height
    mov bp, ax
.height:
    mov [n_sh], bp
    mov ax, 256
    sub ax, cx
    cmp bx, ax
    jbe .width
    mov bx, ax
.width:
    mov [n_pwidth], bx
    call n_mark
    mov es, [n_canvas]
    mov word [n_pplane], 0
.plane:
    call n_planeaddr
    mov si, [n_src]
    add si, bx
    mov ax, [n_pwidth]
    sub ax, bx
    jbe .next
    add ax, 3
    shr ax, 1
    shr ax, 1
    mov bx, ax
    mov bp, [n_sh]
.row:
    mov cx, bx
.pixel:
    mov al, [si]
    add si, 4
    or al, al
    jz .skip
    mov [es:di], al
.skip:
    inc di
    loop .pixel
    mov ax, bx
    shl ax, 1
    shl ax, 1
    sub si, ax
    add si, [n_sw]
    add di, 64
    sub di, bx
    dec bp
    jnz .row
.next:
    inc word [n_pplane]
    cmp word [n_pplane], 4
    jb .plane
.out:
    RESTORE
    ret
n_ptext:
    SAVE
.char:
    lodsb
    or al, al
    jz .out
    cmp al, 32
    jb .char
    cmp al, 90
    ja .char
    cmp cx, 248
    ja .out
    cmp dx, 232
    ja .out
    SAVE
    sub al, 32
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov si, ax
    add si, n_font
    push ds
    pop es
    mov di, n_fontsprite
    mov ax, 0808h
    stosw
    mov bp, 8
.row:
    lodsb
    mov ah, al
    mov bx, 8
.bit:
    mov al, 1
    shl ah, 1
    jnc .paper
    mov al, 4
.paper:
    stosb
    dec bx
    jnz .bit
    dec bp
    jnz .row
    mov si, n_fontsprite
    call n_psprite
    RESTORE
    add cx, 8
    jmp .char
.out:
    RESTORE
    ret
