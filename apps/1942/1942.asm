; Native 1942 remake for os8088. SPEC 101; no NES CPU interpreter.
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
    mov ax, 64
    call OSAPI_MEM_CLAIM
    jc .out
    mov [n_graphics], dx
    mov ax,64
    call OSAPI_MEM_CLAIM
    jc .out
    mov [n_graphics2],dx
    mov si, n_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [n_win], bx
    OS88_REGION_MOVABLE
    mov si,n_pref
    call OSAPI_WM_PREFER
    mov al,1
    call OSAPI_WM_SNAP
    mov ax,n_fronttimer
    call OSAPI_WM_ONTIMER
    mov byte [n_frontstatic],0
    jnc .timer
    inc byte [n_frontstatic]
.timer:
    mov si, n_about
    call OSAPI_ABOUT_SET
    call OSAPI_GET_TICKS
    or ax, 1
    mov [n_seed], ax
    mov byte [n_sound], 1
    mov word [n_players],1
    call n_new
    mov byte [n_state], 0
    call n_frontprepare
    mov bx, [n_win]
    clc
.out:
    ret

n_paint:
    SAVE
    call n_frontpaint
    cmp byte [n_abon],0
    je .out
    mov bx,[n_win]
    mov si,n_ablines
    call os88ui_about_d
.out:
    RESTORE
    ret
n_about:
    SAVE
    mov byte [n_abon],1
    mov si, n_ablines
    call os88ui_about
    RESTORE
    ret
%include "front.inc"

n_exclusive:
    mov byte [n_infs], 1
    ; Drain/release the launcher key before the mode switch masks IRQ1.
.release:
    mov al, KSC_ENTER
    call OSAPI_KEY_DOWN
    jc .waitrelease
    mov al, 21h                     ; F
    call OSAPI_KEY_DOWN
    jc .waitrelease
    mov al,KSC_SPACE
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
    call n_loading
    call n_loadgfx
    jc n_assetfailed
    mov word [n_scene], 0ffffh
    call n_scenecheck
    jc n_assetfailed
n_loop:
    call OSAPI_GET_TICKS
    mov [n_frametick],ax
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
.notpal:
    cmp al, 'p'
    jne .notpause
    cmp byte [n_state], 1
    jne .notpause
    xor byte [n_paused], 1
    call n_refresh
.notpause:
    cmp byte [n_state],0
    jne .selected
    cmp al,'1'
    je .one
    cmp al,'2'
    jne .selected
    mov word [n_players],2
    call n_refresh
    jmp n_loop
.one:
    mov word [n_players],1
    call n_refresh
    jmp n_loop
.selected:
    cmp al, 'n'
    je .new
    ; OR 20h changed CR to 2Dh.
    cmp al, 2dh
    jne n_loop
    cmp byte [n_state], 1
    jne .new
    cmp word [n_flightphase],3
    jne .unpause
    mov word [n_flightphasetime],0
.unpause:
    mov byte [n_paused], 0
    call n_refresh
    jmp n_loop
.new:
    call n_new
    call n_refresh
    jmp n_loop
.frame:
    call n_scenecheck
    jc n_assetfailed
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
    call n_audio
    call n_present
n_frame_end:
    ; Rendering already paces a busy XT. Only yield to the frame clock when
    ; this iteration has not consumed a full BIOS tick (also covers pause).
    call OSAPI_GET_TICKS
    cmp ax,[n_frametick]
    jne n_loop
    mov al, FSXW_FRAME
    call OSAPI_FSX_WAIT
    jmp n_loop
n_failed:
    mov byte [n_error], 1
    jmp n_exit
n_assetfailed:
    mov byte [n_error], 2
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
    mov cx,0
    mov dl,40h
    call OSAPI_SND_TONE
.out:
    RESTORE
    ret

