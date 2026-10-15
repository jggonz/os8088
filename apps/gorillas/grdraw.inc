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
    mov cl, 7
    shl dx, cl                 ; sy*128
    xor cl, cl                 ; CX = sx*256
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
    mov si, ax                  ; this phase's masks
    xor ax, ax
    xor bx, bx
    mov cx, 256
    mov dx, 3
    call gr_lightblit
    add si, 96
    mov bx, 125
    call gr_lightblit
    lea bp, [si+96]             ; the left strip's mask
    xor bx, bx
    mov bl, [gr_animphase]
    mov bl, [gr_lightidx+bx]
    lea di, [gr_lightruns+bx]
    xor ax, ax
    call gr_lightside
    add bp, 128                 ; the right strip's, whose runs follow
    mov ax, 248
    call gr_lightside
    cmp byte [gr_state], 8
    je .restoreclip
    mov ax, 80
    xor bx, bx
    mov bl, [gr_pose]
    call gr_frontape
    mov ax, 160
    xor bx, 1
    call gr_frontape
.restoreclip:
    cmp byte [gr_fs], 0
    jne .out
    mov bx, [gr_win]
    call OSAPI_WM_CLIP_SET
.out:
    ret

; Only rows touched by the old or new lights need repainting. The generator
; coalesces neighboring old/new crosses, excluding the blank gaps and corners.
; DI = (first row, count) runs, ended by a zero (DI is left past it); AX = x,
; BP = the strip's mask offset.
gr_lightside:
    xor bx, bx
    mov bl, [di]
    inc di
    or bl, bl
    jz .out
    xor dx, dx
    mov dl, [di]
    inc di
    mov si, bp
    add si, bx
    mov cx, 8
    call gr_lightblit
    jmp gr_lightside
.out:
    ret

; Logical rectangle AX/BX/CX/DX and SI=offset of its 1bpp mask (ratio 1).
align 2                         ; word-aligned on a 16-bit bus, as it was
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
    mov ax, [gr_sx]
    cmp byte [gr_cga], 0
    je .stride
    mov ax, 2
.stride:
    mov [gr_cellbytes], ax
    mul cx
    mov [gr_stride], ax
    mov ax, si
    mul word [gr_cacheratio]
    add ax, gr_lightcache
    ; Cached masks contain one copy of each logical row. Expand only the
    ; strip being drawn into the common band (at most 768 bytes).
    mov si, ax
    mov di, gr_band
    mov bp, [gr_rh]
.expandrow:
    mov dx, [gr_sy]
.expandcopy:
    push si
    mov cx, [gr_stride]
    rep movsb
    pop si
    dec dx
    jnz .expandcopy
    add si, [gr_stride]
    dec bp
    jnz .expandrow
    mov word [gr_frontsource], gr_band
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

; Prepare the intro once per surface (SPEC.md 99.3), before music/animation:
; the five marquee phases' masks, then both raised-arm poses in the surface's
; own form. The 6,400-byte cache holds horizontal masks and the poses (VGA
; planes with rows repeated sy times, else one native row a line).
gr_frontprepare:
    call gr_inktables
    mov al, [gr_tablekey]
    mov ah, [gr_sy]
    cmp ax, [gr_cachekey]
    jne .build
    ret
.build:
    mov [gr_cachekey], ax
    mov ax, [gr_sx]
    cmp byte [gr_cga], 0
    je .ratio
    mov ax, 2
