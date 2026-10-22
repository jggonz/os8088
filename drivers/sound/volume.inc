; Quick volume UI, SPEC.md 34.15. The loaded driver owns this code; no
; system floppy is needed for volume. The kernel lends one private
; cursor/event bridge, never an app callback.
MVP_W equ 208
MVP_Y equ MBAR_H

mix_icon:
    push es
    push ds
    pop es
    call OSAPI_VIDEO
    mov si, mix_speaker
    cmp dh, 1
    jne .draw
    cmp byte [volume_iconpaper], CBLACK
    jne .draw
    ; A 1bpp BLIT1 is already in screen polarity; its pen cannot invert it.
    ; Reuse the modal slider's scratch for the small Dark-theme bitmap.
    mov di, volume_band
    mov cx, 24
    cld
.invert:
    lodsw
    not ax
    stosw
    loop .invert
    mov si, volume_band
.draw:
    mov ax, cx
    mov bx, 2
    mov cx, 24
    mov dx, 16
    mov bp, 3
    mov ax, [volume_iconx]      ; set by the entry below, before AX was ink
    call OSAPI_GFX_BLIT1
    pop es
    clc
    ret
mix_icon_entry:
    mov ax, (CBLACK << 8) | CWHITE  ; legacy verb keeps its original palette
mix_icon_theme_entry:
    mov [volume_iconx], cx
    mov [volume_iconpaper], al
    call OSAPI_GFX_BLIT1_PEN    ; AL=background bits, AH=speaker ink
    jmp mix_icon
; 24x16, black bits in a white field: horn and two expanding sound arcs.
mix_speaker:
    db 0FFh,0FFh,0FFh, 0FFh,0FFh,0FFh, 0FFh,0BFh,0FFh, 0FFh,3Bh,0FFh
    db 0FEh,0BDh,0FFh, 0F8h,0AEh,0FFh, 0F8h,0B6h,0FFh, 0F8h,0B6h,0FFh
    db 0F8h,0B6h,0FFh, 0F8h,0B6h,0FFh, 0F8h,0AEh,0FFh, 0FEh,0BDh,0FFh
    db 0FFh,3Bh,0FFh, 0FFh,0BFh,0FFh, 0FFh,0FFh,0FFh, 0FFh,0FFh,0FFh

; CX=bar x, ES:DI=kernel's far bridge. No lock on entry/return.
volume_popup:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov ax, es
    cmp ax, KERNEL_SEG          ; this private UI is only entered by the bar
    jne .out
    cmp byte [mix_kind], 0
    je .out
    cmp byte [volume_live], 0
    jne .out
    mov ax, [es:di]
    mov [volume_bridge], ax
    mov ax, [es:di+2]
    mov [volume_bridge+2], ax
    mov [volume_iconx], cx
    mov byte [volume_drag], 0
    call OSAPI_VIDEO
    mov [volume_planes], dh
    sub ax, MVP_W+8
    mov bx, [volume_iconx]
    sub bx, 168
    and bx, 0FFF8h
    cmp bx, 8
    jge .left
    mov bx, 8
.left:
    cmp bx, ax
    jbe .place
    mov bx, ax
.place:
    mov [volume_x], bx
    mov ax, MVP_Y+71
    mov [volume_bottom], ax
    ; X and width are byte-aligned: 26 columns * actual rows * planes.
    ; 2KB mono, 8KB VGA/EGA, 15/30KB VESA at 8/16bpp.
    sub ax, MVP_Y-1
    mov bx, MVP_W/8
    mul bx
    xor bx, bx
    mov bl, [volume_planes]
    mul bx
    add ax, 1023
    mov cl, 10
    shr ax, cl
    call OSAPI_MEM_CLAIM
    jc .nomem
    mov [volume_save], dx
    call OSAPI_GFX_LOCK
    call volume_rect
    mov es, [volume_save]
    xor di, di
    call OSAPI_GFX_SAVE
    jc .nosave
    mov al, CWHITE
    call OSAPI_SET_COLOR
    call volume_rect
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR
    call volume_rect
    call OSAPI_GFX_FRAME
    xor al, al
    call mix_get
    mov [volume_pair], cx       ; baseline is never overwritten by a drag
    call volume_level
    mov [volume_levelv], al
    call volume_draw
    mov byte [volume_live], 1
    xor al, al                  ; bridge 0: spend lazy hide and show arrow
    call far [volume_bridge]
    mov byte [volume_prev], 1  ; opening press may already have been released
