; Gorillas for os8088. See SPEC.md 98 and README.md.
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
    xor bx, bx
    mov bl, [gr_field]
    shl bx, 1
    cmp al, 8
    je .back
    cmp ah, 48h
    je .up
    cmp ah, 4dh
    je .up
    cmp ah, 50h
    je .down
    cmp ah, 4bh
    je .down
    cmp al, '0'
    jb .out
    cmp al, '9'
    ja .out
    sub al, '0'
    xor ah, ah
    mov cx, ax
    mov ax, [gr_angle+bx]
    cmp byte [gr_edit], 0
    jne .append
    xor ax, ax
.append:
    mov dx, 10
    mul dx
    add ax, cx
    jmp short .limit
.back:
    mov ax, [gr_angle+bx]
    xor dx, dx
    mov cx, 10
    div cx
    jmp short .store
.up:
    mov ax, [gr_angle+bx]
    inc ax
    jmp short .limit
.down:
    mov ax, [gr_angle+bx]
    sub ax, 1
    jnc .store
    xor ax, ax
    jmp short .store
.limit:
    mov dx, 180
    or bx, bx
    jz .bound
    mov dx, 150
.bound:
    cmp ax, dx
    ja .out
.store:
    mov byte [gr_edit], 1
    cmp [gr_angle+bx], ax
    je .out
    mov [gr_angle+bx], ax
    jmp gr_inputpaint
.tab:
    xor byte [gr_field], 1
    mov byte [gr_edit], 0
    jmp gr_selectpaint
.hud:
    jmp gr_hudpaint
.enter:
    cmp byte [gr_paused], 0
    jne .pause
    cmp byte [gr_state], 1
    je .out
    cmp byte [gr_state], 2
    je .round
    cmp byte [gr_state], 3
    je .new
    cmp byte [gr_field], 0
    je .tab
    cmp word [gr_power], 0
    je .out
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
    cmp word [gr_musicptr], 0
    jne .active
    cmp byte [gr_state], 1
    je .active
    cmp byte [gr_state], 4
    je .active
    cmp byte [gr_state], 7
    je .active
    cmp byte [gr_state], 0
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
.row:
    push cx
.pixel:
    call gr_put
    inc cx
    cmp cx, si
    jbe .pixel
    pop cx
    inc dx
    cmp dx, di
    jbe .row
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
    jmp gr_city

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
    mov byte [gr_field], 0
    mov byte [gr_edit], 0
    mov word [gr_angle], 45
    mov word [gr_power], 70
    call gr_rand
    xor dx, dx
    mov bx, 21
    div bx
    sub dx, 10
    mov [gr_wind], dx
    ; Eight buildings; varying roof heights, facades and lit windows.
    xor bp, bp
.building:
    call gr_rand
    and ax, 31
    add ax, 63
    mov bx, bp
    mov [gr_roofs+bx], al
    mov dx, ax
    mov cx, bp
    shl cx, 1
    shl cx, 1
    shl cx, 1
    shl cx, 1
    shl cx, 1
    inc cx
    mov si, cx
    add si, 29
    mov di, 127
    mov al, [gr_facades+bx]
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
    call gr_put
    inc cx
    call gr_put
    inc dx
    call gr_put
    dec cx
    call gr_put
    inc dx
    call gr_put
    inc cx
    call gr_put
    dec cx
    sub dx, 2
    add cx, 6
    cmp cx, si
    jb .window
    pop cx
    add dx, 6
    cmp dx, 126
    jb .windows
    inc bp
    cmp bp, 8
    jb .building
    ; The players stand on the second and seventh roofs.
    mov word [gr_gx], 40
    mov word [gr_gx+2], 200
    xor ax, ax
    mov al, [gr_roofs+1]
    sub ax, 20
    mov [gr_gy], ax
    mov al, [gr_roofs+6]
    sub ax, 20
    mov [gr_gy+2], ax
    xor bp, bp
    call gr_gorilla
    mov bp, 2
    call gr_gorilla
    call gr_sun
    call gr_hud
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
    ; Two ten-character names and two two-digit scores fit on one line.
    mov si, gr_name1
    mov di, gr_status
    call gr_namecopy
    mov si, gr_name2
    mov di, gr_status+18
    call gr_namecopy
    xor ax, ax
    mov al, [gr_scores]
    mov di, gr_digits
    call gr_number
    mov ax, [gr_digits+1]
    mov [gr_status+11], ax
    xor ax, ax
    mov al, [gr_scores+1]
    call gr_number
    mov ax, [gr_digits+1]
    mov [gr_status+14], ax
    mov si, gr_name1
    cmp byte [gr_turn], 0
    je .name
    mov si, gr_name2
