; WireFleet — Two PCs. Two fleets. One winner. SPEC.md 114.
%include "os88api.inc"
    OS88_HEADER 'WireFleet', wf_entry, 1
    OS88_ICON16
    dw 0,0,0x0180,0x03c0,0x03c0,0x07e0,0x1ff8,0x7ffe
    dw 0xffff,0xffff,0x7ffe,0x3ffc,0x7ffe,0xffff,0x6666,0
    dw 0,0,0x0180,0x0240,0x0240,0x05a0,0x1818,0x6006
    dw 0x8001,0xffff,0x7ffe,0x3ffc,0,0x9999,0x6666,0
    OS88_ICON16_END

wf_entry:
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
    mov si, wf_about
    call OSAPI_ABOUT_SET
    mov ax, wf_rightclick
    call OSAPI_WM_ONRCLICK
    mov ax, bx
    mov bx, wf_buttons
    mov si, wf_buttonup
    mov di, wf_buttondrag
    mov dx, wf_click
    call os88ui_btninit
    call wf_new
    mov bx, [wf_win]
    clc
.out:
    ret

wf_paint:
    push si
    call wf_geometry
    ; Only the margins/body are filled. Grids compose their own opaque
    ; ground; there is no white rectangle underneath the new board.
    call wf_ground
    mov byte [wf_fullpaint], 1
    call wf_boards
    call wf_header
    call wf_roster
    call wf_controls
    call wf_footer
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
    cmp al, 'y'
    je .ready
    cmp al, 'h'
    je .help
.out:
    ret
.auto:
    call wf_auto
    jmp wf_update
.clear:
    cmp byte [wf_ready], 0
    jne .out
.new:
    call wf_new
    jmp wf_repaint
.rotate:
    cmp byte [wf_ready], 0
    jne .out
    xor byte [wf_direction], 1
    jmp wf_update
.place:
    call wf_place
    jc .invalid
    call wf_nextship
    jmp wf_update
.invalid:
    mov word [wf_message], wf_cannotplace
    jmp wf_update
.mute:
    xor byte [wf_muted], 1
    jmp wf_update
.ready:
    cmp byte [wf_placed], 5
    jne .out
    mov byte [wf_ready], 1
    mov word [wf_message], wf_waitlink
    jmp wf_repaint
.help:
    mov si, [wf_win]
    jmp wf_about

; The native menu callbacks pass a zero-based item in AL and menu in AH.
wf_command:
    cmp ah, 0
    jne .fleet
    cmp al, 0
    je wf_onkey.new
    cmp al, 1
    je wf_onkey.ready
    jmp wf_onkey.mute
.fleet:
    cmp al, 0
    je wf_onkey.rotate
    cmp al, 1
    je wf_onkey.auto
    jmp wf_onkey.clear

wf_click:
    cmp byte [wf_abon], 0
    jne wf_dismiss
    push cx
    push dx
    call wf_geometry
    pop dx
    pop cx
    call wf_hitboard
    jc .roster
    mov [wf_target], al
    jmp wf_onkey.place
.roster:
    cmp byte [wf_ready], 0
    jne .out
    sub cx, [wf_ox]
    cmp cx, 310
    jb .out
    cmp cx, 580
    jae .out
    sub dx, [wf_oy]
    sub dx, [wf_gridy]
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
    sub cx, 24
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
    mov bx, ax
    mov al, [wf_btnkeys+bx]
    jmp wf_onkey.ascii
.out:
    ret
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
    jmp wf_repaint

wf_tpl:
    dw 16,24,600,414
    dw wf_title,wf_paint,wf_onkey,wf_click
wf_prefs: dw 600,414, 600,304, 600,154
    OS88_MENUSET wf_menus, wf_title, wf_command
        OS88_MENU wf_gamemenu, wf_gameitems, 3
        OS88_MENU wf_fleetmenu, wf_fleetitems, 3
    OS88_MENUSET_END wf_menus
wf_gameitems: dw wf_newlabel,wf_readylabel,wf_soundlabel
wf_fleetitems: dw wf_rotatelabel,wf_autolabel,wf_clearlabel
wf_title: db 'WireFleet',0
wf_gamemenu: db 'Game',0
wf_fleetmenu: db 'Fleet',0
wf_newlabel: db 'New fleet [N]',0
wf_readylabel: db 'Ready [Y]',0
wf_rotatelabel: db 'Rotate R',0
wf_autolabel: db 'Auto A',0
wf_clearlabel: db 'Clear C',0
wf_soundlabel: db 'Sound M',0
wf_soundoff: db 'Muted M',0
wf_btnkeys: db 0,'racym'
wf_btnlabels: dw wf_rotatelabel,wf_autolabel,wf_clearlabel,wf_readylabel,wf_soundlabel
wf_btnflags: times 5 dw 0
wf_ablines: dw wf_a1,wf_a2,wf_a3,wf_a4,0
wf_a1: db 'WireFleet for os8088',0
wf_a2: db 'Two PCs. Two fleets. One winner.',0
wf_a3: db 'Naval battles over a null modem',0
wf_a4: db '1-5 ship, R rotate, A auto, Y ready',0
wf_placehelp: db 'Place: click/Enter  R rotate  1-5 ship  Right-click remove',0
wf_allplaced: db 'All five placed. Ready [Y] locks your fleet. N: new fleet',0
wf_cannotplace: db 'Cannot place here: keep every square on water, inside the grid.',0
wf_waitlink: db 'Fleet ready. Serial link is the next delivery wave. N: new fleet',0
wf_deploytitle: db 'DEPLOY YOUR FLEET',0
wf_readytitle: db 'FLEET READY',0
%macro WF_BANNER 1
    %strlen %%length %1
    db %1
    times 75-%%length db ' '
    db 0
%endmacro
wf_bannerdeploy: WF_BANNER '  DEPLOY YOUR FLEET                    Two PCs. Two fleets. One winner.'
wf_bannerready: WF_BANNER '  FLEET READY                         Two PCs. Two fleets. One winner.'
wf_tagline: db 'Two PCs. Two fleets. One winner.',0
wf_yourfleet: db 'YOUR FLEET',0
wf_enemywaters: db 'ENEMY WATERS',0
wf_yourships: db 'YOUR SHIPS',0
wf_selectedlabel: db '  SELECTED',0
wf_placedlabel: db '  PLACED  ',0
wf_emptylabel: db '  UNPLACED',0
wf_footertext: db 'Wave 1: deployment   H: controls/About   M: sound',0
wf_axes: db 'ABCDEFGHIJ'
wf_digit: db '  ',0

%define OS88UI_ABOUT
%define OS88UI_ARM
%include "os88ui.inc"
%include "wirefleet/wfmodel.inc"
%include "wirefleet/wfvideo.inc"
%include "wirefleet/art/tiles.inc"

; The region can move: no segment pointers outlive a callback in this wave.
%assign WF_BSS 0
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
    OS88_BSS WF_BSS
    OS88_IMAGE_END
