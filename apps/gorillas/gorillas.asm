; Gorillas for os8088. See SPEC.md 99 and README.md.
; Gameplay adapted from the supplied Microsoft QBasic Gorillas (1990).
; Native renderer and fixed-point simulation; no BASIC runtime required.
%include "os88api.inc"
OS88_HEADER 'GORILLAS', gr_entry, 1, OS88_STACK_256
OS88_ICON16
    dw 0,0x0f00,0x1f80,0x1680,0x1f80,0x3fc0,0x7fe0,0xef70
    dw 0xcf30,0xcf30,0x1f80,0x1980,0x3180,0x73c0,0,0
    dw 0,0x0f00,0x1f80,0x1680,0x1f80,0x3fc0,0x7fe0,0xef70
    dw 0xcf30,0xcf30,0x1f80,0x1980,0x3180,0x73c0,0,0
OS88_ICON16_END

%macro SAVE 0
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
%endmacro
%macro RESTORE 0
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
%endmacro

gr_entry:
    mov si, gr_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [gr_win], bx
    OS88_REGION_MOVABLE
    OS88_ALTENTER_ARM
    mov si, gr_pref
    call OSAPI_WM_PREFER
    mov si, gr_menus
    call OSAPI_MENU_SET
    mov si, gr_about
    call OSAPI_ABOUT_SET
    mov al, 1
    call OSAPI_WM_OWNBG
    call OSAPI_FONT_GLYPHS
    mov [gr_fontseg], dx
    mov [gr_fontoff], si
    mov [gr_fontfirst], al
    call OSAPI_GET_TICKS
    or ax, 1
    mov [gr_seed], ax
    call gr_intro
    mov bx, [gr_win]
    clc
.out:
    ret

gr_paint:
    SAVE
    cmp byte [gr_spawned], 0
    jne .draw
    mov ax, gr_worker
    mov bx, [gr_win]
    call OSAPI_TASK_SPAWN
    jc .draw
    mov byte [gr_spawned], 1
    OS88_WORKER_RESTARTABLE gr_worker
.draw:
    call gr_layout
    call gr_fullpaint
    cmp byte [gr_abon], 0
    je .out
    mov bx, [gr_win]
    mov si, gr_ablines
    call os88ui_about_d
.out:
    RESTORE
    ret

gr_about:
    SAVE
    mov byte [gr_abon], 1
    mov bx, [gr_win]
    mov si, gr_ablines
    call os88ui_about
    RESTORE
    ret

gr_click:
    SAVE
    cmp byte [gr_abon], 0
    je .out
    mov byte [gr_abon], 0
    call gr_layout
    call gr_fullpaint
.out:
    RESTORE
    ret

gr_onkey:
    SAVE
    push ax
    mov bx, [gr_win]
    call OSAPI_WM_CLIP_SET
    pop ax
    jc .out
    push ax
    call gr_layout
    pop ax
    call gr_key
.out:
    RESTORE
    ret

gr_oncmd:
    ; AL item index, AH menu index.
    SAVE
    cmp al, 0
    je .new
    cmp al, 1
    je .full
    mov ax, 'p'
    jmp short .key
.new:
    mov ax, 'n'
    jmp short .key
.full:
    mov ax, KEY_ALTENTER
.key:
    push ax
    mov bx, [gr_win]
    call OSAPI_WM_CLIP_SET
    pop ax
    jc .out
    push ax
    call gr_layout
    pop ax
    cmp al, 'n'
    je .setup
    cmp ax, KEY_ALTENTER
    je .dispatch
    cmp byte [gr_state], 4
    jae .out
.dispatch:
    call gr_key
    jmp short .out
.setup:
    call gr_setup
.out:
    RESTORE
    ret

gr_key:
    cmp byte [gr_abon], 0
    je .command
    mov byte [gr_abon], 0
    jmp gr_fullpaint
.command:
    cmp ax, KEY_ALTENTER
    je .full
    cmp al, 27
    je .escape
    cmp byte [gr_state], 4
    jae gr_frontkey
    cmp al, 'F'
    je .full
    cmp al, 'f'
    je .full
    cmp al, 'N'
    je .new
    cmp al, 'n'
    je .new
    cmp al, 'P'
    je .pause
    cmp al, 'p'
    je .pause
    cmp byte [gr_paused], 0
    jne .human
    cmp byte [gr_players], 1
    jne .human
    cmp byte [gr_turn], 1
    jne .human
    cmp byte [gr_state], 0
    je .out
.human:
    cmp al, 13
    je .enter
    cmp byte [gr_state], 1
    je .out
    cmp byte [gr_state], 0
    jne .out
    cmp al, 9
    je .tab
    jmp gr_aimkey
.tab:
    xor byte [gr_field], 1
    mov byte [gr_edit], 0
    jmp gr_selectpaint
.hud:
    jmp gr_hudpaint
.enter:
    cmp byte [gr_paused], 0
    jne .pause
    cmp byte [gr_dancing], 0
    jne .out
    cmp byte [gr_blast], 0
    jne .out
    cmp byte [gr_state], 1
    je .out
    cmp byte [gr_state], 2
    je .round
    cmp byte [gr_state], 3
    je gr_results
    cmp byte [gr_field], 0
    je .tab
    call gr_fire
    jmp gr_hudpaint
.round:
    call gr_city
    jmp gr_fullpaint
.new:
    jmp gr_setup
.pause:
    xor byte [gr_paused], 1
    jmp gr_hudpaint
.escape:
    cmp byte [gr_fs], 0
    je .out
.full:
    cmp byte [gr_fs], 0
    je gr_fullscreen
    mov byte [gr_quit], 1
.out:
    ret

; Model mutations and drawing share the graphics lock, so keyboard input
; cannot reset a shot halfway through a worker frame. No API wait under it.
gr_worker:
    mov bx, [gr_win]
    call OSAPI_TASK_ALIVE
    cmp byte [gr_dancing], 0
    jne .active
    cmp word [gr_musicptr], 0
    jne .active
    mov al, [gr_state]
    cmp al, 1
    je .active
    cmp al, 4
    je .active
    cmp al, 5
    je .active
    cmp al, 7
    je .active
    cmp al, 8
    je .active
    cmp al, 2
    je .active
    cmp al, 3
    je .active
    or al, al
    jne .sleep
    cmp byte [gr_players], 1
    jne .sleep
    cmp byte [gr_turn], 1
    jne .sleep
.active:
    cmp byte [gr_paused], 0
    jne .sleep
    cmp byte [gr_abon], 0
    jne .sleep
    call OSAPI_GFX_LOCK
    mov bx, [gr_win]
    call OSAPI_WM_OBSCURED
    jc .unlock
    call OSAPI_WM_TOP
    cmp bx, [gr_win]
    jne .unlock
    call OSAPI_MENU_OWNER
    cmp bx, [gr_win]
    jne .unlock
    call OSAPI_WM_CLIP_SET
    jc .unlock
    call gr_layout
    call gr_tick
.unlock:
    call OSAPI_GFX_UNLOCK
.sleep:
    mov ax, 1
    call OSAPI_TASK_SLEEP
    jmp gr_worker

gr_fullscreen:
    mov bx, [gr_win]
    mov ax, gr_exclusive
    xor cx, cx
    call OSAPI_FSX_RUN
    ret

gr_exclusive:
    mov byte [gr_fs], 1
    mov byte [gr_fsready], 0
    mov byte [gr_quit], 0
    mov byte [gr_cga], 0
    mov byte [gr_vga], 0
    OS88_ALTENTER_SEED
    call gr_release_chord
    xor bx, bx
    call OSAPI_FSX_CAPS
    cmp dl, VID_VGA
    je .vga
    cmp dl, VID_CGA
    jne .native
    push ds
    pop es
    mov di, gr_fsi
    mov al, FSXM_CGA320
    call OSAPI_FSX_MODE
    jc .done
    mov byte [gr_cga], 1
    mov dx, 03d9h
    mov al, 11h                ; blue sky, bright green/red/yellow group
    out dx, al
    jmp short .native
.vga:
    push ds
    pop es
    mov di, gr_fsi
    mov al, FSXM_VGA12
    call OSAPI_FSX_MODE
    jc .done
    mov byte [gr_vga], 1
    call gr_vgapalette
.native:
    call gr_layout
    call gr_fullpaint
    mov byte [gr_fsready], 1
.loop:
    call os88alt_edge
    jc .done
    mov ah, 1
    int 16h
    jz .tick
    xor ah, ah
    int 16h
    call gr_key
    cmp byte [gr_quit], 0
    jne .done