n_tpl: dw 52,32,452,284,n_title,n_paint,n_key,n_click
OS88_PREFER n_pref,452,284,452,284,452,154
n_title: db '1942',0
n_errmsg: db 'Fullscreen requires a VGA or CGA display.',0
n_asseterr: db 'Missing or damaged 1942 graphics files.',0
n_ablines: dw n_title,n_credit,n_credit2,n_credit3,0
n_credit: db 'Native remake for os8088',0
n_credit2: db '1942 original game: Capcom (1985)',0
n_credit3: db 'Original art / native VGA and CGA sprites',0
n_hud: db '000000  L3 R3  STAGE 01',0
n_msgselect: db '1 OR 2 PLAYERS    1',0
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
%include "campaign.inc"
%include "scroll.inc"
%define OS88UI_ABOUT
%define OS88UI_NOBTN
%include "os88ui.inc"
%assign NBSS 0
%macro VAR 2
    %1 equ os88_image_end + NBSS
    %assign NBSS NBSS + %2
%endmacro
VAR n_abon,1
VAR n_frontstatic,1
VAR n_frontready,2
VAR n_frontband,2
VAR n_frontx,2
VAR n_fronty,2
VAR n_frontscale,1
VAR n_frontkind,1
VAR n_frontwant,1
VAR n_fronthelp,1
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
VAR n_enemies,12*ESIZE
VAR n_shots,16*4
VAR n_bullets,16*8
VAR n_blasts,8*6
VAR n_pickx,2
VAR n_picky,2
VAR n_islandy,2
VAR n_wavey,2
; Native renderer scratch and per-page damage lists.
VAR n_graphics,2
VAR n_graphics2,2
VAR n_scene,2
VAR n_bank,2
VAR n_page,2
VAR n_drawbase,2
VAR n_background,1
VAR n_rendered,1
VAR n_savedbase,2
VAR n_counts,4
VAR n_oldcount,2
VAR n_rects,3072
VAR n_origin,2
VAR n_width,2
VAR n_height,2
VAR n_phase,2
VAR n_cacheptr,2
VAR n_compiled,1
VAR n_blitptr,4
VAR n_record,2
VAR n_plane,2
VAR n_vorigin,2
VAR n_scroll,2
VAR n_worldy,2
VAR n_worldpage,2
VAR n_scrollstage,2
VAR n_ringstart,2
VAR n_ringend,2
VAR n_ringbytes,2
VAR n_worldheight,2
VAR n_scanline,256
VAR n_rowmap,2
VAR n_rowtile,2
VAR n_rowhalf,2
VAR n_cacheend,2
VAR n_cachestart,2
VAR n_rowdest,2
VAR n_rowrepeat,2
VAR n_toprow,2
VAR n_toppage,2
VAR n_logicalrow,2
VAR n_cgastart,2
VAR n_prepare,1
VAR n_damagevalid,1
VAR n_damagecount,2
VAR n_damageend,2
VAR n_spans,400
VAR n_vbases,4
VAR n_vscrolls,4
VAR n_shift,2
VAR n_showbase,2
VAR n_spriteid,2
VAR n_spriteseg,2
VAR n_players,2
VAR n_activeplayer,2
VAR n_playersave,36
VAR n_flightphase,2
VAR n_flightphasetime,2
VAR n_distance,2
VAR n_routefrac,2
VAR n_event,2
VAR n_waveslive,192
VAR n_kills,2
VAR n_picktype,2
VAR n_wings,2
VAR n_percent,2
VAR n_percentbonus,4
VAR n_rollbonus,2
VAR n_nextlife,2
VAR n_secretkills,2
VAR n_secretlimit,2
VAR n_highlo,2
VAR n_highhi,2
VAR n_extended,2
VAR n_bosslive,1
VAR n_rebuild,1
VAR n_musicptr,2
VAR n_musicwait,2
VAR n_effectptr,2
VAR n_effectwait,2
VAR n_audiotick,2
VAR n_frametick,2
VAR n_audioelapsed,2
OS88_BSS NBSS
OS88_IMAGE_END
