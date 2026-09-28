; Native XT DrMarco. Contracts and deliberate adaptations: SPEC.md 99.
%include "os88api.inc"
OS88_HEADER 'DRMARCO', dm_entry, 1, OS88_STACK_256
OS88_ICON16
    dw 0,0x03c0,0x07e0,0x0e70,0x1c38,0x381c,0x7038,0xe070
    dw 0xc0e0,0xc1c0,0x6380,0x7700,0x3e00,0x1c00,0,0
    dw 0,0x03c0,0x07e0,0x0ff0,0x1ff8,0x3ffc,0x7ff8,0xfff0
    dw 0xffe0,0xffc0,0x7f80,0x7f00,0x3e00,0x1c00,0,0
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

dm_entry:
    mov si, dm_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [dm_win], bx
    OS88_REGION_MOVABLE
    OS88_ALTENTER_ARM
    mov si, dm_pref
    call OSAPI_WM_PREFER
    mov al, 1
    call OSAPI_WM_SNAP
    mov si, dm_about
    call OSAPI_ABOUT_SET
    call OSAPI_GET_TICKS
    or ax, 1
    mov [dm_seed], ax
    call dm_fontcache
    mov al, 4
    call dm_audio_song
    call dm_audio_open
    mov bx, [dm_win]
    mov ax, dm_audio_timer
    call OSAPI_WM_ONTIMER
    mov ax, 1
    call OSAPI_WM_TIMER
    mov bx, [dm_win]
    clc
.out: ret

dm_paint:
    SAVE
    call dm_frontpaint
    cmp byte [dm_abon], 0
    je .done
    mov bx, [dm_win]
    mov si, dm_credits
    call os88ui_about_d
.done:
    RESTORE
    ret

dm_settingcompose:
    mov al, [dm_level]
    xor ah, ah
    mov di, dm_setting+6
    call dm_decimal2
    mov bx, dm_speednames
    mov al, [dm_speed]
    xor ah, ah
    shl ax, 1
    add bx, ax
    mov si, [bx]
    mov di, dm_setting+16
    mov ax, [si]
    mov [di], ax
    mov al, [si+2]
    mov [di+2], al
    mov bl, [dm_music]
    xor bh, bh
    shl bx, 1
    mov si, [dm_musicnames+bx]
    mov di, dm_setting+27
    mov cx, 5
.copy_music:
    lodsb
    mov [di], al
    inc di
    loop .copy_music
    ret

dm_about:
    SAVE
    mov byte [dm_abon], 1
    mov bx, [dm_win]
    mov si, dm_credits
    call os88ui_about
    RESTORE
    ret

dm_click:
    SAVE
    cmp byte [dm_abon], 0
    je .go
    mov byte [dm_abon], 0
    jmp .paint
.go:
    ; Cached window drags can move the pixels without invoking our painter.
    push dx
    mov bx, [dm_win]
    call OSAPI_WM_CONTENT
    pop di
    sub cx, ax
    sub di, dx
    mov dx, di
    cmp byte [dm_frontscale], 2
    jne .hit
    shr dx, 1
.hit:
    cmp byte [dm_help], 0
    jne .back
    cmp cx, 112
    jb .done
    cmp cx, 328
    ja .done
    cmp dx, 98
    jb .start
    cmp dx, 110
    ja .done
.back:
    xor byte [dm_help], 1
    call dm_frontreset
    jmp .paint
.start:
    cmp dx, 84
    jb .done
    cmp dx, 98
    ja .done
    call dm_launch
.paint:
    call dm_paint
.done:
    RESTORE
    ret

dm_onkey:
    SAVE
    cmp byte [dm_abon], 0
    je .key
    mov byte [dm_abon], 0
    jmp .paint
.key:
    cmp al, 27
    je .back
    cmp ax, KEY_ALTENTER
    je .go
    cmp al, 13
    je .go
    or al, 20h
    cmp al, 'h'
    je .help
    cmp al, 'f'
    je .go
    cmp al, 'n'
    je .new
    cmp al, 's'
    je .speed
    cmp al, 'm'
    je .music
    cmp ah, KSC_LEFT
    je .less
    cmp ah, KSC_RIGHT
    je .more
    jmp .done
.help:
    xor byte [dm_help], 1
    call dm_frontreset
    jmp .paint
.back:
    cmp byte [dm_help], 0
    je .done
    mov byte [dm_help], 0
    call dm_frontreset
    jmp .paint
.less:
    cmp byte [dm_level], 0
    je .done
    dec byte [dm_level]
    jmp .new
.more:
    cmp byte [dm_level], 20
    je .done
    inc byte [dm_level]
    jmp .new
.speed:
    inc byte [dm_speed]
    cmp byte [dm_speed], 3
    jb .new
    mov byte [dm_speed], 0
    jmp .new
.music:
    call dm_music_cycle
    jmp .settings
.new:
    call dm_audio_quiet
    mov al, 5
    call dm_audio_song
    call dm_audio_open
    mov byte [dm_paused], 0
    mov bx, [dm_win]
    mov ax, 1
    call OSAPI_WM_TIMER
    mov al, DM_FX_CURSOR
    call dm_effect
    mov byte [dm_started], 0