.name:
    mov di, gr_help
    call gr_namecopy
    mov ax, [gr_wind]
    mov byte [gr_help+16], '+'
    or ax, ax
    jns .wind
    neg ax
    mov byte [gr_help+16], '-'
.wind:
    mov di, gr_help+17
    call gr_number
    mov si, gr_status
    mov al, 15
    call gr_text
    mov si, gr_prompt
    cmp byte [gr_state], 0
    jne .state
    mov ax, [gr_angle]
    mov di, gr_prompt+3
    call gr_number
    mov ax, [gr_power]
    mov di, gr_prompt+10
    call gr_number
    mov byte [gr_prompt], ' '
    mov byte [gr_prompt+7], ' '
    xor bx, bx
    mov bl, [gr_field]
    mov al, 7
    mul bl
    mov bx, ax
    mov byte [gr_prompt+bx], '>'
    jmp short .line
.state:
    mov si, gr_roundmsg
    cmp byte [gr_state], 2
    je .winner
    mov si, gr_matchmsg
.winner:
.line:
    cmp byte [gr_paused], 0
    je .write
    mov si, gr_pausemsg
.write:
    xor cx, cx
    mov dx, 8
    mov al, 9
    call gr_text
    mov si, gr_help
    mov dx, 16
    mov al, 15
    call gr_text
    ret

; Numeric edits format and compare only the active three-cell field.
; Paused input still changes the model; resume rebuilds the visible prompt.
; AX=new value, BX=word offset (0 angle, 2 velocity).
gr_inputpaint:
    mov si, gr_prompt+3
    mov cx, 24
    or bx, bx
    jz .number
    add si, 7
    mov cx, 80
.number:
    mov di, si
    call gr_number
    cmp byte [gr_paused], 0
    jne .done
    call gr_dirtyclear
    mov dx, 8
    mov al, 9
    mov bp, 3
    call gr_textspan
    mov si, 35
    cmp byte [gr_field], 0
    je .flush
    add si, 7
.flush:
    mov di, si
    add di, 3
    jmp gr_hudflushspan
.done:
    ret

gr_selectpaint:
    mov byte [gr_prompt], ' '
    mov byte [gr_prompt+7], ' '
    mov si, gr_prompt
    cmp byte [gr_field], 0
    je .selected
    add si, 7
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
    add si, 7
    mov cx, 56
    call gr_textspan
    mov si, 32
    mov di, 40
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
    jne .ink
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

; Q6 coordinates. One rendered step has four swept substeps. Trig table
; Q8; velocity * sin / 80 gives pixels per step in Q6. Gravity increments
; velocity by 4/1/10 (Earth/Moon/Jupiter); wind uses a signed remainder.
gr_fire:
    mov byte [gr_state], 1
    mov byte [gr_saved], 0
    mov word [gr_age], 0
    mov word [gr_wrem], 0
    mov word [gr_grem], 0
    xor bx, bx
    mov bl, [gr_turn]
    shl bx, 1
    mov ax, [gr_gx+bx]
    add ax, 8
    mov cl, 6
    shl ax, cl
    mov [gr_px], ax
    mov ax, [gr_gy+bx]
    sub ax, 3
    shl ax, cl
    mov [gr_py], ax
    mov word [gr_pyhi], 0
    mov bx, [gr_angle]
    shl bx, 1
    mov ax, [gr_sin+bx]
    imul word [gr_power]
    mov cx, 80
    idiv cx
    neg ax
    mov [gr_vy], ax
    mov bx, 90
    sub bx, [gr_angle]
    mov si, 1
    jns .cos
    neg bx
.cos:
    ; cosine(a) = sin(90-a), with negative sign above 90.
    cmp word [gr_angle], 90
    jbe .lookup
    mov si, -1
.lookup:
    shl bx, 1
    mov ax, [gr_sin+bx]
    imul word [gr_power]
    idiv cx
    imul si
    cmp byte [gr_turn], 0
    je .vx
    neg ax
.vx:
    mov [gr_vx], ax
    mov si, gr_musicthrow
    jmp gr_musicstart

gr_tick:
    cmp byte [gr_abon], 0
    jne .out
    cmp byte [gr_paused], 0
    jne .out
    call gr_musictick
    cmp byte [gr_state], 4
    jae gr_fronttick
    cmp byte [gr_state], 0
    je gr_aitick
    cmp byte [gr_state], 1
    jne .out
    call gr_unbanana
    inc word [gr_age]
    cmp word [gr_age], 1200
    ja .miss
    mov bp, 4