.tick:
    ; Drain buffered aiming keys without adding a BIOS tick per character.
    ; Flight still advances and waits once per frame.
    call gr_tick
.aimwait:
    mov ah, 1
    int 16h
    jz .wait
    cmp byte [gr_state], 0
    je .loop
    cmp byte [gr_state], 5
    je .loop
.wait:
    mov al, FSXW_TICK
    call OSAPI_FSX_WAIT
    jmp .loop
.done:
    ; Clear BEFORE FSX_RUN restores and invokes our window paint.
    mov byte [gr_fs], 0
    mov byte [gr_fsready], 0
    mov byte [gr_cga], 0
    mov byte [gr_vga], 0
    ret

; A VGA BIOS mode set masks interrupts long enough to lose the Alt break
; from a quick Alt+Enter. Wait BEFORE entering that BIOS call, while IRQ1
; can still retire both releases. Otherwise BDA 40:17 keeps Alt set and
; int 16h delivers Alt-number shortcuts instead of angle/velocity digits.
gr_release_chord:
    mov al, KSC_ALT
    call OSAPI_KEY_DOWN
    jc .wait
    mov al, KSC_ENTER
    call OSAPI_KEY_DOWN
    jnc .done
.wait:
    mov al, FSXW_TICK
    call OSAPI_FSX_WAIT
    jmp gr_release_chord
.done:
    ret

; Re-derive all physical coordinates at each draw, including cached drags.
gr_layout:
    cmp byte [gr_fs], 0
    jne .fs
    mov bx, [gr_win]
    call OSAPI_WM_CONTENT
    mov [gr_left], ax
    mov [gr_top], dx
    call OSAPI_WM_GEOM
    mov [gr_width], cx
    mov [gr_height], dx
    mov bx, [gr_win]
    call OSAPI_WM_DISPLAY
    mov [gr_kind], dl
    mov [gr_depth], dh
    jmp short .scale
.fs:
    cmp byte [gr_vga], 0
    je .cga
    xor ax, ax
    xor bx, bx
    mov cx, 640
    mov dx, 480
    mov byte [gr_depth], 4
    mov byte [gr_kind], VID_VGA
    jmp short .box
.cga:
    cmp byte [gr_cga], 0
    je .surface
    xor ax, ax
    xor bx, bx
    mov cx, 320
    mov dx, 200
    mov byte [gr_depth], 2
    mov byte [gr_kind], VID_CGA
    jmp short .box
.surface:
    xor bx, bx
    call OSAPI_FSX_CAPS
    mov [gr_kind], dl
    mov byte [gr_depth], 4
    cmp dl, VID_HERC
    jne .surf
    mov byte [gr_depth], 1
.surf:
    call OSAPI_FSX_SURF
.box:
    mov [gr_left], ax
    mov [gr_top], bx
    mov [gr_width], cx
    mov [gr_height], dx
.scale:
    mov word [gr_sx], 1
    cmp word [gr_width], 520
    jb .vertical
    mov word [gr_sx], 2
.vertical:
    mov word [gr_sy], 1
    cmp byte [gr_kind], VID_CGA
    je .center
    cmp word [gr_height], 256
    jb .center
    mov word [gr_sy], 2
    cmp byte [gr_kind], VID_VGA
    jne .center
    cmp word [gr_height], 384
    jb .center
    mov word [gr_sy], 3
.center:
    mov ax, [gr_sx]
    mov cx, 256
    mul cx
    mov bx, [gr_width]
    sub bx, ax
    shr bx, 1
    add bx, [gr_left]
    add bx, 7
    and bx, 0fff8h             ; byte-aligned 1bpp destination
    mov [gr_ox], bx
    mov ax, [gr_sy]
    mov cx, 128
    mul cx
    mov bx, [gr_height]
    sub bx, ax
    shr bx, 1
    add bx, [gr_top]
    mov [gr_oy], bx
    ret

; Scene primitives. x=CX, y=DX, colour=AL; out-of-bounds pixels are ignored.
gr_put:
    push bx
    push cx
    push dx
    push ax
    cmp cx, 256
    jae .out
    cmp dx, 128
    jae .out
    mov bx, dx
    push cx
    mov cl, 7
    shl bx, cl
    pop cx
    mov dx, cx
    shr dx, 1
    add bx, dx
    test cl, 1
    jnz .odd
    mov cl, 4
    shl al, cl
    and byte [gr_scene+bx], 0fh
    jmp short .ink
.odd:
    and byte [gr_scene+bx], 0f0h
.ink:
    or [gr_scene+bx], al
.out:
    pop ax
    pop dx
    pop cx
    pop bx
    ret

gr_get:
    push bx
    push cx
    push dx
    mov bx, dx
    push cx
    mov cl, 7
    shl bx, cl
    pop cx
    mov dx, cx
    shr dx, 1
    add bx, dx
    mov al, [gr_scene+bx]
    test cl, 1
    jnz .low
    mov cl, 4
    shr al, cl
.low:
    and al, 15
    pop dx
    pop cx
    pop bx
    ret

; Inclusive model rectangle (CX,DX)..(SI,DI), colour AL.
gr_rect:
    SAVE
    cmp cx, 256
    jae .out
    cmp dx, 128
    jae .out
    cmp si, 255
    jbe .right
    mov si, 255
.right:
    cmp di, 127
    jbe .bottom
    mov di, 127
.bottom:
    cmp si, cx
    jb .out
    cmp di, dx
    jb .out
    sub di, dx
    inc di
    push di                      ; row count
    mov bp, dx
    push cx
    mov cl, 7
    shl bp, cl
    pop cx
    xor bx, bx
    test cl, 1
    jz .left
    or bl, 1                     ; left edge owns only the low nibble
.left:
    test si, 1
    jnz .edges
    or bl, 2                     ; right edge owns only the high nibble
.edges:
    inc cx
    shr cx, 1
    shr si, 1
    sub si, cx
    test bl, 2
    jnz .width
    inc si
.width:
    ; A single even-x pixel has no complete bytes; its right edge handles it.
    mov dx, cx
    add dx, bp
    add dx, gr_scene
    mov bp, si                   ; complete packed bytes per row
    mov ah, al
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1
    or al, ah
    mov ah, al
    push ds
    pop es
    pop si                       ; rows
.row:
    mov di, dx
    test bl, 1
    jz .fill
    and byte [di-1], 0f0h
    and al, 15
    or [di-1], al
    mov al, ah
.fill:
    mov cx, bp
    rep stosb
    test bl, 2
    jz .next
    and byte [di], 15
    and al, 0f0h
    or [di], al
    mov al, ah
.next:
    add dx, 128
    dec si
    jnz .row
.out:
    RESTORE
    ret

gr_rand:
    mov ax, [gr_seed]
    mov dx, 25173
    mul dx
    add ax, 13849
    mov [gr_seed], ax
    ret

gr_new:
    mov word [gr_scores], 0
    mov byte [gr_turn], 0
    mov byte [gr_paused], 0
    mov word [gr_angles], 45
    mov word [gr_angles+2], 45
    mov word [gr_powers], 70
    mov word [gr_powers+2], 70
    jmp gr_city

; Restore the active player's last throw (AX/BX clobbered).
gr_recallaim:
    xor bx, bx
    mov bl, [gr_turn]
    shl bx, 1
    mov ax, [gr_angles+bx]
    mov [gr_angle], ax
    mov ax, [gr_powers+bx]
    mov [gr_power], ax
    ret

gr_city:
    push ds
    pop es
    mov di, gr_scene
    xor ax, ax
    mov cx, 8192
    rep stosw
    ; The scene was cleared: invalidate the HUD character cache too.
    mov di, gr_hudchars
    mov cx, 512/2
    rep stosw
    mov word [gr_aipower], 0
    mov byte [gr_state], 0
    mov byte [gr_saved], 0
    mov byte [gr_sunhit], 0
    mov byte [gr_insun], 0
    mov byte [gr_dancing], 0
    mov byte [gr_blast], 0
    mov byte [gr_throwticks], 0
    mov byte [gr_roundwait], 0
    mov byte [gr_field], 0
    mov byte [gr_edit], 0
    call gr_recallaim
    call gr_rand
    xor dx, dx
    mov ax, 10
    call gr_random
    sub ax, 4
    mov [gr_wind], ax
    mov ax, 3
    call gr_random
    or ax, ax
    jnz .winddone
    mov ax, 10
    call gr_random
    inc ax
    cmp word [gr_wind], 0
    jg .gust
    neg ax
.gust:
    add [gr_wind], ax
