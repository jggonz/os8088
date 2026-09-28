; Native 1942 remake for os8088. SPEC 99; no NES CPU interpreter.
%include "os88api.inc"
OS88_HEADER '1942', n_entry, 1, OS88_STACK_256
OS88_ICON16
    dw 0x0180,0x0990,0x1db8,0x1db8,0x1db8,0x7ffe,0xffff,0x1ff8
    dw 0x1db8,0x1998,0x1998,0x1998,0x1998,0x1ff8,0x0990,0x0180
    dw 0x0180,0x0990,0x1db8,0x1db8,0x1db8,0x7ffe,0xffff,0x1ff8
    dw 0x1db8,0x1998,0x1998,0x1998,0x1998,0x1ff8,0x0990,0x0180
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

n_entry:
    mov ax, 64
    call OSAPI_MEM_CLAIM
    jc .out
    mov [n_canvas], dx
    mov si, n_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [n_win], bx
    OS88_REGION_MOVABLE
    mov si, n_about
    call OSAPI_ABOUT_SET
    call OSAPI_GET_TICKS
    or ax, 1
    mov [n_seed], ax
    mov byte [n_sound], 1
    call n_new
    mov byte [n_state], 0
    mov bx, [n_win]
    clc
.out:
    ret

n_paint:
    SAVE
    call OSAPI_WM_CONTENT
    add ax, 10
    add dx, 8
    mov cx, ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov si, n_l1
    call OSAPI_FONT_STR_XPARENT
    add dx, 14
    mov si, n_l2
    call OSAPI_FONT_STR_XPARENT
    add dx, 14
    mov si, n_l3
    call OSAPI_FONT_STR_XPARENT
    add dx, 14
    mov si, n_l4
    call OSAPI_FONT_STR_XPARENT
    add dx, 14
    mov si, n_l5
    call OSAPI_FONT_STR_XPARENT
    add dx, 14
    mov si, n_l6
    cmp byte [n_error], 0
    je .line
    mov si, n_errmsg
.line:
    call OSAPI_FONT_STR_XPARENT
    RESTORE
    ret
n_about:
    SAVE
    mov si, n_ablines
    call os88ui_about
    RESTORE
    ret
n_click:
    ret
n_key:
    SAVE
    cmp al, 13
    je .run
    or al, 20h
    cmp al, 'f'
    jne .out
.run:
    mov bx, [n_win]
    call OSAPI_FSX_CAPS
    test ax, (1 << FSXM_MODEX) | (1 << FSXM_CGA320)
    jz .error
    mov byte [n_error], 0
    mov ax, n_exclusive
    mov bx, [n_win]
    mov cx, FSXF_FASTTICK
    call OSAPI_FSX_RUN
    jnc .out
.error:
    mov byte [n_error], 1
    mov bx, [n_win]
    call n_paint
.out:
    RESTORE
    ret

n_exclusive:
    mov byte [n_infs], 1
    ; Drain/release the launcher key before the mode switch masks IRQ1.
.release:
    mov al, KSC_ENTER
    call OSAPI_KEY_DOWN
    jc .waitrelease
    mov al, 21h                     ; F
    call OSAPI_KEY_DOWN
    jnc .mode
.waitrelease:
    mov al, FSXW_FRAME
    call OSAPI_FSX_WAIT
    jmp .release
.mode:
    mov bx, [n_win]
    call OSAPI_FSX_CAPS
    mov byte [n_cga], 0
    mov al, FSXM_MODEX
    cmp dl, VID_VGA
    je .set
    mov al, FSXM_CGA320
    mov byte [n_cga], 1
.set:
    push ds
    pop es
    mov di, n_fsi
    call OSAPI_FSX_MODE
    jc n_failed
    call n_palette
    call n_refresh
n_loop:
    mov ah, 1
    int 16h
    jz .frame
    xor ah, ah
    int 16h
    cmp al, 27
    je n_exit
    or al, 20h
    cmp al, 'f'
    je n_exit
    cmp al, 'm'
    jne .notmute
    xor byte [n_sound], 1
    xor ax, ax
    call n_tone
.notmute:
    cmp al, 'c'
    jne .notpal
    inc byte [n_profile]
    cmp byte [n_profile], 3
    jb .pal
    mov byte [n_profile], 0
.pal:
    call n_palette
    call n_dirtyall
.notpal:
    cmp al, 'p'
    jne .notpause
    cmp byte [n_state], 1
    jne .notpause
    xor byte [n_paused], 1
    call n_refresh