.step:
    mov ax, [gr_vx]
    mov cl, 2
    sar ax, cl
    add [gr_px], ax
    mov ax, [gr_vy]
    sar ax, cl
    cwd
    add [gr_py], ax
    adc [gr_pyhi], dx
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
    cmp dx, 128
    jge .miss
    cmp dx, 24
    jl .next                    ; flight above scene is legal
    ; Gorilla hit boxes, including a return onto the thrower.
    xor si, si
.ape:
    mov ax, cx
    sub ax, [gr_gx+si]
    cmp ax, 16
    jae .other
    mov ax, dx
    sub ax, [gr_gy+si]
    cmp ax, 20
    jb .hitape
.other:
    add si, 2
    cmp si, 4
    jb .ape
    ; Sun is decorative and may be crossed without impact.
    cmp dx, 42
    jb .next
    call gr_get
    cmp al, 3                  ; sun rays are not terrain
    je .next
    or al, al
    jnz .terrain
.next:
    dec bp
    jnz .step
    ; Fractional gravity: 9.8 maps to the original four velocity units/tick.
    mov ax, [gr_grav10]
    shl ax, 1
    shl ax, 1
    add ax, [gr_grem]
    xor dx, dx
    mov bx, 98
    div bx
    mov [gr_grem], dx
    add [gr_vy], ax
    mov ax, [gr_wind]
    add ax, [gr_wrem]
    cwd
    mov bx, 4
    idiv bx
    mov [gr_wrem], dx
    add [gr_vx], ax
    call gr_banana
.out:
    ret
.miss:
    xor byte [gr_turn], 1
    mov word [gr_aipower], 0
    mov byte [gr_state], 0
    mov byte [gr_field], 0
    mov byte [gr_edit], 0
    jmp gr_hudpaint
.terrain:
    call gr_crater
    mov si, gr_musicimpact
    call gr_musicstart
    jmp .miss
.hitape:
    shr si, 1
    xor si, 1                  ; opponent scores even on a self-hit
    mov ax, si
    mov [gr_winner], al
    mov [gr_turn], al
    inc byte [gr_scores+si]
    mov byte [gr_state], 2
    mov al, [gr_scores]
    add al, [gr_scores+1]
    cmp al, [gr_target]
    jb .explode
    mov byte [gr_state], 3
.explode:
    call gr_crater
    mov si, gr_musichit
    call gr_musicstart
    jmp gr_hudpaint

; A persistent circular crater: collision and all later repaints see the hole.
gr_crater:
    mov [gr_hitx], cx
    mov [gr_hity], dx
    mov bp, -7
.row:
    mov si, -7
.pixel:
    mov ax, bp
    imul bp
    mov bx, ax
    mov ax, si
    imul si
    add ax, bx
    cmp ax, 49
    ja .next
    mov cx, [gr_hitx]
    add cx, si
    mov dx, [gr_hity]
    add dx, bp
    cmp dx, 24
    jl .next
    xor al, al
    call gr_put
.next:
    inc si
    cmp si, 7
    jle .pixel
    inc bp
    cmp bp, 7
    jle .row
    mov ax, [gr_hitx]
    sub ax, 8
    and ax, 0fff8h
    jns .xok
    xor ax, ax
.xok:
    mov bx, [gr_hity]
    sub bx, 8
    mov cx, 24
    mov dx, 17
    jmp gr_blit

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
    cmp ax, 252
    ja .out
    cmp dx, 0
    jl .out
    cmp dx, 120
    ja .out
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
    mov al, 1
    call gr_put
    inc cx
    call gr_put
    inc dx
    call gr_put
    inc dx
    test byte [gr_age], 2
    jz .left
    inc cx
    jmp short .tip
.left:
    dec cx
.tip:
    call gr_put
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
    call gr_background
.scene:
    xor ax, ax
    xor bx, bx
    mov cx, 256
    mov dx, 128
    jmp gr_blit

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

; Render AX=x (8-aligned), BX=y, CX=width, DX=height in logical pixels.
; Eight logical rows per band bound scratch space at 512*24/2 = 6144.
; The 1bpp path maps every nonzero ink to white: black windows stay readable.
gr_blit:
    SAVE
    push ds
    pop es
    cmp bx, 128
    jae .done
    cmp ax, 256
    jae .done
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
.band:
    mov ax, [gr_endy]
    sub ax, [gr_ry]
    cmp ax, 8
    jbe .rows
    mov ax, 8
