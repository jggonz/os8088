; Desktop splash and native release-fired controls (SPEC.md 108.4).
; Artwork is embedded, with no file access, worker or separate heap claim.
WB_FRONT_W equ 416
WB_FRONT_BIG equ 250

wb_frontgeom:
    mov bx, [wb_win]
    call OSAPI_WM_CONTENT
    mov [wb_ox], ax
    mov [wb_oy], dx
    call OSAPI_WM_GEOM
    mov [wb_frontw], cx
    mov [wb_fronth], dx
    mov word [wb_arth], WB_ART_H/2
    mov word [wb_buttony], 84
    cmp dx, WB_FRONT_BIG
    jb .size
    mov word [wb_arth], WB_ART_H
    mov word [wb_buttony], 190
.size:
    mov ax, cx
    sub ax, WB_ART_W
    shr ax, 1
    mov [wb_artx], ax
    call OSAPI_WM_DISPLAY
    mov byte [wb_frontmono], 0
    cmp dh, 1
    ja .rects
    mov byte [wb_frontmono], 1
.rects:
    mov ax, [wb_ox]
    add ax, 16
    mov bx, [wb_oy]
    add bx, [wb_buttony]
    mov di, wb_brects
    mov cx, 2
.row:
    mov [di], ax
    mov [di+2], bx
    mov dx, ax
    add dx, 183
    mov [di+4], dx
    mov dx, bx
    add dx, 18
    mov [di+6], dx
    mov dx, ax
    add dx, 200
    mov [di+8], dx
    mov [di+10], bx
    add dx, 183
    mov [di+12], dx
    mov dx, bx
    add dx, 18
    mov [di+14], dx
    add di, 16
    add bx, 25
    cmp word [wb_arth], WB_ART_H
    jne .next
    add bx, 5
.next:
    loop .row
    mov word [wb_blabels], wb_seriallabel
    mov word [wb_blabels+2], wb_practicelabel
    mov word [wb_blabels+4], wb_helplabel
    cmp byte [wb_help], 0
    je .sound
    mov word [wb_blabels+4], wb_backlabel
.sound:
    mov word [wb_blabels+6], wb_soundonlabel
    cmp byte [wb_muted], 0
    je .out
    mov word [wb_blabels+6], wb_soundofflabel
.out:
    ret

wb_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call wb_frontgeom
    ; Opaque title strip covers the top. Only the surrounding margins/body
    ; need a background fill; no white-fill/black-fill flash before the art.
    mov al, CBLACK
    call OSAPI_SET_COLOR
    cmp word [wb_artx], 0
    je .art
    xor ax, ax
    xor bx, bx
    mov cx, [wb_artx]
    dec cx
    mov dx, [wb_arth]
    dec dx
    call wb_frontfill
    mov ax, [wb_artx]
    add ax, WB_ART_W
    xor bx, bx
    mov cx, [wb_frontw]
    dec cx
    mov dx, [wb_arth]
    dec dx
    call wb_frontfill
.art:
    push es
    push ds
    pop es
    mov si, wb_artcolor
    mov bp, WB_ART_W/2
    mov ax, [wb_artx]
    add ax, [wb_ox]
    mov bx, [wb_oy]
    mov cx, WB_ART_W
    mov dx, [wb_arth]
    cmp dx, WB_ART_H
    je .format
    shl bp, 1                  ; alternate source rows on compact displays
.format:
    cmp byte [wb_frontmono], 0
    jne .mono
    call OSAPI_GFX_BLIT4
    jmp .body
.mono:
    mov si, wb_artmono
    mov bp, WB_ART_W/8
    cmp dx, WB_ART_H
    je .pen
    shl bp, 1
.pen:
    push ax
    mov ax, (CBLACK << 8) | CWHITE
    call OSAPI_GFX_BLIT1_PEN
    pop ax
    call OSAPI_GFX_BLIT1
.body:
    pop es
    call wb_frontbody
    pop di
    pop si
    cmp byte [wb_abon], 0
    je .out
    push si
    mov bx, si
    mov si, wb_ablines
    call os88ui_about_d
    pop si
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

wb_frontfill:
    add ax, [wb_ox]
    add bx, [wb_oy]
    add cx, [wb_ox]
    add dx, [wb_oy]
    call OSAPI_GFX_FILL
    ret

; The controls page and mute toggle don't repaint the immutable artwork.
wb_frontbody:
    mov al, CBLACK
    call OSAPI_SET_COLOR
    xor ax, ax
    mov bx, [wb_arth]
    mov cx, [wb_frontw]
    dec cx
    mov dx, [wb_fronth]
    dec dx
    call wb_frontfill
    mov di, wb_frontnormal
    cmp byte [wb_help], 0
    je .layout
    mov di, wb_frontcontrols
.layout:
    cmp word [wb_arth], WB_ART_H
    je .lines
    mov di, wb_frontcompact
    cmp byte [wb_help], 0
    je .lines
    mov di, wb_frontcompacthelp
.lines:
    mov si, [di]
    or si, si
    jz .buttons
    mov dx, [di+2]
    call wb_frontline
    add di, 4
    jmp .lines
