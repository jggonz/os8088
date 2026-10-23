; WireFleet — Two PCs. Two fleets. One winner. SPEC.md 114.
%include "os88api.inc"
    OS88_HEADER 'WireFleet', wf_entry, OS88_F_ICON | OS88_F_PARTS, OS88_HIGH | OS88_HIGHMEM
%include "os88parts.inc"
    OS88_ICON16
    dw 0,0,0x0180,0x03c0,0x03c0,0x07e0,0x1ff8,0x7ffe
    dw 0xffff,0xffff,0x7ffe,0x3ffc,0x7ffe,0xffff,0x6666,0
    dw 0,0,0x0180,0x0240,0x0240,0x05a0,0x1818,0x6006
    dw 0x8001,0xffff,0x7ffe,0x3ffc,0,0x9999,0x6666,0
    OS88_ICON16_END

wf_entry:
    call op_load                ; SI is the kernel's: the name, FIRST (20.12).
                                ; Both parts are lazy, so this is the copy
    mov si, wf_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [wf_win], bx
    OS88_REGION_MOVABLE
    OS88_ALTENTER_ARM
    mov al, 1
    call OSAPI_WM_SNAP
    call OSAPI_WM_OWNBG
    mov si, wf_prefs
    call OSAPI_WM_PREFER
    mov si, wf_menus
    call OSAPI_MENU_SET
    mov si, wf_aboutentry
    call OSAPI_ABOUT_SET
    mov ax, wf_rightclick
    call OSAPI_WM_ONRCLICK
    mov ax, wf_closeentry
    call OSAPI_WM_ONCLOSE
    mov ax, bx
    mov bx, wf_buttons
    mov si, wf_buttonupentry
    mov di, wf_buttondrag
    mov dx, wf_clickentry
    call os88ui_btninit
    mov bx, [wf_win]
    mov ax, wf_tick
    call OSAPI_WM_ONTIMER
    call OSAPI_WM_ONWAKE
    mov ax, 1
    call OSAPI_WM_TIMER
    mov byte [wf_fallback], 0
    jnc .clock
    mov byte [wf_fallback], 1