.settings:
    call dm_frontsettings
    jmp .done
.go:
    call dm_launch
.paint:
    mov bx, [dm_win]
    call OSAPI_WM_CLIP_SET
    jc .done
    call dm_paint
.done:
    RESTORE
    ret

dm_launch:
    mov bx, [dm_win]
    call OSAPI_FSX_CAPS
    test ax, (1 << FSXM_MODEX) | (1 << FSXM_CGA320)
    jz .out
    xor ax, ax
    call OSAPI_WM_TIMER
    call dm_audio_quiet
    mov ax, dm_fullscreen
    mov cx, FSXF_FASTTICK
    call OSAPI_FSX_RUN
    ; Enter can beat the first artwork timer. Resume its deferred load after
    ; gameplay, while leaving the saved game music paused on the desktop.
    cmp byte [dm_artkind], 0
    jne .loaded
    call dm_frontreset
    jmp .out
.loaded:
    mov word [dm_reveal], 264
.out: ret

dm_fullscreen:
    OS88_ALTENTER_SEED
    ; Let the activating chord release before a BIOS video mode call can
    ; mask its break. The wait uses the bracket, never the scheduler.
.release:
    mov al, KSC_ENTER
    call OSAPI_KEY_DOWN
    jnc .mode
    mov al, FSXW_FRAME
    call OSAPI_FSX_WAIT
    jmp .release
.mode:
    mov bx, [dm_win]
    call OSAPI_FSX_CAPS
    mov byte [dm_cga], 0
    mov al, FSXM_MODEX
    cmp dl, VID_VGA
    je .set
    inc byte [dm_cga]
    mov al, FSXM_CGA320
.set:
    push ds
    pop es
    mov di, dm_fsi
    call OSAPI_FSX_MODE
    jc .done
    mov byte [dm_fs], 1
    call dm_palette
    call dm_fontbuild
    call dm_addresses
    cmp byte [dm_started], 0
    jne .resume
    mov word [dm_score], 0
    mov word [dm_score+2], 0
    call dm_newgame
.resume:
    mov byte [dm_paused], 0
    mov word [dm_keys], 0
    mov word [dm_pressed], 0
    call dm_invalidate
    call dm_framepaint
    call dm_audio_open
    call dm_audio_restore
.loop:
    call os88alt_edge
    jc .done
    mov ah, 1
    int 16h
    jz .input
    xor ah, ah
    int 16h
    cmp al, 27
    je .done
    call dm_bufferkey
    jmp .loop
.input:
    call dm_input
    call dm_tick
    call dm_audio_tick
    call dm_render
    mov al, FSXW_FRAME
    call OSAPI_FSX_WAIT
    jmp .loop
.done:
    call dm_audio_quiet
    mov byte [dm_fs], 0
    mov byte [dm_paused], 1
    ret

; Keep short action taps that begin and end between two frame polls.
; Held action keys are edge-triggered: their BIOS typematic repeats are ignored.
; Left/right/down still use scan-state timing, independent of typematic.
dm_bufferkey:
    mov si, dm_scans+3
    mov bx, 8
    mov cx, 7
.find:
    cmp ah, [si]
    je .found
    inc si
    shl bx, 1
    loop .find
    ret
.found:
    test [dm_keys], bx
    jnz .out
    or [dm_pressed], bx
.out: ret

; Edge keys in bit order: left,right,down,Z,X,up,P,N,Enter.
dm_input:
    xor bx, bx
    mov di, 1
    mov si, dm_scans
    mov cx, 10
.poll:
    lodsb
    call OSAPI_KEY_DOWN
    jnc .up
    or bx, di
.up:
    shl di, 1
    loop .poll
    mov dx, [dm_keys]
    mov [dm_keys], bx
    not dx
    and dx, bx
    or dx, [dm_pressed]
    mov word [dm_pressed], 0
    test dx, 200h
    jz .newkey
    push dx
    call dm_music_cycle
    pop dx
.newkey:
    test dx, 80h
    jz .pause
    mov word [dm_score], 0
    mov word [dm_score+2], 0
    call dm_newgame
    ret
.pause:
    test dx, 40h
    jz .enter
    xor byte [dm_paused], 1
    push dx
    call dm_audio_quiet
    call dm_audio_open
    cmp byte [dm_paused], 0
    jne .pausecue
    call dm_audio_restore
.pausecue:
    mov al, DM_FX_PAUSE
    call dm_effect
    pop dx
    mov byte [dm_huddirty], 1
.enter:
    test dx, 100h
    jz .play
    cmp byte [dm_state], 3
    jb .play
    cmp byte [dm_state], 4
    jne .restart
    cmp byte [dm_level], 20
    jae .restart
    inc byte [dm_level]
.restart:
    call dm_newgame
    ret
.play:
    cmp byte [dm_paused], 0
    jne .out
    cmp byte [dm_state], 0
    jne .out
    push dx
    test dx, 18h
    jz .uprotate
    mov al, -1
    test dx, 08h
    jz .rotate
    mov al, 1
    jmp .rotate