.rows:
    mov [gr_rh], ax
    mov [gr_rows], ax
    mov di, gr_band
    mov ax, [gr_ry]
    mov cl, 7
    shl ax, cl
    mov si, [gr_rx]
    shr si, 1
    add si, ax
    add si, gr_scene
    mov ax, [gr_rw]
    mul word [gr_sx]
    mov [gr_outw], ax
    cmp byte [gr_depth], 4
    je .stride4
    cmp byte [gr_depth], 2
    je .stride2
    shr ax, 1
.stride2:
    shr ax, 1
.stride4:
    shr ax, 1
    mov [gr_stride], ax
.row:
    call gr_musicservice
    push si
    mov [gr_rowstart], di
    mov cx, [gr_rw]
    shr cx, 1
    cmp byte [gr_depth], 4
    jne .packed
.color:
    lodsb
    cmp byte [gr_vga], 0
    jne .nativecolor
    xor bx, bx
    mov bl, al
    cmp word [gr_sx], 1
    jne .expanded
    mov al, [gr_winmap+bx]
    stosb
    loop .color
    jmp short .repeat
.expanded:
    shl bx, 1
    mov ax, [gr_winx2+bx]
    stosw
    loop .color
    jmp short .repeat
.nativecolor:
    cmp word [gr_sx], 1
    je .single
    mov ah, al
    and al, 0f0h
    mov bl, al
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    or al, bl
    stosb
    mov al, ah
    and al, 15
    mov bl, al
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1
    or al, bl
.single:
    stosb
    loop .color
    jmp short .repeat
.packed:
    xor bp, bp                 ; bits accumulated
    xor dh, dh                 ; output byte
.byte:
    lodsb
    mov ah, al
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    call gr_packpixel
    mov al, ah
    and al, 15
    call gr_packpixel
    loop .byte
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
    mov ax, [gr_rx]
    mul word [gr_sx]
    add ax, [gr_ox]
    mov bx, ax
    mov ax, [gr_ry]
    mul word [gr_sy]
    add ax, [gr_oy]
    xchg ax, bx                ; AX destination x, BX y
    mov cx, [gr_outw]
    mov dx, [gr_rh]
    push ax
    mov ax, dx
    mul word [gr_sy]
    mov dx, ax
    pop ax
    mov bp, [gr_stride]
    mov si, gr_band
    cmp byte [gr_vga], 0
    jne .vga
    cmp byte [gr_cga], 0
    jne .cga
    cmp byte [gr_depth], 1
    je .mono
    call OSAPI_GFX_BLIT4
    jmp short .advance
.mono:
    call OSAPI_GFX_BLIT1
    jmp short .advance
.cga:
    call gr_cgablit
    jmp short .advance
.vga:
    call gr_vgablit
.advance:
    mov ax, [gr_rh]
    add [gr_ry], ax
    mov ax, [gr_ry]
    cmp ax, [gr_endy]
    jb .band
.done:
    RESTORE
    ret

; AL logical colour, AH saved source byte, DH accumulator, BP bit count.
gr_packpixel:
    push cx
    push bx
    mov cx, [gr_sx]
    cmp byte [gr_depth], 2
    je .cga
    xor bx, bx
    mov bl, al
    mov al, [gr_monomap+bx]
.mono:
    shl dh, 1
    or dh, al
    inc bp
    cmp bp, 8
    jne .again
    mov [es:di], dh
    inc di
    xor dh, dh
    xor bp, bp
.again:
    loop .mono
    jmp short .done
.cga:
    xor bx, bx
    mov bl, al
    mov al, [gr_cgamap+bx]
    shl dh, 1
    shl dh, 1
    or dh, al
    add bp, 2
    cmp bp, 8
    jne .done
    mov [es:di], dh
    inc di
    xor dh, dh
    xor bp, bp
.done:
    pop bx
    pop cx
    ret

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

; AX/BX=physical x/y, CX/DX=width/height; DS:SI=packed 4bpp band.
; All rectangles are byte-aligned. Each plane is written once, with no
; read/modify/write cycle per pixel and no kernel drawing in the foreign mode.
gr_vgablit:
    push es
    mov [gr_vgsrc], si
    mov [gr_vgheight], dx
    shr cx, 1
    shr cx, 1
    shr cx, 1
    mov [gr_vgcols], cx
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
    mov byte [gr_vgmask], 1
    mov bx, gr_planebits
.plane:
    mov dx, 03c4h
    mov al, 2
    mov ah, [gr_vgmask]
    out dx, ax
    mov si, [gr_vgsrc]
    mov di, [gr_vgdest]
    mov ax, [gr_vgheight]
    mov [gr_vgrows], ax
.row:
    mov bp, [gr_vgcols]
.byte:
    xor ah, ah