.clock:
    call wf_pcm_open            ; 114.11: the samples, on a Sound Blaster
    call wf_new
    call wf_autostart           ; 114.9: look for the opponent at once
    call wf_geometry            ; (the stamps' planar copy asks the display)
    call wf_fx_need             ; 114.11: the stamp bank
    mov bx, [wf_win]
    clc
.out:
    ret

wf_paint:
    push si
    cmp byte [wf_fallback], 1
    jne .clock
    mov bx, [wf_win]
    mov ax, wf_wakeworker
    call OSAPI_TASK_SPAWN
    jc .clock
    inc byte [wf_fallback]
    OS88_WORKER_RESTARTABLE_NOKEEP wf_wakeworker
.clock:
    call wf_geometry
    cmp byte [wf_colour], 0
    je .mono
    mov byte [wf_fullpaint], 1  ; 114.10: the colour face, whole
    call wf_c_draw
    jmp short .about
.mono:
    ; Only the margins/body are filled. Grids compose their own opaque
    ; ground; there is no white rectangle underneath the new board.
    call wf_ground
    mov byte [wf_fullpaint], 1
    call wf_boards
    call wf_header
    call wf_roster
    call wf_controls
    call wf_footer
.about:
    cmp byte [wf_abon], 0
    je .out
    mov bx, [wf_win]
    mov si, wf_ablines
    call os88ui_about_d
.out:
    pop si
    ret

; Update changes under the window's clip, including partial coverage. A
; clipped blit is never recorded as fully visible: the next W_PAINT forces
; the full span back when exposure changes.
wf_update:
    push si
    mov bx, [wf_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    call wf_geometry
    mov byte [wf_fullpaint], 0
    cmp byte [wf_colour], 0
    je .mono
    call wf_c_draw
    jmp short .out
.mono:
    call wf_boards
    call wf_header
    call wf_roster
    call wf_controls
    call wf_footer
.out:
    pop si
    ret

wf_onkey:
    cmp byte [wf_abon], 0
    jne wf_dismiss
    cmp ax, KEY_ALTENTER
    je wf_fullscreen
    cmp byte [wf_chatmode], 0
    jne wf_chat_key
    cmp al, 27
    jne .key
    cmp byte [wf_nativefull], 0
    jne wf_fullscreen
    ret
.key:
    cmp al, 0
    jne .ascii
    mov dl, [wf_target]
    cmp ah, 0x4b
    je .left
    cmp ah, 0x4d
    je .right
    cmp ah, 0x48
    je .up
    cmp ah, 0x50
    jne .out
    cmp dl, 90
    jae .out
    add dl, 10
    jmp .move
.up:
    cmp dl, 10
    jb .out
    sub dl, 10
    jmp .move
.left:
    cmp dl, 0
    je .out
    dec dl
    jmp .move
.right:
    cmp dl, 99
    jae .out
    inc dl
.move:
    mov [wf_target], dl
    jmp wf_update
.ascii:
    cmp al, 13
    je .place
    cmp al, '1'
    jb .letter
    cmp al, '5'
    ja .letter
    cmp byte [wf_ready], 0
    jne .out
    sub al, '1'
    mov [wf_selected], al
    jmp wf_update
.letter:
    or al, 0x20
    cmp al, 'a'
    je .auto
    cmp al, 'c'
    je .clear
    cmp al, 'r'
    je .rotate
    cmp al, 'n'
    je .new
    cmp al, 'm'
    je .mute
    cmp al, 'u'
    je .music
    cmp al, 'y'
    je .ready
    cmp al, 't'
    je wf_chat_begin
    cmp al, 's'
    je wf_save
    cmp al, 'o'
    je wf_load
    cmp al, 'f'
    je wf_fullscreen
    cmp al, 'h'
    je .help
.out:
    ret
.auto:
    cmp byte [wf_ready], 0
    jne .out
    call wf_auto
    mov al, 1
    call wf_sfx
    jmp wf_update
.clear:
    cmp byte [wf_ready], 0
    jne .out
    call wf_clearfleet
    jmp wf_repaint
.new:
    call wf_new
    jmp wf_repaint
.rotate:
    cmp byte [wf_ready], 0
    jne .out
    xor byte [wf_direction], 1
    jmp wf_update
.place:
    cmp byte [wf_ready], 0
    jne wf_fire
    call wf_place
    jc .invalid
    call wf_nextship
    mov al, 1
    call wf_sfx
    jmp wf_update
.invalid:
    mov word [wf_message], wf_cannotplace
    jmp wf_update
.mute:
    xor byte [wf_muted], 1
    cmp byte [wf_muted], 0
    je .soundupdate
    call wf_soundquiet
.soundupdate:
    jmp wf_update
.music:
    call wf_musictoggle
    jmp wf_update
.ready:
    cmp byte [wf_ready], 0
    jne .out
    cmp byte [wf_placed], 5
    jne .out
    mov byte [wf_ready], 1
    mov word [wf_message], wf_readywaiting
    cmp byte [wf_connected], 0
    je .readycue
    call wf_readyline
.readycue:
    mov byte [wf_urgent], 1
    mov al, 2
    call wf_sfx
    jmp wf_repaint
.help:
    mov si, [wf_win]
    jmp wf_about

; The native menu callbacks pass a zero-based item in AL and menu in AH.
wf_command:
    cmp ah, 2
    je .link
    cmp ah, 0
    jne .fleet
    cmp al, 0
    je wf_onkey.new
    cmp al, 1
    je wf_onkey.ready
    cmp al, 2
    je wf_save
    cmp al, 3
    je wf_load
    cmp al, 5
    je wf_onkey.music
    jmp wf_onkey.mute
.fleet:
    cmp al, 0
    je wf_onkey.rotate
    cmp al, 1
    je wf_onkey.auto
    jmp wf_onkey.clear
.link:
    cmp al, 0
    je .automatic
    cmp al, 3
    je .disconnect
    cmp al, 4
    je .stats
    jmp wf_connect              ; 1 = COM1, 2 = COM2
.stats:
    call wf_stats
    jmp wf_update
.automatic:
    call wf_disconnect
    mov byte [wf_wantport], 0
    call wf_autostart
    mov word [wf_message], wf_searching
    jmp wf_update
.disconnect:
    mov byte [wf_linkmode], 0
    call wf_disconnect
    jmp wf_update

wf_click:
    cmp byte [wf_abon], 0
    jne wf_dismiss
    push cx
    push dx
    call wf_geometry
    pop dx
    pop cx
    cmp word [wf_cellh], 8
    jne .line
    cmp byte [wf_chatmode], 0
    je .grid
.line:
    mov si, wf_line
    call os88line_click
    jc .outside
    jmp wf_chat_begin
.outside:
    cmp byte [wf_chatmode], 0
    jne .out
.grid:
    call wf_hitboard
    jc .roster
    mov [wf_target], al
    cmp byte [wf_ready], 0
    je wf_onkey.place
    jmp wf_update
.roster:
    cmp byte [wf_ready], 0
    jne .out
    sub cx, [wf_ox]
    cmp cx, [wf_rostx1]
    jb .out
    cmp cx, [wf_rostx2]
    jae .out
    sub dx, [wf_oy]
    sub dx, [wf_rosty]
    jc .out
    mov ax, dx
    xor dx, dx
    div word [wf_roststep]
    cmp ax, 5
    jae .out
    mov [wf_selected], al
    jmp wf_update
.out:
    ret

wf_rightclick:
    cmp byte [wf_ready], 0
    jne .out
    cmp byte [wf_abon], 0
    jne .out
    call wf_hitboard
    jc .out
    xor ah, ah
    mov bx, ax
    mov al, [wf_board+bx]
    or al, al
    jz .out
    dec al
    mov [wf_selected], al
    call wf_remove
    jmp wf_update
.out:
    ret

; CX/DX=absolute pointer. CF=outside left grid; AL=0..99 inside.
wf_hitboard:
    sub cx, [wf_ox]
    mov ax, [wf_gx0]
    cmp byte [wf_ready], 0
    je .origin
    mov ax, [wf_gx1]
.origin:
    sub cx, ax
    jc .bad
    cmp cx, 240
    jae .bad
    sub dx, [wf_oy]
    sub dx, [wf_gridy]
    jc .bad
    cmp dx, [wf_gridh]
    jae .bad
    mov ax, dx
    xor dx, dx
    div word [wf_cellh]
    mov dl, 10
    mul dl
    mov bx, ax
    mov ax, cx
    mov cl, 24
    div cl
    xor ah, ah
    add ax, bx
    clc
    ret
.bad:
    stc
    ret

wf_buttonup:
    push cx
    push dx
    call wf_geometry
    pop dx
    pop cx
    mov bx, wf_buttons
    call os88ui_btnup
    or ax, ax
    jz .out
    cmp byte [wf_colour], 0     ; 114.10: 5 is Chat, or Send while typing
    je .phase
    cmp al, 5
    jne .phase
    cmp byte [wf_chatmode], 0
    je wf_chat_begin
    mov al, 13
    jmp wf_chat_key
.phase:
    cmp byte [wf_ready], 0
    je .deployment
    cmp al, 1
    je wf_fire
    cmp al, 2
    je wf_chat_begin
    cmp al, 3
    je wf_save
    cmp al, 4
    je .connect
.deployment:
    mov bx, ax
    mov al, [wf_btnkeys+bx]
    jmp wf_onkey.ascii
.out:
    ret
.connect:
    mov al, [wf_wantport]       ; a pinned port reconnects to itself; else
    cmp byte [wf_linkmode], 2   ; the search starts over (114.9)
    je .port
    mov al, 0
    jmp wf_command.link
.port:
    jmp wf_connect
wf_buttondrag:
    push cx
    push dx
    call wf_geometry
    pop dx
    pop cx
    mov bx, wf_buttons
    jmp os88ui_btndrag

wf_repaint:
    mov bx, [wf_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    mov si, [wf_win]
    call wf_paint
.out:
    ret
wf_about:
    mov byte [wf_abon], 1
    mov word [wf_buttons+OS88UI_BT_N], 0
    mov bx, si
    mov si, wf_ablines
    jmp os88ui_about
wf_dismiss:
    mov byte [wf_abon], 0
    mov word [wf_buttons+OS88UI_BT_N], 5
    cmp byte [wf_chatmode], 0
    je .paint
    cmp word [wf_cellh], 8
    jne .paint
    mov word [wf_buttons+OS88UI_BT_N], 0
.paint:
    jmp wf_repaint

%macro WF_CALLBACK 2
%1:
    push si
    call %2
    pop si
    ret
%endmacro
WF_CALLBACK wf_keyentry, wf_onkey
WF_CALLBACK wf_clickentry, wf_click
WF_CALLBACK wf_commandentry, wf_command
WF_CALLBACK wf_buttonupentry, wf_buttonup
WF_CALLBACK wf_aboutentry, wf_about
WF_CALLBACK wf_closeentry, wf_close

wf_close:
    call wf_disconnect
    call wf_soundquiet
    call wf_pcm_close
    mov dx, [wf_fxpseg]
    or dx, dx
    jz .bank
    call OSAPI_MEM_FREE
.bank:
    mov al, WF_P_FX
    call op_drop
    clc
    ret

wf_wakeworker:
.loop:
    mov bx, [wf_win]
    call OSAPI_TASK_ALIVE
    call OSAPI_WM_WAKE
    mov ax, 1
    call OSAPI_TASK_SLEEP
    jmp short .loop

wf_tpl:
    dw 16,24,600,414
    dw wf_title,wf_paint,wf_keyentry,wf_clickentry
wf_prefs: dw 600,414, 600,304, 600,154
    OS88_MENUSET wf_menus, wf_title, wf_commandentry
        OS88_MENU wf_gamemenu, wf_gameitems, 6
        OS88_MENU wf_fleetmenu, wf_fleetitems, 3
        OS88_MENU wf_linkmenu, wf_linkitems, 5
    OS88_MENUSET_END wf_menus
wf_gameitems: dw wf_newlabel,wf_readylabel,wf_savelabel,wf_loadlabel,wf_soundlabel,wf_musiclabel
wf_fleetitems: dw wf_rotatelabel,wf_autolabel,wf_clearlabel
wf_linkitems: dw wf_autolinklabel,wf_com1label,wf_com2label,wf_disconnectlabel,wf_statslabel
wf_title: db 'WireFleet',0
wf_gamemenu: db 'Game',0
wf_fleetmenu: db 'Fleet',0
wf_linkmenu: db 'Link',0
wf_autolinklabel: db 'Automatic',0
wf_com1label: db 'Connect COM1',0
wf_com2label: db 'Connect COM2',0
wf_disconnectlabel: db 'Disconnect',0
wf_statslabel: db 'Statistics',0
wf_readywaiting: db 'Fleet locked. Waiting for your opponent...',0
wf_newlabel: db 'New fleet [N]',0
wf_readylabel: db 'Ready [Y]',0
wf_rotatelabel: db 'Rotate R',0
wf_autolabel: db 'Auto A',0
wf_clearlabel: db 'Clear C',0
wf_soundlabel: db 'Sound M',0
wf_soundoff: db 'Muted M',0
wf_musiclabel: db 'Music U',0
wf_firelabel: db 'Fire Enter',0
wf_chatlabel: db 'Chat [T]',0
wf_connectlabel: db 'Connect',0
wf_savelabel: db 'Save S',0
wf_loadlabel: db 'Load O',0
wf_btnkeys: db 0,'racym'
wf_btnlabels: dw wf_rotatelabel,wf_autolabel,wf_clearlabel,wf_readylabel,wf_soundlabel
wf_btnflags: times 5 dw 0
wf_ablines: dw wf_a1,wf_a2,wf_a3,wf_a4,0
wf_a1: db 'WireFleet for os8088',0
wf_a2: db 'Two PCs. Two fleets. One winner.',0
wf_a3: db 'Naval battles over a null modem',0
wf_a4: db '1-5 ship, R rotate, A auto, Y ready, U music',0
wf_placehelp: db 'Place: click/Enter  R rotate  1-5 ship  Right-click remove',0
wf_allplaced: db 'All five placed. Ready [Y] locks your fleet. N: new fleet',0
wf_cannotplace: db 'Cannot place here: keep every square on water, inside the grid.',0
wf_deploytitle: db 'DEPLOY YOUR FLEET',0
wf_readytitle: db 'FLEET READY',0
wf_matchcaption: db 'Captain 1 vs Captain 2 | Turn',0
wf_tagline: db 'Two PCs. Two fleets. One winner.',0
wf_yourfleet: db 'YOUR FLEET',0
wf_enemywaters: db 'ENEMY WATERS',0
wf_yourships: db 'YOUR SHIPS',0
wf_selectedlabel: db '  SELECTED',0
wf_selectedh: db '> Horizontal ',0
wf_selectedv: db '> Vertical   ',0
wf_placedlabel: db '  PLACED     ',0
wf_emptylabel: db '  UNPLACED   ',0
wf_footertext: db 'H: controls/About   T: chat   S/O: save/load   F: fullscreen',0
wf_axes: db 'ABCDEFGHIJ'
wf_digit: db '  ',0

%define OS88UI_ABOUT
%define OS88UI_ARM
%define OS88UI_BOWN
%include "os88ui.inc"
%include "os88line.inc"
%include "os88alt.inc"
%include "wirefleet/wfmodel.inc"
%include "wirefleet/wflink.inc"
%include "wirefleet/wfchat.inc"
%include "wirefleet/wfsound.inc"
%include "wirefleet/wfanim.inc"
%include "wirefleet/wfsave.inc"
%include "wirefleet/wffull.inc"
%include "wirefleet/wfvideo.inc"
%include "wirefleet/wfcolor.inc"
%define GFXE_TEXT
%define GFXE_SCALE
%define GFXE_BITS
%define GFXE_BAND_W   304       ; the title bar's icon and longest caption
%define GFXE_BAND_H   16
%define GFXE_BAND_BUF wf_band
%include "os88gfx.inc"
%define DEC_U16
%include "os88dec.inc"
%define FLAT_BCAP wf_ccap
%define FLAT_BSTY wf_cstyle
%define FLAT_BIG
%include "os88flat.inc"
%include "wirefleet/art/tiles.inc"
%include "wirefleet/art/fx.inc"

; The two lazy parts (SPEC.md 114.11): the effect stamps and the samples.
; Row order is the Makefile's WFLETPARTS.
WF_P_FX  equ 0                  ; WFLETFX.GFX  the stamp bank, three forms
WF_P_SFX equ 1                  ; WFLET.SFX    Sound Blaster clips
OS88_PARTS_BEGIN 2
    OS88_PART OP_ASSET, OP_LAZY | OP_COMP
    OS88_PART OP_ASSET, OP_LAZY | OP_COMP
OS88_PARTS_END

; The region can move: no segment pointers outlive a callback in this wave.
%assign WF_BSS OP_BSS             ; the parts standard's bss comes first
%macro WF_VAR 2
%1 equ os88_image_end+WF_BSS
%assign WF_BSS WF_BSS+%2
%endmacro
WF_VAR wf_win,2
WF_VAR wf_ox,2
WF_VAR wf_oy,2
WF_VAR wf_w,2
WF_VAR wf_h,2
WF_VAR wf_cellh,2
WF_VAR wf_gridh,2
WF_VAR wf_gridy,2
WF_VAR wf_controlsy,2
WF_VAR wf_roststep,2
WF_VAR wf_monochrome,1
WF_VAR wf_abon,1
WF_VAR wf_fullpaint,1
WF_VAR wf_muted,1
WF_VAR wf_ready,1
WF_VAR wf_selected,1
WF_VAR wf_direction,1
WF_VAR wf_target,1
WF_VAR wf_placed,1
WF_VAR wf_step,1
WF_VAR wf_attempts,1
WF_VAR wf_message,2
WF_VAR wf_anchors,5
WF_VAR wf_dirs,5
WF_VAR wf_board,100
WF_VAR wf_inshots,100
WF_VAR wf_outshots,100
WF_VAR wf_cache,200
WF_VAR wf_btnrects,40
WF_VAR wf_rowmap,48
WF_VAR wf_rowbuf,2880
WF_VAR wf_drawboard,1
WF_VAR wf_drawrow,1
WF_VAR wf_drawcol,1
WF_VAR wf_first,1
WF_VAR wf_last,1
WF_VAR wf_tile,1
WF_VAR wf_source,2
WF_VAR wf_rowdest,2
WF_VAR wf_rowstride,2
WF_VAR wf_tstride,2
WF_VAR wf_destx,2
WF_VAR wf_desty,2
WF_VAR wf_rosterindex,1
WF_VAR wf_savedboard,1
WF_VAR wf_rowlimit,1
WF_VAR wf_rowtiles,10
WF_VAR wf_identity,4
WF_VAR wf_peer,4
WF_VAR wf_match,4
WF_VAR wf_side,1
WF_VAR wf_linkopen,1
WF_VAR wf_connected,1
WF_VAR wf_conflict,1
WF_VAR wf_peerready,1
WF_VAR wf_wantport,1
WF_VAR wf_linkmode,1
WF_VAR wf_cands,1
WF_VAR wf_curport,1
WF_VAR wf_heard,1
WF_VAR wf_retry,2
WF_VAR wf_dwell,2
WF_VAR wf_turn,2
WF_VAR wf_peerturn,2
WF_VAR wf_hash,2
WF_VAR wf_pending,1
WF_VAR wf_pendingcell,1
WF_VAR wf_cachevalid,1
WF_VAR wf_cacheturn,2
WF_VAR wf_cachecell,1
WF_VAR wf_cachevalue,1
WF_VAR wf_cacheship,1
WF_VAR wf_cacheanchor,1
WF_VAR wf_cachedir,1
WF_VAR wf_finished,1
WF_VAR wf_enemysunk,1
WF_VAR wf_urgent,1
WF_VAR wf_dirty,1
WF_VAR wf_sendkind,1
WF_VAR wf_now,2
WF_VAR wf_seen,2
WF_VAR wf_sendtime,2
WF_VAR wf_rxwork,1
WF_VAR wf_rlen,1
WF_VAR wf_rescape,1
WF_VAR wf_frame,64
WF_VAR wf_rxbuf,64
WF_VAR wf_txbuf,130
WF_VAR wf_txpos,2
WF_VAR wf_txlen,2
WF_VAR wf_canonical,7
WF_VAR wf_serialbuf,SER_BUFFER_SIZE
WF_VAR wf_badframes,2
WF_VAR wf_chatseq,2
WF_VAR wf_chatrxseq,2
WF_VAR wf_chatpending,1
WF_VAR wf_chatlength,1
WF_VAR wf_chattext,32
WF_VAR wf_chatline,40
WF_VAR wf_fallback,1
WF_VAR wf_chatmode,1
WF_VAR wf_line,OS88LINE_SZ
WF_VAR wf_input,33
WF_VAR wf_chatlog,120
WF_VAR wf_chatcount,2
WF_VAR wf_linebuf,76
WF_VAR wf_soundseq,2
WF_VAR wf_soundwait,1
WF_VAR wf_soundtime,2
WF_VAR wf_soundhz,2
WF_VAR wf_soundactive,1
WF_VAR wf_soundnotes,2
WF_VAR wf_filename,13
WF_VAR wf_filemode,1
WF_VAR wf_filesizelo,2
WF_VAR wf_filesizehi,2
WF_VAR wf_checkboard,100
WF_VAR wf_checkship,1
WF_VAR wf_checkplaced,1
WF_VAR wf_nativefull,1
WF_VAR wf_infs,1
WF_VAR wf_fsfile,1
WF_VAR wf_fsquit,1
WF_VAR wf_fsforce,1
WF_VAR wf_fsmouse,1
WF_VAR wf_fspress,1
WF_VAR wf_fslastx,2
WF_VAR wf_fslasty,2
WF_VAR wf_fsi,FSI_SIZE
WF_VAR wf_fontfirst,1
WF_VAR wf_fontlast,1
WF_VAR wf_fontbase,2
WF_VAR wf_font,768
WF_VAR wf_fsstart,2
WF_VAR wf_fsxbyte,2
WF_VAR wf_fsscan,1
WF_VAR wf_fstextx,2
WF_VAR wf_fstexty,2
WF_VAR wf_fstextptr,2
WF_VAR wf_fsglyph,2
WF_VAR wf_fstextscan,1
WF_VAR wf_fscache,240
WF_VAR wf_fscacheptr,2
WF_VAR wf_bannerbuf,76
WF_VAR wf_headershown,75
WF_VAR wf_rostershown,5
WF_VAR wf_buttonshown,20
WF_VAR wf_footershown,2
WF_VAR wf_chatshown,2
WF_VAR wf_chatmodeshown,1
WF_VAR wf_colour,1
WF_VAR wf_strx,2
WF_VAR wf_sterr,2
WF_VAR wf_stbits,1
WF_VAR wf_stframes,2
WF_VAR wf_sthello,2
WF_VAR wf_statline,80
WF_VAR wf_cbx1,2
WF_VAR wf_planar,1
WF_VAR wf_pdest,2
WF_VAR wf_cellbuf,12
WF_VAR wf_spreadok,1
WF_VAR wf_spreadt,1024
WF_VAR wf_ptiles,WF_TILE_STORED*WF_TILE_COLOR
WF_VAR wf_pand,WF_TILE_COLOR
WF_VAR wf_por,WF_TILE_COLOR
WF_VAR wf_hitx,1
WF_VAR wf_hitand,2
WF_VAR wf_hitor,2
WF_VAR wf_gx0,2
WF_VAR wf_gx1,2
WF_VAR wf_rostx1,2
WF_VAR wf_rostx2,2
WF_VAR wf_rosty,2
WF_VAR wf_cardb,2
WF_VAR wf_chatt,2
WF_VAR wf_chatn,1
WF_VAR wf_cbx,2
WF_VAR wf_rowy,2
WF_VAR wf_rowsig,1
WF_VAR wf_placedshown,1
WF_VAR wf_capshown,2
WF_VAR wf_capink,1
WF_VAR wf_lampshown,1
WF_VAR wf_msgseen,2
WF_VAR wf_chatnews,2
WF_VAR wf_chatlogshown,2
WF_VAR wf_fireshown,1
WF_VAR wf_lastout,1
WF_VAR wf_ccap,10
WF_VAR wf_cstyle,5
WF_VAR wf_firecap,12
WF_VAR wf_turnbuf,11
WF_VAR wf_turnshown,10
WF_VAR wf_evbuf,74
WF_VAR wf_evshown,74
WF_VAR wf_statbuf,75
WF_VAR wf_statshown,74
WF_VAR wf_band,38*16
WF_VAR wf_musicoff,1
WF_VAR wf_cueq,1
WF_VAR wf_fxleft,1
WF_VAR wf_bgmmode,1
WF_VAR wf_bgmsound,1
WF_VAR wf_bgmch,3
WF_VAR wf_bgmptr,2
WF_VAR wf_bgmdue,2
WF_VAR wf_pcmseg,2
WF_VAR wf_pcmflags,1
WF_VAR wf_pcmhandle,1
WF_VAR wf_pcmactive,1
WF_VAR wf_fxseg,2
WF_VAR wf_fxtried,1
WF_VAR wf_fxmode,1
WF_VAR wf_fxt0,2
WF_VAR wf_fxdt,1
WF_VAR wf_fxboard,1
WF_VAR wf_fxcell,1
WF_VAR wf_fxlx,1
WF_VAR wf_fxly,1
WF_VAR wf_fxtx,1
WF_VAR wf_fxty,1
WF_VAR wf_fxdx,1
WF_VAR wf_fxdy,1
WF_VAR wf_fxn,1
WF_VAR wf_fxk,1
WF_VAR wf_fxland,1
WF_VAR wf_fxdir,1
WF_VAR wf_fxspr,WF_FXSPR*2
WF_VAR wf_fxnew,WF_FXSPR*2
WF_VAR wf_fxstash,1
WF_VAR wf_fxreplay,1
WF_VAR wf_fxframe,64
WF_VAR wf_fxcache,200
WF_VAR wf_rowfx,10
WF_VAR wf_fxnow,1
WF_VAR wf_fxstamp,1
WF_VAR wf_fxand,2
WF_VAR wf_fxor,2
WF_VAR wf_fxtmp,24
WF_VAR wf_fxrows,4
WF_VAR wf_fxpseg,2
WF_VAR wf_fxptried,1
wf_savepad equ (512-((os88_image_end-$$+WF_BSS)&511))&511
wf_savebuf equ os88_image_end+WF_BSS+wf_savepad
    OS88_BSS WF_BSS+wf_savepad+512
    OS88_IMAGE_END