.buttons:
    mov bx, wb_btrec
    mov al, 1
    call os88ui_btn
    mov al, 2
    call os88ui_btn
    mov al, 3
    call os88ui_btn
    mov al, 4
    call os88ui_btn
    cmp word [wb_arth], WB_ART_H
    jne .out
    mov si, wb_frontfooter
    mov dx, [wb_fronth]
    sub dx, 15
    call wb_frontline
.out:
    ret

; SI=NUL text, DX=relative y. Opaque centered text, on the black paper.
wb_frontline:
    push ax
    push cx
    push dx
    call OSAPI_FONT_WIDTH
    mov cx, [wb_frontw]
    sub cx, ax
    shr cx, 1
    add cx, [wb_ox]
    add dx, [wb_oy]
    mov ax, (CBLACK << 8) | CWHITE
    call OSAPI_FONT_RUN
    pop dx
    pop cx
    pop ax
    ret

wb_repaint:
    mov bx, [wb_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    mov si, [wb_win]
    call wb_paint
.out:
    ret
wb_frontupdate:
    mov bx, [wb_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    call wb_frontgeom
    call wb_frontbody
.out:
    ret

wb_frontsoundupdate:
    mov bx, [wb_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    call wb_frontgeom
    mov bx, wb_btrec
    mov al, 4
    call os88ui_btn             ; only this caption changed
.out:
    ret

wb_about:
    mov byte [wb_abon], 1
    mov word [wb_btrec+OS88UI_BT_N], 0
    mov bx, si
    mov si, wb_ablines
    call os88ui_about
    ret
wb_onclick:
    cmp byte [wb_abon], 0
    jne wb_dismiss
    ret                         ; the illustration is not a Play button
wb_dismiss:
    mov byte [wb_abon], 0
    mov word [wb_btrec+OS88UI_BT_N], 4
    call wb_repaint
    ret

wb_onkey:
    cmp byte [wb_abon], 0
    jne wb_dismiss
    cmp al, 27
    jne .key
    cmp byte [wb_help], 0
    je .out
    jmp .help
.key:
    or al, 0x20
    cmp al, 's'
    je .serial
    cmp al, 'l'
    je .practice
    cmp al, 'h'
    je .help
    cmp al, 'm'
    je .sound
.out:
    ret
.serial:
    xor al, al
    jmp wb_launch
.practice:
    mov al, 1
    jmp wb_launch
.help:
    xor byte [wb_help], 1
    call wb_frontupdate
    ret
.sound:
    call wb_sound_toggle
    call wb_frontsoundupdate
    ret

wb_buttonup:
    push cx
    push dx
    call wb_frontgeom           ; current origin, even after a window move
    pop dx
    pop cx
    mov bx, wb_btrec
    call os88ui_btnup
    cmp al, 1
    je .serial
    cmp al, 2
    je .practice
    cmp al, 3
    je .help
    cmp al, 4
    je .sound
    ret
.serial:
    xor al, al
    jmp wb_launch
.practice:
    mov al, 1
    jmp wb_launch
.help:
    xor byte [wb_help], 1
    jmp wb_frontupdate
.sound:
    call wb_sound_toggle
    jmp wb_frontsoundupdate
wb_buttondrag:
    push cx
    push dx
    call wb_frontgeom
    pop dx
    pop cx
    mov bx, wb_btrec
    call os88ui_btndrag
    ret

wb_bflags: dw OS88UI_FILL,OS88UI_FILL,OS88UI_FILL,OS88UI_FILL
    OS88UI_BTNREC wb_btrec, wb_brects, wb_blabels, wb_bflags, 4
wb_prefs: dw 418,295, 418,295, 418,154
wb_seriallabel: db 'Serial Match [S]',0
wb_practicelabel: db 'Practice [L]',0
wb_helplabel: db 'How to Play [H]',0
wb_backlabel: db 'Back [H]',0
wb_soundonlabel: db 'Sound: On [M]',0
wb_soundofflabel: db 'Sound: Off [M]',0

wb_frontnormal:
    dw wb_tagline,112, wb_setuptext,132, wb_movetext,150, wb_commandtext,164, wb_goaltext,178, 0
wb_frontcontrols:
    dw wb_controltitle,112, wb_movetext,132, wb_pausetext,146, wb_mutesc,160, wb_linktext,178, 0
wb_frontcompact: dw wb_compactgoal,56, wb_compactlink,69, 0
wb_frontcompacthelp: dw wb_movetext,56, wb_commandtext,69, 0
wb_tagline: db 'ONE COURT. TWO SCREENS.',0
wb_setuptext: db 'Connect a null modem. Find your opponent.',0
wb_movetext: db 'Up/Down or W/S: move your paddle',0
wb_commandtext: db 'P: pause   N: new   M: sound   Esc: desktop',0
wb_goaltext: db 'FIRST TO 11 WINS',0
wb_controltitle: db 'HOW TO PLAY',0
wb_pausetext: db 'P: pause / resume   N: start a new match',0
wb_mutesc: db 'M: sound on / off   Esc: return to desktop',0
wb_linktext: db 'Choose Serial Match on BOTH computers.',0
wb_compactgoal: db 'First to 11. Up/Down or W/S: paddle.',0
wb_compactlink: db 'Connect COM1/COM2. Press S on both to link.',0
wb_frontfooter: db 'COM1 / COM2 - automatic opponent discovery',0