.winddone:
    mov ax, 5
    call gr_random
    add ax, 8
    mov [gr_buildings], ax
    mov ax, 6
    call gr_random
    mov [gr_profile], al
    ; Eight to twelve lots, each 18..36 pixels including two gutters.
    xor bp, bp
    xor cx, cx
.building:
    mov bx, bp
    mov [gr_lots+bx], cl
    call gr_lotwidth
    mov [gr_widths+bx], al
    mov si, cx
    add si, ax
    dec si
    dec si
    inc cx
    ; Broad random variation around an overall skyline trend.
    push cx
    mov ax, bp
    mov bx, 4
    mul bx
    mov bx, ax
    mov ax, [gr_buildings]
    shl ax, 1
    cmp byte [gr_profile], 0
    je .rising
    cmp byte [gr_profile], 1
    je .falling
    sub bx, ax
    jns .distance
    neg bx
.distance:
    cmp byte [gr_profile], 5
    jne .falling
    sub ax, bx
    mov bx, ax
    jmp short .falling
.rising:
    shl ax, 1
    sub ax, bx
    mov bx, ax
.falling:
    mov ax, 30
    call gr_random
    add ax, bx
    add ax, 48
    cmp ax, 103
    jbe .roof
    mov ax, 103
.roof:
    mov bx, bp
    mov [gr_roofs+bx], al
    mov dx, ax
    mov di, 115
    mov ax, 3
    call gr_random
    add al, 5
    mov [gr_colors+bx], al
    pop cx
    call gr_rect
    mov di, dx
    mov al, 12                 ; one-pixel rooftop coping
    call gr_rect
    add dx, 4
.windows:
    push cx
    add cx, 4
.window:
    push dx
    call gr_rand
    and ah, 3
    mov al, 14
    jnz .lit
    mov al, 8
.lit:
    pop dx
    push si
    push di
    mov si, cx
    inc si
    mov di, dx
    add di, 2
    call gr_rect
    pop di
    pop si
    add cx, 6
    cmp cx, si
    jb .window
    pop cx
    add dx, 6
    cmp dx, 114
    jb .windows
    mov cx, si
    add cx, 2
    inc bp
    cmp bp, [gr_buildings]
    jb .building
    ; Reference placement: second or third roof from either edge.
    mov ax, 2
    call gr_random
    inc ax
    mov si, ax
    mov ax, 2
    call gr_random
    add ax, 2
    mov di, [gr_buildings]
    sub di, ax
    mov bx, si
    xor bp, bp
    call gr_place
    mov bx, di
    mov bp, 2
    call gr_place
    xor bp, bp
    call gr_gorilla
    mov bp, 2
    call gr_gorilla
    call gr_sun
    call gr_windarrow
    call gr_hud
    ret

; CX=lot start, BP=lot index. Return AX=width, preserving other registers.
; Clamp width so all remaining lots can still fit in 18..36 pixels.
gr_lotwidth:
    push bx
    push dx
    push si
    push di
    mov bx, [gr_buildings]
    dec bx
    sub bx, bp
    mov ax, 18
    mul bx
    mov di, 256
    sub di, cx
    sub di, ax                 ; maximum width leaving 18 for each later lot
    mov ax, 36
    mul bx
    mov si, 256
    sub si, cx
    sub si, ax                 ; minimum width leaving at most 36 for each
    call gr_rand
    xor dx, dx
    mov bx, 19
    div bx
    mov ax, dx
    add ax, 18
    cmp ax, di
    jbe .minimum
    mov ax, di
.minimum:
    cmp ax, si
    jge .done                  ; minimum may be negative for early lots
    mov ax, si
.done:
    pop di
    pop si
    pop dx
    pop bx
    ret

; Center a 16-pixel gorilla on lot BX; BP selects the player (0 or 2).
gr_place:
    xor ax, ax
    mov al, [gr_widths+bx]
    sub ax, 16
    shr ax, 1
    add al, [gr_lots+bx]
    mov [ds:gr_gx+bp], ax
    xor ax, ax
    mov al, [gr_roofs+bx]
    sub ax, 20
    mov [ds:gr_gy+bp], ax
    ret

; Packed 16x20 art. Zero is transparent sky; 1/10/11 are the orange
; body, highlights and shadows. Facial gaps expose the blue background.
gr_gorilla:
    mov si, gr_ape
gr_gorillapose:
    mov dx, [ds:gr_gy+bp]
    mov di, 20
.row:
    mov cx, [ds:gr_gx+bp]
    push di
    mov di, 8
.pixel:
    lodsb
    mov ah, al
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    or al, al
    jz .right
    call gr_put
.right:
    inc cx
    mov al, ah
    and al, 15
    jz .next
    call gr_put
.next:
    inc cx
    dec di
    jnz .pixel
    pop di
    inc dx
    dec di
    jnz .row
    ret

; 25x21 sun: eight rays, circular body and smile, like DoSun in the BASIC.
; Art rows share the scene's ink 3, which collision treats as decoration.
gr_sun:
    mov si, gr_sunart
    mov dx, 25
    mov bp, 21
.row:
    mov cx, 116
    mov di, 25
.pixel:
    lodsb
    or al, al
    jz .next
    mov al, 3
    call gr_put
.next:
    inc cx
    dec di
    jnz .pixel
    inc dx
    dec bp
    jnz .row
    ret

; Only the mouth changes. Keep nearby gorillas and the ray tips intact.
gr_sunface:
    mov si, gr_sunart + 11*25 + 8
    mov dx, 36
    mov bp, 6
.row:
    mov cx, 124
    mov di, 9
.pixel:
    lodsb
    or al, al
    jz .put
    mov al, 3
.put:
    call gr_put
    inc cx
    dec di
    jnz .pixel
    add si, 16
    inc dx
    dec bp
    jnz .row
    cmp byte [gr_sunhit], 0
    je .out
    ; Fill the smile, then cut an oval mouth out of the yellow face.
    mov cx, 124
    mov dx, 37
    mov si, 132
    mov di, 39
    mov al, 3
    call gr_rect
    mov cx, 127
    mov dx, 36
    mov si, 129
    mov di, 40
    xor al, al
    call gr_rect
    mov cx, 126
    mov dx, 37
    mov si, 130
    mov di, 39
    call gr_rect
.out:
    ret

; The banana has already been restored before changing the sun's face.
; Preserve the swept collision coordinates and remaining substeps.
gr_sunpaint:
    SAVE
    call gr_sunface
    mov ax, 120
    mov bx, 36
    mov cx, 16
    mov dx, 6
    call gr_blit
    RESTORE
    ret

gr_sunreset:
    cmp byte [gr_sunhit], 0
    je .out
    mov byte [gr_sunhit], 0
    call gr_sunpaint
.out:
    ret

; Opaque HUD cells, still mirrored into the scene for exposure/full paints.
; Compare characters before touching pixels. Each glyph row expands four
; pixels at a time through a tiny table; no per-pixel gr_put calls.
; SI=NUL text, CX=0, DX=logical row (0,8,16), AL=ink (15 or 9).
gr_text:
    mov bp, 32
; CX may start within a row; BP is the number of cells to compare.
gr_textspan:
    SAVE
    mov [gr_textx], cx
    shl bp, 1
    shl bp, 1
    shl bp, 1
    add bp, cx
    mov [gr_textend], bp
    mov ah, al
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1
    or al, ah
    mov ah, al
    mov [gr_textmask], ax
    mov [gr_texty], dx
    shr cx, 1
    shr cx, 1
    shr cx, 1
    mov bx, dx
    shl bx, 1
    shl bx, 1
    add bx, cx
    mov [gr_textcell], bx
    mov ax, [gr_fontseg]
    mov es, ax
.char:
    ; Pad the whole line so a shorter message erases the previous suffix.
    mov al, ' '
    cmp byte [si], 0
    je .compare
    lodsb
.compare:
    mov bx, [gr_textcell]
    cmp al, [gr_hudchars+bx]
    je .next
    mov [gr_hudchars+bx], al
    mov byte [gr_huddirty+bx], 1
    cmp al, ' '
    jne .glyph
    ; A deleted cell is eight zero dwords, without font/table lookups.
    mov di, [gr_texty]
    mov cl, 7
    shl di, cl
    mov ax, [gr_textx]
    shr ax, 1
    add di, ax
    xor ax, ax
%assign row 0
%rep 8
    mov [gr_scene+di+row*128], ax
    mov [gr_scene+di+row*128+2], ax
%assign row row+1
%endrep
    jmp .next