.notpause:
    cmp al, 'n'
    je .new
    ; OR 20h changed CR to 2Dh.
    cmp al, 2dh
    jne n_loop
    cmp byte [n_state], 1
    jne .new
    mov byte [n_paused], 0
    call n_refresh
    jmp n_loop
.new:
    call n_new
    call n_refresh
    jmp n_loop
.frame:
    cmp byte [n_state], 1
    jne .present
    cmp byte [n_paused], 0
    jne .present
    call n_erase
    call n_update
    call n_draw
    cmp byte [n_state], 1
    je .present
    call n_refresh
.present:
    call n_present
n_frame_end:
    mov al, FSXW_FRAME
    call OSAPI_FSX_WAIT
    mov al, FSXW_FRAME
    call OSAPI_FSX_WAIT
    jmp n_loop
n_failed:
    mov byte [n_error], 1
n_exit:
    xor ax, ax
    call n_tone
    mov byte [n_infs], 0
    ret

n_tone:
    SAVE
    or ax, ax
    jz .play
    cmp byte [n_sound], 0
    je .out
.play:
    mov cx, 1
    mov dl, 40h
    call OSAPI_SND_TONE
.out:
    RESTORE
    ret

n_tpl: dw 100,70,390,118,n_title,n_paint,n_key,n_click
n_title: db '1942',0
n_l1: db '1942 - native vertical shooter',0
n_l2: db 'Enter / F: fullscreen or resume',0
n_l3: db 'Arrows move. Space / Z fires. X rolls.',0
n_l4: db 'P pause. M sound. C CGA palette. N new.',0
n_l5: db 'Escape / F returns to this window.',0
n_l6: db 'VGA 320x240 / color CGA 320x200',0
n_errmsg: db 'Fullscreen requires a VGA or CGA display.',0
n_ablines: dw n_title,n_credit,n_credit2,n_credit3,0
n_credit: db 'Native remake for os8088',0
n_credit2: db '1942 original game: Capcom (1985)',0
n_credit3: db 'Font and textures from your NES cartridge',0
n_hud: db '000000  L3 R3  STAGE 01',0
n_msgtitle: db '1 9 4 2',0
n_msgstart: db 'ENTER TO TAKE OFF',0
n_msghint: db 'SPACE FIRES  X ROLLS',0
n_msgover: db 'GAME OVER',0
n_msgwin: db 'MISSION COMPLETE',0
n_msgnew: db 'N FOR A NEW GAME',0
n_msgpause: db 'PAUSED',0
n_msgresume: db 'P TO RESUME',0
%include "1942art.inc"
%include "video.inc"
%include "game.inc"
%define OS88UI_ABOUT
%define OS88UI_NOBTN
%include "os88ui.inc"
%assign NBSS 0
%macro VAR 2
    %1 equ os88_image_end + NBSS
    %assign NBSS NBSS + %2
%endmacro
VAR n_canvas,2
VAR n_win,2
VAR n_seed,2
VAR n_error,1
VAR n_infs,1
VAR n_cga,1
VAR n_profile,1
VAR n_sound,1
VAR n_state,1
VAR n_paused,1
VAR n_fsi,FSI_SIZE
VAR n_dirty,240
VAR n_vbits,1
VAR n_vblocks,1
VAR n_px,2
VAR n_py,2
VAR n_lives,2
VAR n_rolls,2
VAR n_roll,2
VAR n_xheld,1
VAR n_grace,2
VAR n_fire,2
VAR n_weapon,2
VAR n_scorelo,2
VAR n_scorehi,2
VAR n_stage,2
VAR n_spawned,2
VAR n_spawnwait,2
VAR n_frames,2
VAR n_bossmade,1
VAR n_huddirty,1
VAR n_enemies,12*14
VAR n_shots,16*4
VAR n_bullets,16*8
VAR n_blasts,8*6
VAR n_pickx,2
VAR n_picky,2
VAR n_islandy,2
VAR n_wavey,2
; Renderer scratch, confined to the exclusive bracket.
VAR n_sx,2
VAR n_sy,2
VAR n_src,2
VAR n_pwidth,2
VAR n_pheight,2
VAR n_pplane,2
VAR n_fontsprite,66
VAR n_sw,2
VAR n_sh,2
VAR n_color,1
VAR n_vrow,2
VAR n_vplane,2
VAR n_vmask,1
VAR n_pairs,4096
OS88_BSS NBSS
OS88_IMAGE_END