.ratio:
    mov [gr_cacheratio], ax
    ; Five phases of top/bottom (32x3) and side (1x128) masks lie end to
    ; end, lit a cross at a time (tools/gorillas_art.py's light_masks()).
    push ds
    pop es
    mov di, gr_lightcache
    mov cx, 5*448/2
    xor ax, ax
    rep stosw
    mov di, gr_lightcache       ; phase: top +0, bottom +96 (three rows
    xor bp, bp                  ; of 32), left +192, right +320; BP=phase*4
.phase:
    mov cx, bp
.across:
    mov bx, di
    call gr_spark               ; (x, 0)
    push cx
    neg cx
    add cx, 252
    lea bx, [di+96]
    call gr_spark               ; (252-x, 125)
    pop cx
    add cx, 20
    cmp cx, 253
    jb .across
    lea bx, [bp+5]
.down:
    or byte [bx+di+192], 40h    ; (0, y): columns 0..2, a byte a row
    or byte [bx+di+193], 0e0h
    or byte [bx+di+194], 40h
    push bx
    neg bx
    or byte [bx+di+320+125], 02h ; (253, 125-y): columns 253..255
    or byte [bx+di+320+126], 07h
    or byte [bx+di+320+127], 02h
    pop bx
    add bx, 20
    cmp bx, 123
    jb .down
    ; The side strips' three end rows are the across strips' end bytes.
    xor bx, bx
    mov si, di
.corner:
    mov al, [si]
    or [bx+di+192], al
    mov al, [si+31]
    or [bx+di+320], al
    mov al, [si+96]
    or [bx+di+192+125], al
    mov al, [si+96+31]
    or [bx+di+320+125], al
    add si, 32
    inc bx
    cmp bx, 3
    jb .corner
    add di, 448
    add bp, 4
    cmp bp, 20
    jb .phase
    ; Two cache bytes a mask byte at sx 2 and on CGA (palette 2's red),
    ; widened in place from the end.
    cmp byte [gr_cacheratio], 1
    je .apes
    mov dx, 0ffffh
    cmp byte [gr_cga], 0
    je .widemask
    mov dx, 0aaaah
.widemask:
    mov si, gr_lightcache+5*448-1
    mov di, gr_lightcache+2*(5*448-1)
    mov cx, 5*448
    std
.widen:
    lodsb
    xor ah, ah
    shl ax, 1
    xchg bx, ax
    mov ax, [gr_textdouble+bx]
    and ax, dx
    stosw
    loop .widen
    cld
.apes:
    ; Both poses through the surface's pair tables, gr_pairbits bits (so
    ; bytes) a source byte (row): four planes repeated sy times on VGA.
    mov al, [gr_sx]
    cmp byte [gr_depth], 2
    jne .bits
    mov al, 2
.bits:
    add al, al
    mov [gr_pairbits], al
    mov di, gr_apecache
    mov bp, gr_apeleft          ; then its mirror, the right pose
.pose:
    mov bx, gr_inkpairs
    mov byte [gr_planesleft], 1
    cmp byte [gr_depth], 4
    jne .plane
    mov byte [gr_planesleft], 4
.plane:
    mov si, bp
    mov dx, 20
.row:
    push di
    call gr_convrow
    pop ax
    cmp byte [gr_depth], 4
    jne .next
    push si
    xchg si, ax
    mov al, [gr_pairbits]
    mov cl, [gr_sy]
    dec cl
    mul cl
    xchg cx, ax
    rep movsb                   ; overlapping: the row sy-1 more times
    pop si
.next:
    dec dx
    jnz .row
    add bx, 256
    dec byte [gr_planesleft]
    jnz .plane
    cmp bp, gr_apeleft
    jne .done
    call gr_mirrorpose
    mov bp, si
    jmp .pose
.done:
    ret

; One 16-pixel packed row at SI through table BX, gr_pairbits bits a byte.
gr_convrow:
    mov cx, 4
    cmp byte [gr_pairbits], 2
    je .two
.four:
    lodsb
    xlat
    mov ah, al
    lodsb
    xlat
    shl ah, 1
    shl ah, 1
    shl ah, 1
    shl ah, 1
    or al, ah
    stosb
    loop .four
    ret
.two:
    shr cx, 1
.byte:
    lodsb
    xlat
    mov ah, al
%rep 3
    shl ah, 1
    shl ah, 1
    lodsb
    xlat
    or ah, al
%endrep
    mov al, ah
    stosb
    loop .byte
    ret

; The 5-pixel cross at x+1 (CX = x, 0..252) in the 3-row, 32-byte strip at
; BX: one column above and below, three across the middle. Nothing spills
; out of the row: x+2 is in column 31 whenever x is.
gr_spark:
    push bx
    push cx
    mov ax, cx
    shr ax, 1
    shr ax, 1
    shr ax, 1
    add bx, ax
    and cl, 7
    mov ax, 4000h
    shr ax, cl
    or [bx], ah
    or [bx+1], al
    or [bx+64], ah
    or [bx+65], al
    mov ax, 0e000h
    shr ax, cl
    or [bx+32], ah
    or [bx+33], al
    pop cx
    pop bx
    ret

; AX=logical x, BX=pose (0/1). gr_frontprepare supplies native pixel order,
; palette conversion, horizontal scaling, and VGA plane separation.
align 2
gr_frontape:
    SAVE
    mov word [gr_frontsource], gr_band
    mov [gr_rx], ax
    mov word [gr_ry], 80
    mov word [gr_rw], 16
    mov word [gr_rh], 20
    cmp byte [gr_vga], 0
    je .desktop
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
.out:
    RESTORE
    ret
.desktop:
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
    ; Foreign VGA never gets here: it drew from the cache above. Monochrome
    ; and CGA rows are cached too; a clipped or refused desktop VGA pose is
    ; packed from the pose itself, into the band's tail.
    mov ax, [gr_sx]
    shl ax, 1                   ; monochrome: two or four bytes a row
    cmp byte [gr_cga], 0
    je .mode
    mov ax, 4
.mode:
    mov si, gr_apecache
    cmp byte [gr_depth], 4
    jne .chosen
    call gr_widepose
    xor bx, bx
.chosen:
    mov [gr_stride], ax
    mov cx, 20
    mul cx
    mul bx
    add si, ax
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
    call gr_frontcoords
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
.done:
    jmp .out

; Pose BX as packed desktop rows (gr_inktables' fallback map) at the band's
; tail, clear of the 960 bytes the rows expand to: SI = rows, AX = stride.
GR_WIDE equ 5376-320
gr_widepose:
    push ds
    pop es
    mov si, gr_apeleft
    or bx, bx
    jz .left
    call gr_mirrorpose
.left:
    mov di, gr_band+GR_WIDE
    mov bx, gr_inkpairs+1024
    mov cx, 160
    cmp word [gr_sx], 1
    jne .pairs
.byte:
    lodsb
    xlat
    stosb
    loop .byte
    mov ax, 8
    jmp short .out
.pairs:
    lodsb
    xor ah, ah
    shl ax, 1
    xchg bx, ax
    mov ax, [gr_inkpairs+1024+bx]
    stosw
    loop .pairs
    mov ax, 16
.out:
    mov si, gr_band+GR_WIDE
    ret

; The OS accepts planar bands only when it can honor the complete rectangle.
; Probe first; packed BLIT4 above remains the clipped/cross-display fallback.
align 2
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