.glyph:
    sub al, [gr_fontfirst]
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov bp, [gr_fontoff]
    add bp, ax
    mov di, [gr_texty]
    mov cl, 7
    shl di, cl
    mov ax, [gr_textx]
    shr ax, 1
    add di, ax
    add di, gr_scene
    mov cx, 8
.row:
    mov al, [es:bp]
    inc bp
    xor ah, ah
    mov bx, ax
    and bx, 15
    shl bx, 1
    mov dx, [gr_textnibbles+bx]
    and dx, [gr_textmask]
    mov [di+2], dx
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    mov bx, ax
    shl bx, 1
    mov ax, [gr_textnibbles+bx]
    and ax, [gr_textmask]
    mov [di], ax
    add di, 128
    loop .row
.next:
    inc word [gr_textcell]
    add word [gr_textx], 8
    mov ax, [gr_textx]
    cmp ax, [gr_textend]
    jb .char
    RESTORE
    ret

; Three decimal columns at DI, AX=0..999.
gr_number:
    push ax
    push bx
    push dx
    mov bx, 10
    xor dx, dx
    div bx
    add dl, '0'
    mov [di+2], dl
    xor dx, dx
    div bx
    add dl, '0'
    mov [di+1], dl
    add al, '0'
    mov [di], al
    pop dx
    pop bx
    pop ax
    ret

gr_dirtyclear:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    mov di, gr_huddirty
    xor ax, ax
    mov cx, 96/2
    rep stosw
    pop es
    pop di
    pop cx
    pop ax
    ret

gr_hud:
    call gr_dirtyclear
    xor cx, cx
    xor dx, dx
    cmp byte [gr_state], 1
    jne .visible
    ; Clear all three rows once at launch. Cached spaces leave the banana
    ; untouched if pause/resume asks for another HUD update during flight.
    mov si, gr_blank
    mov al, 15
    call gr_text
    mov dx, 8
    call gr_text
    mov dx, 16
    call gr_text
    ret
.visible:
    call gr_scoreheader
    mov si, gr_status
    mov al, 15
    call gr_text
    mov ax, [gr_wind]
    mov byte [gr_prompt+28], '+'
    or ax, ax
    jns .wind
    neg ax
    mov byte [gr_prompt+28], '-'
.wind:
    mov di, gr_prompt+29
    call gr_number
    cmp byte [gr_state], 0
    jne .state
    mov ax, [gr_angle]
    mov di, gr_prompt+8
    xor bx, bx
    call gr_shotnumber
    mov ax, [gr_power]
    mov di, gr_velocity+11
    mov bx, 2
    call gr_shotnumber
    mov byte [gr_prompt], ' '
    mov byte [gr_velocity], ' '
    mov si, gr_prompt
    cmp byte [gr_field], 0
    je .selected
    mov si, gr_velocity
.selected:
    mov byte [si], '>'
    mov si, gr_prompt
    jmp short .line
.state:
    mov si, gr_roundmsg
    cmp byte [gr_state], 2
    je .line
    mov si, gr_matchmsg
.line:
    cmp byte [gr_paused], 0
    je .write
    mov si, gr_pausemsg
.write:
    xor cx, cx
    mov dx, 8
    mov al, 9
    call gr_text
    mov si, gr_blank
    cmp byte [gr_state], 0
    jne .last
    cmp byte [gr_paused], 0
    jne .last
    mov si, gr_velocity
.last:
    mov dx, 16
    call gr_text
    ret

; Numeric edits format and compare only the active seven-cell field.
; Paused input still changes the model; resume rebuilds the visible prompt.
; AX=new value, BX=word offset (0 angle, 2 velocity).
gr_inputpaint:
    mov si, gr_prompt+8
    mov cx, 64
    mov dx, 8
    or bx, bx
    jz .number
    mov si, gr_velocity+11
    mov cx, 88
    mov dx, 16
.number:
    mov di, si
    call gr_shotnumber
    cmp byte [gr_paused], 0
    jne .done
    call gr_dirtyclear
    mov al, 9
    mov bp, 7
    call gr_textspan
    mov si, 40
    cmp byte [gr_field], 0
    je .flush
    mov si, 75
.flush:
    mov di, si
    add di, 7
    jmp gr_hudflushspan
.done:
    ret

gr_selectpaint:
    mov byte [gr_prompt], ' '
    mov byte [gr_velocity], ' '
    mov si, gr_prompt
    cmp byte [gr_field], 0
    je .selected
    mov si, gr_velocity
.selected:
    mov byte [si], '>'
    cmp byte [gr_paused], 0
    jne .done
    call gr_dirtyclear
    mov si, gr_prompt
    xor cx, cx
    mov dx, 8
    mov al, 9
    mov bp, 1
    call gr_textspan
    mov si, gr_velocity
    mov dx, 16
    call gr_textspan
    mov si, 32
    mov di, 65
    jmp gr_hudflushspan
.done:
    ret

gr_hudpaint:
    call gr_hud
    xor si, si
    mov di, 96
gr_hudflushspan:
    ; Coalesce adjacent changed cells, never crossing a text row. In
    ; particular Tab draws two one-cell spans, not the labels between them.
.scan:
    cmp byte [gr_huddirty+si], 0
    je .next
    mov ax, si
    and ax, 31
    mov cl, 3
    shl ax, cl
    mov bx, si
    and bx, 0ffe0h
    shr bx, 1
    shr bx, 1
    xor cx, cx
.run:
    mov byte [gr_huddirty+si], 0
    add cx, 8
    inc si
    test si, 31
    jz .draw
    cmp si, di
    jae .draw
    cmp byte [gr_huddirty+si], 0
    jne .run
.draw:
    call gr_hudblit
    jmp short .more
.next:
    inc si
.more:
    cmp si, di
    jb .scan
    ret

; AX=x, BX=y, CX=width, all in logical pixels. Compose the changed text
; straight from the font as a 1bpp band (2bpp for foreign CGA). The general
; scene scaler/colour converter is deliberately absent from this hot path.
gr_hudblit:
    SAVE
    mov [gr_rx], ax
    mov [gr_ry], bx
    mov [gr_rw], cx
    shr ax, 1
    shr ax, 1
    shr ax, 1
    shl bx, 1
    shl bx, 1
    add ax, bx
    mov si, gr_hudchars
    add si, ax
    shr cx, 1
    shr cx, 1
    shr cx, 1
    mov ax, [gr_sx]
    cmp byte [gr_cga], 0
    je .bytes
    mov ax, 2                 ; each CGA glyph bit becomes 00 or 11
.bytes:
    mov [gr_cellbytes], ax
    mul cx
    mov [gr_stride], ax
    ; Erasing a whole span (old input/error/help) needs one zero fill.
    push si
    push cx
.blankcheck:
    lodsb
    cmp al, ' '
    jne .glyphs
    loop .blankcheck
    pop cx
    pop si
    mov ax, [gr_stride]
    mul word [gr_sy]
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov cx, ax
    push ds
    pop es
    mov di, gr_band
    xor ax, ax
    rep stosb
    jmp .composed
.glyphs:
    pop cx
    pop si
    mov ax, [gr_fontseg]
    mov es, ax
    mov di, gr_band
.cell:
    lodsb
    sub al, [gr_fontfirst]
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov bp, [gr_fontoff]
    add bp, ax
    push cx
    push di
    mov dx, 8
.row:
    mov al, [es:bp]
    inc bp
    cmp word [gr_cellbytes], 1
    je .single
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov ax, [gr_textdouble+bx]
    mov cx, [gr_sy]
.double:
    mov [di], ax
    add di, [gr_stride]
    loop .double
    jmp short .nextrow
.single:
    mov cx, [gr_sy]
.repeat:
    mov [di], al
    add di, [gr_stride]
    loop .repeat
.nextrow:
    dec dx
    jnz .row
    pop di
    add di, [gr_cellbytes]
    pop cx
    loop .cell
.composed:
    push ds
    pop es
    cmp byte [gr_vga], 0
    jne .coords
    cmp byte [gr_cga], 0
    jne .coords
    mov ax, (CBLUE << 8) | CWHITE
    call OSAPI_GFX_BLIT1_PEN
.coords:
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
    mov ax, 8
    mul word [gr_sy]
    mov dx, ax
    pop ax
    mov bp, [gr_stride]
    mov si, gr_band
    cmp byte [gr_vga], 0
    jne .vga
    cmp byte [gr_cga], 0
    jne .cga
    call OSAPI_GFX_BLIT1
    jnc .done
    ; Keep a clipped scene fallback if the kernel refuses a band.
    mov ax, [gr_rx]
    mov bx, [gr_ry]
    mov cx, [gr_rw]
    mov dx, 8
    call gr_blit
    jmp short .done