.uprotate:
    test dx, 20h
    jz .movement
    mov al, -1
.rotate:
    call dm_rotate
.movement:
    pop dx
    mov bx, [dm_keys]
    and bx, 3
    cmp bl, [dm_lr]
    je .held
    mov [dm_lr], bl
    mov byte [dm_repeat], 15
    jmp .move
.held:
    or bx, bx
    jz .out
    dec byte [dm_repeat]
    jnz .out
    mov byte [dm_repeat], 5
.move:
    cmp bl, 1
    je .left
    cmp bl, 2
    jne .out
    mov al, 1
    jmp dm_move
.left:
    mov al, -1
    jmp dm_move
.out: ret

dm_scans: db KSC_LEFT,KSC_RIGHT,KSC_DOWN,2ch,2dh,KSC_UP,19h,31h,KSC_ENTER,32h

dm_tpl: dw 52, 32, 452, 284, dm_title, dm_paint, dm_onkey, dm_click
OS88_PREFER dm_pref, 452,284,452,284,452,154
dm_title: db 'DrMarco',0
dm_setting: db 'Level 00  Speed LOW  Music FEVER',0
dm_musicnames: dw dm_fever,dm_chill,dm_off
dm_fever: db 'FEVER'
dm_chill: db 'CHILL'
dm_off: db 'OFF  '
dm_credits: dw dm_title,dm_credit1,dm_credit2,dm_credit3,0
dm_credit1: db 'Gameplay reference: Nintendo (1990)',0
dm_credit2: db 'Native 8086 single-player adaptation',0
dm_credit3: db 'Original DrMarco surround; NES cell tiles',0
dm_speednames: dw dm_low,dm_med,dm_hi
dm_low: db 'LOW',0
dm_med: db 'MED',0
dm_hi: db 'HI ',0

%include "game.inc"
%include "audio.inc"
%include "video.inc"
%include "anim.inc"
%include "front.inc"
%include "dm-tables.inc"
%define OS88UI_ABOUT
%define OS88UI_NOBTN
%include "os88ui.inc"
%include "os88alt.inc"

%assign DM_BSS 0
%macro VAR 2
 %1 equ os88_image_end + DM_BSS
 %assign DM_BSS DM_BSS + %2
%endmacro
VAR dm_win,2
VAR dm_fs,1
VAR dm_cga,1
VAR dm_abon,1
VAR dm_help,1
VAR dm_frontx,2
VAR dm_fronty,2
VAR dm_frontscale,1
VAR dm_frontcolor,1
VAR dm_frontplay,1
VAR dm_frontheight,2
VAR dm_frontrow,2
VAR dm_frontend,2
VAR dm_fronttextfrom,2
VAR dm_fronttextto,2
VAR dm_frontbatch,2
VAR dm_frontstride,2
VAR dm_frontptr,2
VAR dm_reveal,2
VAR dm_artseg,2
VAR dm_artsize,2
VAR dm_artkind,1
VAR dm_fsi,FSI_SIZE
VAR dm_seed,2
VAR dm_started,1
VAR dm_level,1
VAR dm_speed,1
VAR dm_state,1
VAR dm_paused,1
VAR dm_keys,2
VAR dm_pressed,2
VAR dm_lr,1
VAR dm_repeat,1
VAR dm_x,1
VAR dm_y,1
VAR dm_rotation,1
VAR dm_colors,2
VAR dm_next,2
VAR dm_sequence,128
VAR dm_seqindex,1
VAR dm_pillcount,1
VAR dm_accel,1
VAR dm_falltimer,1
VAR dm_delay,1
VAR dm_viruses,1
VAR dm_score,4
VAR dm_award,2
VAR dm_board,128
VAR dm_marks,128
VAR dm_scene,128
VAR dm_shadow,128
VAR dm_olda,2
VAR dm_oldb,2
VAR dm_candidates,8
VAR dm_dirty,1
VAR dm_huddirty,1
VAR dm_moved,1
VAR dm_matched,1
VAR dm_queue,520
VAR dm_qend,2
VAR dm_planeoff,2
VAR dm_mask,1
VAR dm_hud,256
VAR dm_hudshadow,256
VAR dm_glyphs,768
VAR dm_fontvga,4032
dm_fontcga equ dm_fontvga
VAR dm_animstart,0
VAR dm_animclock,1
VAR dm_animslot,1
VAR dm_animpending,1
VAR dm_blinktimer,1
VAR dm_colorcount,4
VAR dm_hits,4
VAR dm_viruspose,8
VAR dm_actorpose,4
VAR dm_animend,0
VAR dm_actorshadow,4
VAR dm_animdirty,1
VAR dm_audio_pending,3
VAR dm_audio_turn,1
VAR dm_music,1
VAR dm_audio_fm,1
VAR dm_audio_live,1
VAR dm_audio_phase,2
VAR dm_audio_sent,2
VAR dm_audio_lease,1
VAR dm_audio_voices,24
VAR dm_fx_ptr,2
VAR dm_fx_hz,2
VAR dm_fx_wait,1
VAR dm_fx_priority,1
OS88_BSS DM_BSS
OS88_IMAGE_END