%rep 4
    lodsb
    xlat                         ; two plane bits, high nibble first
    shl ah, 1
    shl ah, 1
    or ah, al
%endrep
    mov al, ah
    stosb
    dec bp
    jnz .byte
    add di, 80
    sub di, [gr_vgcols]
    dec word [gr_vgrows]
    jnz .row
    add bx, 256
    shl byte [gr_vgmask], 1
    cmp byte [gr_vgmask], 16
    jb .plane
    pop es
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
gr_status: db '           00:00            ',0
gr_prompt: db ' A:045  V:070',0
gr_blank: db 0
gr_roundmsg: db 'Point! Enter: next skyline',0
gr_matchmsg: db 'Match over! Enter: setup',0
gr_pausemsg: db 'Paused. P or Enter to resume',0
gr_help: db 'Player 1   Wind  +000',0
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
gr_facades: db 5,6,7,5,7,6,5,7
; Semantic inks: sky, ape, explosion, sun, black, gray/red/cyan facade,
; unlit window, white text, ape highlight/shadow, roof, spare, lit window, text.
gr_cgamap: db 0,3,2,3,0,2,2,1,0,3,3,2,2,0,3,3
gr_monomap: db 0,1,1,1,0,1,1,1,0,1,1,1,1,0,0,1
%include "grart.inc"

; sin(degrees)*256, rounded; cosine uses symmetry.
gr_sin:
    dw 0, 4, 9, 13, 18, 22, 27, 31, 36, 40, 44, 49
    dw 53, 58, 62, 66, 71, 75, 79, 83, 88, 92, 96, 100
    dw 104, 108, 112, 116, 120, 124, 128, 132, 136, 139, 143, 147
    dw 150, 154, 158, 161, 165, 168, 171, 175, 178, 181, 184, 187
    dw 190, 193, 196, 199, 202, 204, 207, 210, 212, 215, 217, 219
    dw 222, 224, 226, 228, 230, 232, 234, 236, 237, 239, 241, 242
    dw 243, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 254
    dw 255, 255, 255, 256, 256, 256, 256, 256, 256, 256, 255, 255
    dw 255, 254, 254, 253, 252, 251, 250, 249, 248, 247, 246, 245
    dw 243, 242, 241, 239, 237, 236, 234, 232, 230, 228, 226, 224
    dw 222, 219, 217, 215, 212, 210, 207, 204, 202, 199, 196, 193
    dw 190, 187, 184, 181, 178, 175, 171, 168, 165, 161, 158, 154
    dw 150, 147, 143, 139, 136, 132, 128, 124, 120, 116, 112, 108
    dw 104, 100, 96, 92, 88, 83, 79, 75, 71, 66, 62, 58
    dw 53, 49, 44, 40, 36, 31, 27, 22, 18, 13, 9, 4
    dw 0

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
VAR gr_scores, 2
VAR gr_grav10, 2
VAR gr_grem, 2
VAR gr_target, 1
VAR gr_players, 1
VAR gr_aipower, 2
VAR gr_aierror, 2
VAR gr_aibest, 2
VAR gr_aix, 2
VAR gr_aiy, 2
VAR gr_aivy, 2
VAR gr_aivx, 2
VAR gr_aiyhi, 2
VAR gr_aiwrem, 2
VAR gr_aigrem, 2
VAR gr_name1, 11
VAR gr_name2, 11
VAR gr_setupfield, 1
VAR gr_inputlen, 1
VAR gr_input, 11
VAR gr_digits, 3
VAR gr_musicptr, 2
VAR gr_musicdue, 2
VAR gr_introseq, 1
VAR gr_frontcols, 2
VAR gr_frontplane, 1
VAR gr_frontsource, 2
VAR gr_cachekey, 2
VAR gr_cacheratio, 2
VAR gr_lightcache, 13440
VAR gr_apecache, 1920
VAR gr_animdue, 2
VAR gr_animphase, 1
VAR gr_pose, 1
VAR gr_field, 1
VAR gr_edit, 1
VAR gr_angle, 2
VAR gr_power, 2
VAR gr_wind, 2
VAR gr_roofs, 8
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
VAR gr_age, 2
VAR gr_wrem, 2
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
VAR gr_outw, 2
VAR gr_rowstart, 2
VAR gr_cgax, 2
VAR gr_cgarows, 2
VAR gr_vgsrc, 2
VAR gr_vgdest, 2
VAR gr_vgcols, 2
VAR gr_vgheight, 2
VAR gr_vgrows, 2
VAR gr_vgmask, 1
VAR gr_scene, 16384
VAR gr_band, 6144

OS88_BSS GR_BSS
OS88_IMAGE_END