.cga:
    call gr_cgablit
    jmp short .done
.vga:
    call gr_vgatext
.done:
    RESTORE
    ret

; Foreign VGA HUD: logical sky is palette index 0, text is 9 or 15.
; Write the glyph to all ink planes together, then zero the other planes.
; This avoids converting packed 4bpp back to four identical glyph planes.
; AX/BX=physical x/y, DX=height, BP=1bpp stride, DS:SI=band.
gr_vgatext:
    mov [gr_vgheight], dx
    shr ax, 1
    shr ax, 1
    shr ax, 1
    mov di, ax
    mov ax, bx
    mov cx, 80
    mul cx
    add di, ax
    mov [gr_vgdest], di
    mov ax, 0a000h
    mov es, ax
    mov ax, 0f02h
    cmp word [gr_ry], 8
    je .inputink
    cmp word [gr_ry], 16
    jne .ink
    cmp byte [gr_state], 4
    jae .ink                 ; startup titles on this row stay white
.inputink:
    mov ah, 9
.ink:
    mov dx, 03c4h
    out dx, ax
    mov bx, [gr_vgheight]
.row:
    mov cx, bp
    rep movsb
    add di, 80
    sub di, bp
    dec bx
    jnz .row
    xor ah, 15
    jz .done
    out dx, ax
    mov di, [gr_vgdest]
    mov bx, [gr_vgheight]
    xor ax, ax
.clear:
    mov cx, bp
    rep stosb
    add di, 80
    sub di, bp
    dec bx
    jnz .clear
.done:
    ret

 ; Reference equations in a Q6 scene, shared with computer prediction.
gr_fire:
    mov byte [gr_state], 1
    mov byte [gr_saved], 0
    mov word [gr_age], 0
    mov ax, [gr_power]
    mov di, ax
    xor bx, bx
    mov bl, [gr_turn]
    shl bx, 1
    mov bp, bx
    mov si, gr_px
    mov [gr_powers+bx], ax
    mov ax, [gr_angle]
    mov [gr_angles+bx], ax
    call gr_initmotion
    call gr_throwpose
    mov si, gr_musicthrow
    jmp gr_musicstart

gr_tick:
    cmp byte [gr_abon], 0
    jne .out
    cmp byte [gr_paused], 0
    jne .out
    call gr_musictick
    cmp byte [gr_blast], 0
    jne gr_blasttick
    cmp byte [gr_throwticks], 0
    jne gr_throwtick
    cmp byte [gr_dancing], 0
    jne gr_victorytick
    cmp byte [gr_state], 4
    jae gr_fronttick
    cmp byte [gr_state], 0
    je gr_aitick
    cmp byte [gr_state], 1
    jne gr_roundtick
    call gr_unbanana
    inc word [gr_age]
    mov si, gr_px
    call gr_framebegin
    mov bp, [gr_steps]
.step:
    mov si, gr_px
    call gr_motionstep
    mov cl, 6                   ; shift through AX; CX is coordinate below
    mov ax, [gr_px]
    sar ax, cl
    mov bx, [gr_py]
    shr bx, cl
    mov cx, ax
    mov dx, bx
    cmp cx, 256
    jae .miss
    cmp word [gr_pyhi], 0
    jl .next                    ; signed 32-bit height: high lunar arcs
    jg .miss
    cmp dx, 116
    jge .miss
    cmp dx, 24
    jl .next                    ; flight above scene is legal
    ; Bound the lookup, then test actual gorilla ink, including self-hits.
    xor si, si
.ape:
    mov ax, cx
    sub ax, [gr_gx+si]
    cmp ax, 16
    jae .other
    mov ax, dx
    sub ax, [gr_gy+si]
    cmp ax, 20
    jae .other
    call gr_get
    cmp al, 1
    je .hitape
    cmp al, 10
    je .hitape
    cmp al, 11
    je .hitape
    ; Sky between arms/legs is passable, as in POINT-based BASIC.
    jmp short .next
.other:
    add si, 2
    cmp si, 4
    jb .ape
    ; Sun ink triggers surprise, but never stops the projectile.
    call gr_get
    cmp al, 3
    jne .solid
    mov byte [gr_insun], 1
    cmp byte [gr_sunhit], 0
    jne .next
    mov byte [gr_sunhit], 1
    call gr_sunpaint
    jmp short .next
.solid:
    or al, al
    jnz .terrain
.next:
    dec bp
    jnz .step
    mov si, gr_px
    xor bx, bx
    call gr_accelerate
    call gr_banana
.out:
    ret
.miss:
    call gr_sunreset
    xor byte [gr_turn], 1
    call gr_recallaim
    mov word [gr_aipower], 0
    mov byte [gr_state], 0
    mov byte [gr_field], 0
    mov byte [gr_edit], 0
    jmp gr_hudpaint
.terrain:
    mov al, 7
    call gr_blaststart
    mov si, gr_musicimpact
    jmp gr_musicstart
.hitape:
    mov cx, [gr_gx+si]
    add cx, 8
    mov dx, [gr_gy+si]
    add dx, 10
    shr si, 1
    xor si, 1                  ; opponent scores even on a self-hit
    mov ax, si
    mov [gr_winner], al
    xor byte [gr_turn], 1
    inc byte [gr_scores+si]
    mov byte [gr_state], 2
    mov al, [gr_scores+si]
    cmp al, [gr_target]
    jb .explode
    mov byte [gr_state], 3
.explode:
    mov al, 18
    call gr_blaststart
    mov si, gr_musichit
    jmp gr_musicstart

; VictoryDance: four left/right pairs, each with PLAY EFGEFDC and Rest .2.
; Each phrase includes its rest; pose changes wait for the sequencer, so
; rendering, pause and focus changes cannot run the dance ahead of the tune.
gr_victorytick:
    cmp word [gr_musicptr], 0
    jne .out
    dec byte [gr_dancing]
    jnz .pose
    mov byte [gr_roundwait], 18
    ret
.pose:
    xor bx, bx
    mov bl, [gr_winner]
    shl bx, 1
    mov bp, bx
    mov cx, [gr_gx+bx]
    mov dx, [gr_gy+bx]
    mov si, cx
    add si, 15
    mov di, dx
    add di, 19
    xor al, al
    call gr_rect
    mov si, gr_apeleft
    test byte [gr_dancing], 1
    jz .draw
    mov si, gr_aperight
.draw:
    call gr_gorillapose
    ; Gorillas need not start on an eight-pixel blit boundary.
    mov ax, [ds:gr_gx+bp]
    and ax, 0fff8h
    mov bx, [ds:gr_gy+bp]
    mov cx, 24
    mov dx, 20
    call gr_blit
    mov si, gr_musicvictory
    call gr_musicstart
.out:
    ret

; Save/restore an aligned 16x8 scene patch. The extra byte column lets a
; banana cross a byte boundary without snapping its visible x coordinate.
gr_patchptr:
    mov ax, [gr_by]
    mov cl, 7
    shl ax, cl
    mov si, [gr_bx]
    shr si, 1
    add si, ax
    add si, gr_scene
    ret

gr_unbanana:
    cmp byte [gr_saved], 0
    je .out
    call gr_patchptr
    mov di, si
    mov si, gr_under
    push ds
    pop es
    mov bp, 8
.row:
    movsw
    movsw
    movsw
    movsw
    add di, 120
    dec bp
    jnz .row
    mov byte [gr_saved], 0
    mov ax, [gr_bx]
    mov bx, [gr_by]
    mov cx, 16
    mov dx, 8
    call gr_blit
.out:
    ret

gr_banana:
    cmp word [gr_pyhi], 0
    jne .out
    mov cl, 6
    mov ax, [gr_px]
    sar ax, cl
    mov dx, [gr_py]
    sar dx, cl
    cmp ax, 249
    ja .out
    cmp dx, 0
    jl .out
    cmp dx, 120
    ja .out
    cmp byte [gr_insun], 0
    je .visible
    cmp ax, 113
    jb .leavesun
    cmp ax, 141
    ja .leavesun
    cmp dx, 46
    jb .out
.leavesun:
    mov byte [gr_insun], 0
.visible:
    and ax, 0fff8h
    cmp ax, 240
    jbe .patch
    mov ax, 240
.patch:
    mov [gr_bx], ax
    mov [gr_by], dx
    call gr_patchptr
    push ds
    pop es
    mov di, gr_under
    mov bp, 8