.poll:
    mov ah, 1
    int 16h
    jz .mouse
    xor ah, ah
    int 16h
    cmp al, 27
    je .dismiss
    cmp al, 13
    je .dismiss
    cmp ah, 4Bh                 ; Left/Down quieter, Right/Up louder
    je .quieter
    cmp ah, 50h
    je .quieter
    cmp ah, 4Dh
    je .louder
    cmp ah, 48h
    je .louder
    cmp ah, 47h
    je .home
    cmp ah, 4Fh
    jne .mouse
    mov al, 100
    jmp short .keyset
.home:
    xor al, al
    jmp short .keyset
.quieter:
    mov al, [volume_levelv]
    sub al, 1
    jnc .keyset
    xor al, al
    jmp short .keyset
.louder:
    mov al, [volume_levelv]
    add al, 1
    cmp al, 100
    jbe .keyset
    mov al, 100
.keyset:
    call volume_set
.mouse:
    mov al, 2
    call far [volume_bridge]    ; AL=buttons, CX/DX=live, indivisible coords
    and al, 1
    mov [volume_btn], al
    or al, al
    jz .release
    cmp byte [volume_prev], 0
    je .press
    cmp byte [volume_drag], 0
    jne .slide                 ; captured drag keeps its x even outside y
    cmp dx, MVP_Y              ; initial held press can drag into the track
    jb .wait
    cmp dx, MVP_Y+64
    jae .wait
    mov byte [volume_drag], 1
.press:
    cmp byte [volume_drag], 0
    jne .slide
    mov ax, [volume_x]
    cmp cx, ax
    jb .dismiss
    add ax, MVP_W
    cmp cx, ax
    jae .dismiss
    cmp dx, MVP_Y
    jb .dismiss
    cmp dx, [volume_bottom]
    ja .dismiss
    cmp dx, MVP_Y+64
    jb .grab
    jmp short .wait
.grab:
    mov byte [volume_drag], 1
.slide:
    mov ax, [volume_x]
    add ax, 16
    call mixui_hit
    call volume_set
    jmp short .wait
.release:
    cmp byte [volume_prev], 0
    je .wait
    cmp byte [volume_drag], 0
    jne .dismiss              ; press-drag-release changes and closes
    ; Opening click released on the icon: leave the panel up for a click.
    jmp short .wait
.wait:
    mov al, [volume_btn]
    mov [volume_prev], al
    call OSAPI_TASK_YIELD
    jmp .poll
.dismiss:
    mov al, 1                  ; arrow down before restoring its background
    call far [volume_bridge]
    call volume_rect
    mov es, [volume_save]
    xor si, si
    call OSAPI_GFX_REST
    ; Consume the entire dismissing gesture, including a held outside press.
.drain:
    mov al, 2
    call far [volume_bridge]
    test al, 1
    jz .drained
    call OSAPI_TASK_YIELD
    jmp short .drain
.drained:
    mov al, 3
    call far [volume_bridge]    ; drain modal mouse edges from event ring
.nosave:
    call OSAPI_GFX_UNLOCK
    mov dx, [volume_save]
    call OSAPI_MEM_FREE
    mov word [volume_save], 0
    mov byte [volume_live], 0
    jmp short .out
.nomem:
    push ds
    pop es
    mov si, volume_memerr
    xor cx, cx
    call OSAPI_TOAST
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

volume_rect:
    mov ax, [volume_x]
    mov bx, MVP_Y
    mov cx, ax
    add cx, MVP_W-1
    mov dx, [volume_bottom]
    ret
volume_level:                  ; pair CX -> AL=max(L,R)
    mov al, cl
    cmp al, ch
    jae .out
    mov al, ch
.out:
    ret
volume_draw:
    mov ax, [volume_x]
    add ax, 16
    mov bx, MVP_Y+12
    mov cl, [volume_levelv]
    mov si, volume_label
    call mixui_draw
    ret

volume_set:
    cmp al, [volume_levelv]
    je .out
    push ax                    ; keep the requested position, not the DAC step
    mov cx, [volume_pair]
    call mixui_scale
    xor al, al
    call mix_set
    pop ax
    jc .out
    mov [volume_levelv], al
    mov al, 1
    call far [volume_bridge]
    call volume_draw
    xor al, al
    call far [volume_bridge]
.out:
    ret

volume_label:  db 'Volume',0
volume_memerr: db 'Volume needs memory',0

%define MIXUI_BUFFER volume_band
%include "os88mixui.inc"

%macro VOLUME_STATE 0
volume_bridge: times 4 db 0
volume_iconx: dw 0
volume_iconpaper: db 0
volume_x: dw 0
volume_bottom: dw 0
volume_save: dw 0
volume_live: db 0
volume_pair: dw 0
volume_levelv: db 0
volume_planes: db 0
volume_drag: db 0
volume_prev: db 0
volume_btn: db 0
volume_band: times 176*48/8 db 0
%endmacro