.row:
    movsw
    movsw
    movsw
    movsw
    add si, 120
    dec bp
    jnz .row
    mov byte [gr_saved], 1
    mov ax, [gr_px]
    mov cl, 6
    shr ax, cl
    mov cx, ax
    mov dx, [gr_by]
    mov bx, [gr_age]
    and bx, 3
    mov si, bx
    shl bx, 1
    shl bx, 1
    shl bx, 1
    sub bx, si
    add bx, gr_bananas
    mov bp, 7
.banrow:
    mov ah, [bx]
    inc bx
    mov si, 7
    push cx
.banpixel:
    shl ah, 1
    jnc .skip
    mov al, 14
    call gr_put
.skip:
    inc cx
    dec si
    jnz .banpixel
    pop cx
    inc dx
    dec bp
    jnz .banrow
    mov ax, [gr_bx]
    mov bx, [gr_by]
    mov cx, 16
    mov dx, 8
    call gr_blit
.out:
    ret

; Full content (including borders around the centered logical scene).
gr_fullpaint:
    cmp byte [gr_state], 4
    jae gr_frontpaint
    cmp byte [gr_vga], 0
    jne .scene
    cmp byte [gr_cga], 0
    jne .scene
    call gr_margins
.scene:
    ; Between shots the three HUD rows are opaque font bands. Reuse the
    ; incremental text path instead of converting them through four planes.
    ; During flight the banana can occupy these rows, so use the scene.
    cmp byte [gr_state], 1
    je .city
    xor ax, ax
    xor bx, bx
    mov cx, 256
.hudrow:
    call gr_hudblit
    add bx, 8
    cmp bx, 24
    jb .hudrow
.city:
    xor ax, ax
.strip:
    xor bx, bx
    mov cx, 32
    mov dx, 128
    cmp byte [gr_state], 1
    je .draw
    mov bx, 24
    mov dx, 104
.draw:
    call gr_blit
    add ax, 32
    cmp ax, 256
    jb .strip
    ret

; Full scene bands are opaque: clear only the four surrounding margins.
; Clearing the city first both doubles the writes and flashes a blank frame.
gr_margins:
    mov al, CBLACK
    cmp byte [gr_depth], 4
    jne .pen
    mov al, CBLUE
.pen:
    call OSAPI_SET_COLOR
    mov ax, [gr_left]
    mov cx, ax
    add cx, [gr_width]
    dec cx
    mov bx, [gr_top]
    mov dx, [gr_oy]
    dec dx
    call .fill
    mov bx, [gr_sy]
    mov dx, 128
    push ax
    mov ax, bx
    mul dx
    add ax, [gr_oy]
    mov bx, ax
    pop ax
    mov dx, [gr_top]
    add dx, [gr_height]
    dec dx
    call .fill
    mov bx, [gr_oy]
    mov dx, [gr_sy]
    push cx
    mov cl, 7
    shl dx, cl
    pop cx
    add dx, bx
    dec dx
    mov cx, [gr_ox]
    dec cx
    call .fill
    xor ax, ax
    mov ah, [gr_sx]           ; sx*256 (sx is 1 or 2)
    add ax, [gr_ox]
    mov cx, [gr_left]
    add cx, [gr_width]
    dec cx
.fill:
    cmp ax, cx
    jg .out
    cmp bx, dx
    jg .out
    call OSAPI_GFX_FILL
.out:
    ret

gr_background:
    mov al, CBLACK
    cmp byte [gr_depth], 4
    jne .background
    mov al, CBLUE
.background:
    call OSAPI_SET_COLOR
    mov ax, [gr_left]
    mov bx, [gr_top]
    mov cx, ax
    add cx, [gr_width]
    dec cx
    mov dx, bx
    add dx, [gr_height]
    dec dx
    call OSAPI_GFX_FILL
    ret

; Render AX=x (8-aligned), BX=y, CX=width (8-aligned), DX=height.
; Native ink-pair tables combine palette mapping, scaling and bit packing.
; Convert logical rows once, then duplicate native bytes vertically.
; Up to 32 rows for strips <=56 pixels wide, or 7 rows for wider bands.
; At sx=2, sy=3, either path needs at most 5376 scratch bytes.
gr_blit:
    SAVE
    push ds
    pop es
    cmp bx, 128
    jae .done
    cmp ax, 256
    jae .done
    or cx, cx
    jz .done
    or dx, dx
    jz .done
    mov [gr_rx], ax
    mov [gr_ry], bx
    add cx, ax
    cmp cx, 256
    jbe .right
    mov cx, 256
.right:
    sub cx, ax
    mov [gr_rw], cx
    add dx, bx
    cmp dx, 128
    jbe .bottom
    mov dx, 128
.bottom:
    mov [gr_endy], dx
    sub dx, bx
    mov [gr_rh], dx
    mov byte [gr_planar], 0
    mov byte [gr_reclip], 0
    mov byte [gr_pairbits], 4
    mov word [gr_frontsource], gr_band
    mov ax, cx
    mul word [gr_sx]
    shr ax, 1
    mov [gr_stride], ax
    cmp byte [gr_vga], 0
    jne .native
    cmp byte [gr_depth], 2
    je .packed
    ; The OS planar API refuses an armed region. Only disarm after proving
    ; the whole requested rectangle drawable, and restore even on refusal.
    call gr_frontcoords
    add cx, ax
    dec cx
    add dx, bx
    dec dx
    call OSAPI_WM_CLIP_TEST
    jnc .visible
    mov byte [gr_pairbits], 0   ; packed fallback also preserves mono edge pixels
    jmp .bands
.visible:
    cmp byte [gr_depth], 1
    je .packed
    call OSAPI_WM_CLIP_CLEAR
    mov byte [gr_reclip], 1
    call gr_frontcoords
    mov di, 8000h
    call OSAPI_GFX_BLITP
    jc .bands
    mov bx, gr_winplanes
    cmp word [gr_sx], 1
    je .planar
    mov bx, gr_winplanesx2
    jmp short .planar
.native:
    mov bx, gr_planebits
    cmp word [gr_sx], 1
    je .planar
    call gr_nativeprepare
    mov bx, gr_lightcache
.planar:
    mov byte [gr_planar], 1
    cmp word [gr_sx], 1
    jne .mono_stride
    mov byte [gr_pairbits], 2
    jmp short .mono_stride
.packed:
    mov bx, gr_cgapairs
    cmp byte [gr_depth], 2
    je .cga_stride
    mov bx, gr_monopairsx2
    cmp word [gr_sx], 1
    jne .mono_stride
    mov bx, gr_monopairs
    mov byte [gr_pairbits], 2
.mono_stride:
    shr word [gr_stride], 1
.cga_stride:
    shr word [gr_stride], 1
    mov [gr_pairtable], bx
.bands:
    mov ax, [gr_endy]
    sub ax, [gr_ry]
    mov bx, 7
    cmp word [gr_rw], 56
    ja .limit
    mov bx, 32
.limit:
    cmp ax, bx
    jbe .rows
    mov ax, bx
.rows:
    mov [gr_rh], ax
    mul word [gr_sy]
    mul word [gr_stride]
    mov [gr_planestep], ax
    ; Sky is ink zero. Clear an empty planar band with all planes selected
    ; together (or the OS's solid fill), avoiding four identical conversions.
    cmp byte [gr_planar], 0
    je .compose
    mov bp, [gr_rh]
    mov di, [gr_ry]
    mov cl, 7
    shl di, cl
    mov ax, [gr_rx]
    shr ax, 1
    add di, ax
    add di, gr_scene
    xor ax, ax
.skyrow:
    mov cx, [gr_rw]
    shr cx, 1
    shr cx, 1
    push di
    repe scasw
    pop di
    jne .compose
    add di, 128
    dec bp
    jnz .skyrow
    cmp byte [gr_vga], 0
    je .skynative
    mov di, gr_band
    mov cx, [gr_planestep]
    rep stosb
    call gr_frontcoords
    mov byte [gr_frontplane], 15
    call gr_frontvgaplane
    push ds
    pop es
    jmp .advance
.skynative:
    mov al, CBLUE
    call OSAPI_SET_COLOR
    call gr_frontcoords
    add cx, ax
    dec cx
    add dx, bx
    dec dx
    call OSAPI_GFX_FILL
    jmp .advance
.compose:
    mov di, gr_band
    mov bx, [gr_pairtable]
    mov [gr_worktable], bx
    mov byte [gr_planesleft], 1
    cmp byte [gr_planar], 0
    je .plane
    mov byte [gr_planesleft], 4
.plane:
    mov ax, [gr_rh]
    mov [gr_rows], ax
    mov ax, [gr_ry]
    mov cl, 7
    shl ax, cl
    mov si, [gr_rx]
    shr si, 1
    add si, ax
    add si, gr_scene
    call gr_musicservice
.row:
    push si
    mov [gr_rowstart], di
    ; Building windows repeat for three rows, blank facade rows for three
    ; more. Reuse the previous native row (sky too) within each band/plane.
    mov ax, [gr_rows]
    cmp ax, [gr_rh]
    je .changed
    push di
    mov di, si
    sub si, 128
    mov cx, [gr_rw]
    shr cx, 1
    shr cx, 1
    repe cmpsw
    pop di
    je .same
.changed:
    pop si
    push si
    cmp byte [gr_planar], 0
    jne .native_row
    cmp byte [gr_depth], 4
    je .color
    cmp byte [gr_pairbits], 0
    jne .native_row
    mov cx, [gr_rw]
    shr cx, 1
.monopacked:
    lodsb
    mov bx, gr_monopairs
    xlat
    xor ah, ah
    mov bx, ax
    cmp word [gr_sx], 1
    jne .monodouble
    mov al, [gr_monopacked+bx]
    stosb
    jmp short .mononext
.monodouble:
    shl bx, 1
    mov ax, [gr_monowords+bx]
    stosw
.mononext:
    loop .monopacked
    jmp .repeat
.color:
    ; Packed desktop fallback, for a covered/straddling window or refusal.
    mov cx, [gr_rw]
    shr cx, 1
    mov bx, gr_winmap
    cmp word [gr_sx], 1
    jne .color2
.color1:
    lodsb
    xlat
    stosb
    loop .color1
    jmp short .repeat
.color2:
    lodsb
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov ax, [gr_winx2+bx]
    stosw
    loop .color2
    jmp short .repeat
.native_row:
    mov bx, [gr_worktable]
    mov cx, [gr_stride]
    cmp byte [gr_pairbits], 2
    je .two
.four:
    lodsb
    xlat
    mov ah, al
    shl ah, 1
    shl ah, 1
    shl ah, 1
    shl ah, 1
    lodsb
    xlat
    or al, ah
    stosb
    loop .four
    jmp short .repeat
.two:
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
    loop .two
    jmp short .repeat
.same:
    mov si, di
    sub si, [gr_stride]
    mov cx, [gr_stride]
    rep movsb
.repeat:
    mov bx, [gr_sy]
    dec bx
    jz .nextrow
    mov si, [gr_rowstart]
.copy:
    mov cx, [gr_stride]
    rep movsb
    dec bx
    jnz .copy
.nextrow:
    pop si
    add si, 128
    dec word [gr_rows]
    jnz .row
    add word [gr_worktable], 256
    dec byte [gr_planesleft]
    jnz .plane
    call gr_frontcoords
    cmp byte [gr_vga], 0
    jne .vga
    cmp byte [gr_cga], 0
    jne .cga
    cmp byte [gr_planar], 0
    jne .osplanes
    cmp byte [gr_pairbits], 0
    je .blit4
    cmp byte [gr_depth], 1
    je .mono
.blit4:
    call OSAPI_GFX_BLIT4
    jmp short .advance
.mono:
    call OSAPI_GFX_BLIT1
    jmp short .advance
.osplanes:
    mov di, [gr_planestep]
    call OSAPI_GFX_BLITP
    jmp short .advance
.cga:
    call gr_cgablit
    jmp short .advance
.vga:
    mov byte [gr_frontplane], 1
.vgaplane:
    call gr_frontcoords
    call gr_frontvgaplane
    mov ax, [gr_planestep]
    add [gr_frontsource], ax
    shl byte [gr_frontplane], 1
    cmp byte [gr_frontplane], 16
    jb .vgaplane
    mov word [gr_frontsource], gr_band
    push ds
    pop es
.advance:
    mov ax, [gr_rh]
    add [gr_ry], ax
    mov ax, [gr_ry]
    cmp ax, [gr_endy]
    jb .bands
    cmp byte [gr_reclip], 0
    je .done
    mov bx, [gr_win]
    call OSAPI_WM_CLIP_SET
.done:
    RESTORE
    ret

; Fullscreen's doubled plane table shares the inactive intro cache. The
; high cache-key bit makes the next frontend paint rebuild its sprite cache.
gr_nativeprepare:
    cmp word [gr_cachekey], 8000h
    je .out
    mov word [gr_cachekey], 8000h
    mov si, gr_planebits
    mov di, gr_lightcache
    mov bx, gr_pairdouble
    mov cx, 1024
.byte:
    lodsb
    xlat
    stosb
    loop .byte
.out:
    ret
gr_pairdouble: db 0,3,12,15
gr_monopacked: db 0,15,240,255
gr_monowords: dw 0,0ff00h,00ffh,0ffffh

; VGA fullscreen owns a foreign mode. Set identity attribute indices, then
; load the original six-bit EGA colors converted to six-bit DAC components.
; The FSX mode restore resets ALL of this before desktop windows repaint.
gr_vgapalette:
    mov dx, 03dah
    in al, dx                    ; attribute flip-flop: index next
    mov dx, 03c0h
    xor ax, ax
.attr:
    out dx, al
    out dx, al
    inc al
    cmp al, 16
    jb .attr
    mov al, 20h
    out dx, al                   ; video enabled
    mov dx, 03c8h
    xor al, al
    out dx, al
    inc dx
    mov si, gr_dac
    mov cx, 48
.dac:
    lodsb
    out dx, al
    loop .dac
    mov dx, 03ceh
    mov ax, 0001h               ; disable set/reset
    out dx, ax
    mov ax, 0003h               ; no rotation / logical operation
    out dx, ax
    mov ax, 0005h               ; write mode zero
    out dx, ax
    mov ax, 0ff08h              ; every bit writable
    out dx, ax
    ret

; Foreign mode 4: 80 bytes per row, odd rows at +2000h. No kernel drawing
; slot is called in this mode. gr_exclusive explicitly selects palette 0.
gr_cgablit:
    push es
    mov di, 0b800h
    mov es, di
    shr ax, 1
    shr ax, 1
    mov [gr_cgax], ax
    mov [gr_cgarows], dx
    mov ax, bx
    and ax, 1
    mov cl, 13
    shl ax, cl
    mov di, ax
    mov ax, bx
    shr ax, 1
    mov cx, 80
    mul cx
    add di, ax
    add di, [gr_cgax]
.row:
    mov cx, bp
    rep movsb
    sub di, bp
    xor di, 2000h            ; alternate CGA banks, advance after odd rows
    test di, 2000h
    jnz .next
    add di, 80
.next:
    dec word [gr_cgarows]
    jnz .row
    pop es
    ret

gr_tpl: dw 40, 28, 530, 282, gr_title, gr_paint, gr_onkey, gr_click
OS88_PREFER gr_pref, 530, 282, 530, 282, 530, 150
OS88_MENUSET gr_menus, gr_title, gr_oncmd
    OS88_MENU gr_mgame, gr_items, 3
OS88_MENUSET_END gr_menus
gr_mgame: db 'Game',0
gr_items: dw gr_mnew, gr_mfull, gr_mpause
gr_mnew: db 'New Match',0
gr_mfull: db 'Full Screen',0
gr_mpause: db 'Pause',0
gr_title: db 'Gorillas',0
gr_ablines: dw gr_title, gr_credit, gr_credit2, gr_credit3, 0
gr_credit: db 'After QBasic Gorillas (1990)',0
gr_credit2: db 'Original: Microsoft Corporation',0
gr_credit3: db '8086 port for os8088',0
gr_status: db '               Score 000 - 000  ',0
gr_prompt: db ' Angle: 45             Wind +000',0
gr_velocity: db ' Velocity: 70     ',0
gr_blank: db 0
gr_roundmsg: db 'Point! Next skyline shortly',0
gr_matchmsg: db 'Match over! Final scores soon',0
gr_pausemsg: db 'Paused. P or Enter to resume',0
; Four font bits -> four opaque scene pixels, little-endian byte order.
gr_textnibbles:
%assign gr_n 0
%rep 16
    dw (((gr_n & 8) * 30) | ((gr_n & 4) * 15 / 4) | ((gr_n & 2) * 30720) | ((gr_n & 1) * 3840))
%assign gr_n gr_n+1
%endrep
; One glyph row -> doubled bits (also CGA colour 3 on background 0).
gr_textdouble:
%assign gr_n 0
%rep 256
%assign gr_bits 0
%assign gr_bit 0
%rep 8
%assign gr_bits gr_bits | (((gr_n >> gr_bit) & 1) * (3 << (gr_bit*2)))
%assign gr_bit gr_bit+1
%endrep
    dw ((gr_bits >> 8) | ((gr_bits & 255) << 8))
%assign gr_n gr_n+1
%endrep
%include "grart.inc"

; Q14 sine, full circle plus the interpolation endpoint.
gr_sin:
    dw 0,286,572,857,1143,1428,1713,1997,2280,2563,2845,3126
    dw 3406,3686,3964,4240,4516,4790,5063,5334,5604,5872,6138,6402
    dw 6664,6924,7182,7438,7692,7943,8192,8438,8682,8923,9162,9397
    dw 9630,9860,10087,10311,10531,10749,10963,11174,11381,11585,11786,11982
    dw 12176,12365,12551,12733,12911,13085,13255,13421,13583,13741,13894,14044
    dw 14189,14330,14466,14598,14726,14849,14968,15082,15191,15296,15396,15491
    dw 15582,15668,15749,15826,15897,15964,16026,16083,16135,16182,16225,16262
    dw 16294,16322,16344,16362,16374,16382,16384,16382,16374,16362,16344,16322
    dw 16294,16262,16225,16182,16135,16083,16026,15964,15897,15826,15749,15668
    dw 15582,15491,15396,15296,15191,15082,14968,14849,14726,14598,14466,14330
    dw 14189,14044,13894,13741,13583,13421,13255,13085,12911,12733,12551,12365
    dw 12176,11982,11786,11585,11381,11174,10963,10749,10531,10311,10087,9860
    dw 9630,9397,9162,8923,8682,8438,8192,7943,7692,7438,7182,6924
    dw 6664,6402,6138,5872,5604,5334,5063,4790,4516,4240,3964,3686
    dw 3406,3126,2845,2563,2280,1997,1713,1428,1143,857,572,286
    dw 0,-286,-572,-857,-1143,-1428,-1713,-1997,-2280,-2563,-2845,-3126
    dw -3406,-3686,-3964,-4240,-4516,-4790,-5063,-5334,-5604,-5872,-6138,-6402
    dw -6664,-6924,-7182,-7438,-7692,-7943,-8192,-8438,-8682,-8923,-9162,-9397
    dw -9630,-9860,-10087,-10311,-10531,-10749,-10963,-11174,-11381,-11585,-11786,-11982
    dw -12176,-12365,-12551,-12733,-12911,-13085,-13255,-13421,-13583,-13741,-13894,-14044
    dw -14189,-14330,-14466,-14598,-14726,-14849,-14968,-15082,-15191,-15296,-15396,-15491
    dw -15582,-15668,-15749,-15826,-15897,-15964,-16026,-16083,-16135,-16182,-16225,-16262
    dw -16294,-16322,-16344,-16362,-16374,-16382,-16384,-16382,-16374,-16362,-16344,-16322
    dw -16294,-16262,-16225,-16182,-16135,-16083,-16026,-15964,-15897,-15826,-15749,-15668
    dw -15582,-15491,-15396,-15296,-15191,-15082,-14968,-14849,-14726,-14598,-14466,-14330
    dw -14189,-14044,-13894,-13741,-13583,-13421,-13255,-13085,-12911,-12733,-12551,-12365
    dw -12176,-11982,-11786,-11585,-11381,-11174,-10963,-10749,-10531,-10311,-10087,-9860
    dw -9630,-9397,-9162,-8923,-8682,-8438,-8192,-7943,-7692,-7438,-7182,-6924
    dw -6664,-6402,-6138,-5872,-5604,-5334,-5063,-4790,-4516,-4240,-3964,-3686
    dw -3406,-3126,-2845,-2563,-2280,-1997,-1713,-1428,-1143,-857,-572,-286
    dw 0,286

%include "grphysics.inc"
%include "grplay.inc"
%include "grai.inc"
%include "grfront.inc"
%include "grmusic.inc"

%define OS88UI_ABOUT
%define OS88UI_NOBTN
%include "os88ui.inc"
%include "os88alt.inc"

; Every instance owns its model and scratch. No heap allocations.
%assign GR_BSS 0
%macro VAR 2
    %1 equ os88_image_end + GR_BSS
    %assign GR_BSS GR_BSS + %2
%endmacro
VAR gr_win, 2
VAR gr_fontseg, 2
VAR gr_fontoff, 2
VAR gr_fontfirst, 1
VAR gr_seed, 2
VAR gr_spawned, 1
VAR gr_abon, 1
VAR gr_state, 1
VAR gr_paused, 1
VAR gr_turn, 1
VAR gr_winner, 1
VAR gr_dancing, 1
VAR gr_sunhit, 1
VAR gr_insun, 1
VAR gr_scores, 2
VAR gr_target, 1
VAR gr_players, 1
VAR gr_aipower, 2
VAR gr_aierror, 2
VAR gr_aibest, 2
VAR gr_aix, 2
VAR gr_aiy, 2
VAR gr_aiyhi, 2
VAR gr_aivx, 2
VAR gr_aivy, 2
VAR gr_aigrem, 2
VAR gr_aiwrem, 2
VAR gr_aixrem, 2
VAR gr_aiyrem, 2
VAR gr_aitarget, 2
VAR gr_name1, 11
VAR gr_name2, 11
VAR gr_setupfield, 1
VAR gr_inputlen, 1
VAR gr_input, 11
VAR gr_musicptr, 2
VAR gr_musicdue, 2
VAR gr_introseq, 1
VAR gr_frontcols, 2
VAR gr_frontplane, 1
VAR gr_frontsource, 2
VAR gr_cachekey, 2
VAR gr_cacheratio, 2
VAR gr_lightcache, 4480
VAR gr_apecache, 1920
VAR gr_animdue, 2
VAR gr_animphase, 1
VAR gr_pose, 1
VAR gr_field, 1
VAR gr_edit, 1
VAR gr_angle, 2
VAR gr_power, 2
VAR gr_angles, 4
VAR gr_powers, 4
VAR gr_wind, 2
VAR gr_gwhole, 2
VAR gr_gfrac, 2
VAR gr_grava, 4
VAR gr_roofs, 12
VAR gr_lots, 12
VAR gr_widths, 12
VAR gr_colors, 12
VAR gr_buildings, 2
VAR gr_profile, 1
VAR gr_blast, 1
VAR gr_discink, 1
VAR gr_blastmax, 1
VAR gr_throwticks, 1
VAR gr_roundwait, 1
VAR gr_gx, 4
VAR gr_gy, 4
VAR gr_fs, 1
VAR gr_fsready, 1
VAR gr_quit, 1
VAR gr_cga, 1
VAR gr_vga, 1
VAR gr_fsi, FSI_SIZE
VAR gr_left, 2
VAR gr_top, 2
VAR gr_width, 2
VAR gr_height, 2
VAR gr_kind, 1
VAR gr_depth, 1
VAR gr_sx, 2
VAR gr_sy, 2
VAR gr_ox, 2
VAR gr_oy, 2
VAR gr_textmask, 2
VAR gr_textcell, 2
VAR gr_textend, 2
VAR gr_cellbytes, 2
VAR gr_hudchars, 512
VAR gr_huddirty, 512
VAR gr_textx, 2
VAR gr_texty, 2
VAR gr_px, 2
VAR gr_py, 2
VAR gr_pyhi, 2
VAR gr_vx, 2
VAR gr_vy, 2
VAR gr_grem, 2
VAR gr_wrem, 2
VAR gr_xrem, 2
VAR gr_yrem, 2
VAR gr_steps, 2
VAR gr_age, 2
VAR gr_saved, 1
VAR gr_bx, 2
VAR gr_by, 2
VAR gr_under, 64
VAR gr_hitx, 2
VAR gr_hity, 2
VAR gr_rx, 2
VAR gr_ry, 2
VAR gr_rw, 2
VAR gr_rh, 2
VAR gr_endy, 2
VAR gr_rows, 2
VAR gr_stride, 2
VAR gr_rowstart, 2
VAR gr_cgax, 2
VAR gr_cgarows, 2
VAR gr_planar, 1
VAR gr_reclip, 1
VAR gr_pairbits, 1
VAR gr_pairtable, 2
VAR gr_worktable, 2
VAR gr_planesleft, 1
VAR gr_planestep, 2
VAR gr_vgdest, 2
VAR gr_vgheight, 2
VAR gr_scene, 16384
VAR gr_band, 5376

OS88_BSS GR_BSS
OS88_IMAGE_END
