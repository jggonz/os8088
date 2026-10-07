; Disk-resident game artwork. The idle fullscreen font cache holds up to 16
; decoded rows; mode entry rebuilds it. dm_queue holds clipped fallback pixels.
%include "dm-front.inc"
dm_frontgeom:
    mov bx, [dm_win]
    call OSAPI_WM_DISPLAY
    mov byte [dm_frontcolor], 0
    cmp dh, 1
    jbe .mono
    inc byte [dm_frontcolor]
.mono:
    mov bx, [dm_win]
    call OSAPI_FSX_CAPS
    and ax, (1 << FSXM_MODEX) | (1 << FSXM_CGA320)
    mov [dm_frontplay], al
    call OSAPI_WM_CONTENT
    mov [dm_frontx], ax
    mov [dm_fronty], dx
    call OSAPI_WM_GEOM
    mov byte [dm_frontscale], 1
    mov word [dm_frontheight], 132
    cmp dx, 260
    jb .out
    inc byte [dm_frontscale]
    shl word [dm_frontheight], 1
.out: ret

dm_frontpaint:
    call dm_frontgeom
    clc
    call OSAPI_GFX_PEN
    mov ax, [dm_frontx]
    mov bx, [dm_fronty]
    add cx, ax
    add dx, bx
    dec cx
    dec dx
    call OSAPI_GFX_FILL
    ; Let the first paint return before disk I/O: controls appear immediately.
    cmp byte [dm_artkind], 0
    je .pending
    call dm_frontload
.pending:
    mov word [dm_fronttextfrom], 0
    xor ax, ax
    mov cx, [dm_reveal]
    cmp cx, [dm_frontheight]
    jbe .band
    mov cx, [dm_frontheight]
.band:
    call dm_frontband
    mov word [dm_fronttextfrom], 0
    mov ax, [dm_frontheight]
    mov [dm_fronttextto], ax
    jmp dm_frontlabels

dm_frontreset:
    mov word [dm_reveal], 0
    mov bx, [dm_win]
    mov ax, 1
    call OSAPI_WM_TIMER
    ret

; One window-shade step per existing music timer. No worker or blocking wait.
dm_fronttick:
    cmp byte [dm_fs], 0
    jne .out
    cmp byte [dm_abon], 0
    jne .out
    mov ax, [dm_reveal]
    cmp ax, [dm_frontheight]
    jae .out
    mov bx, [dm_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    call dm_frontgeom
    call dm_frontload
    cmp word [dm_artseg], 0
    jne .ready
    mov ax, [dm_frontheight]
    mov [dm_reveal], ax
    jmp dm_frontpaint
.ready:
    mov ax, [dm_reveal]
    mov [dm_fronttextfrom], ax
    add ax, 12
    cmp ax, [dm_frontheight]
    jbe .step
    mov ax, [dm_frontheight]
.step:
    mov [dm_fronttextto], ax
    mov cx, ax
    sub cx, [dm_reveal]
    xchg ax, [dm_reveal]
    call dm_frontband
    call dm_frontlabels
.out: ret

dm_frontload:
    xor bx, bx
    cmp byte [dm_frontcolor], 0
    jne .kind
    inc bx
    cmp byte [dm_frontscale], 2
    je .kind
    inc bx
.kind:
    inc bx
    cmp bl, [dm_artkind]
    je .out
    mov [dm_artkind], bl
    dec bx
    shl bx, 1
    shl bx, 1
    mov si, [dm_artfiles+bx]
    mov cx, [dm_artfiles+bx+2]
    mov [dm_artsize], cx
    mov dx, [dm_artseg]
    or dx, dx
    jz .claim
    call OSAPI_MEM_FREE
.claim:
    mov word [dm_artseg], 0
    mov ax, cx
    add ax, 1023
    push cx
    mov cl, 10
    shr ax, cl
    pop cx
    call OSAPI_MEM_CLAIM
    jc .out
    mov [dm_artseg], dx
    mov es, dx
    xor bx, bx
    xor dx, dx
    call OSAPI_FILE_READ
    jc .bad
    or dx, dx
    jnz .bad
    cmp ax, [dm_artsize]
    jne .bad
    cmp word [es:0], 'DM'
    jne .bad
    cmp word [es:2], 'F1'
    jne .bad
    cmp word [es:4], 432
    jne .bad
    mov ax, [dm_frontheight]
    cmp byte [dm_frontcolor], 0
    je .height
    mov ax, 264
.height:
    cmp ax, [es:6]
    jne .bad
    mov ax, 1
    cmp byte [dm_frontcolor], 0
    je .depth
    mov al, 4
.depth:
    cmp ax, [es:8]
    jne .bad
    mov dx, [dm_artseg]
    mov ax, dm_artreloc
    call OSAPI_MEM_MOVABLE
.out: ret
.bad:
    mov dx, [dm_artseg]
    call OSAPI_MEM_FREE
    mov word [dm_artseg], 0
    ret
dm_artreloc:
    mov [dm_artseg], dx
    ret

; AX=first output row, CX=count; the file directory addresses compressed rows.
dm_frontband:
    cmp word [dm_artseg], 0
    je .ret
    or cx, cx
    jz .ret
    mov [dm_frontrow], ax
    add ax, cx
    mov [dm_frontend], ax
    mov bx, [dm_win]
    call OSAPI_WM_CLIP_SET   ; real ownership clip, including during W_PAINT
    jc .ret
    ; Native planar copying refuses a region clip. Only disarm it after
    ; proving this entire band belongs to us; covered bands keep clipping.
    mov dx, ax
    add dx, [dm_fronty]
    dec dx
    mov bx, [dm_frontrow]
    add bx, [dm_fronty]
    mov ax, [dm_frontx]
    add ax, 8
    mov cx, ax
    add cx, 431
    call OSAPI_WM_CLIP_TEST
    jc .row
    call OSAPI_WM_CLIP_CLEAR
.row:
    mov ax, [dm_frontrow]
    cmp ax, [dm_frontend]
    jae .out
    mov [dm_frontbatch], ax
    add ax, 16
    cmp ax, [dm_frontend]
    jbe .limit
    mov ax, [dm_frontend]
.limit:
    mov [dm_frontptr], ax       ; exclusive row limit while decoding
    mov word [dm_frontstride], 54
    cmp byte [dm_frontcolor], 0
    je .decodebegin
    mov word [dm_frontstride], 216
.decodebegin:
    mov di, dm_fontvga
.decode:
    mov ax, [dm_frontrow]
    mov es, [dm_artseg]
    cmp byte [dm_frontcolor], 0
    je .scene
    cmp byte [dm_frontscale], 2
    je .scene
    shl ax, 1                  ; compact color window samples alternate rows
.scene:
    cmp byte [dm_help], 0
    je .index
    add ax, [es:6]
.index:
    shl ax, 1
    mov bx, ax
    mov si, [es:bx+10]
    call dm_frontdecode
    jc .out
    inc word [dm_frontrow]
    mov ax, [dm_frontrow]
    cmp ax, [dm_frontptr]
    jb .decode
    push ds
    pop es
    mov si, dm_fontvga
    mov ax, [dm_frontx]
    add ax, 8
    mov bx, [dm_fronty]
    add bx, [dm_frontbatch]
    mov cx, 432
    mov dx, [dm_frontrow]
    sub dx, [dm_frontbatch]
    mov bp, [dm_frontstride]
    cmp byte [dm_frontcolor], 0
    je .mono
    mov di, 54
    call OSAPI_GFX_BLITP
    jnc .row
    ; Region clipping/display seams: convert and draw one row at a time.
    mov word [dm_frontptr], dm_fontvga
.fallback:
    mov si, [dm_frontptr]
    mov di, dm_queue
    mov cx, 108
    rep movsw
    mov [dm_frontptr], si
    call dm_frontpacked
    mov si, dm_queue+216
    mov ax, [dm_frontx]
    add ax, 8
    mov bx, [dm_fronty]
    add bx, [dm_frontbatch]
    mov cx, 432
    mov dx, 1
    mov bp, 216
    call OSAPI_GFX_BLIT4
    inc word [dm_frontbatch]
    mov ax, [dm_frontbatch]
    cmp ax, [dm_frontrow]
    jb .fallback
    jmp .row
.mono:
    call OSAPI_GFX_BLIT1
    jmp .row
.out:
    mov bx, [dm_win]
    call OSAPI_WM_CLIP_SET
.ret: ret

; Native four-plane row -> packed fallback, using the spare half of dm_queue.
dm_frontpacked:
    SAVE
    mov si, dm_queue
    mov di, dm_queue+216
    mov dx, 54
.byte:
    mov cx, 4
.pair:
    xor ax, ax
%rep 2
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1
%assign plane 3
%rep 4
    shl byte [si+plane*54], 1
    rcl ah, 1
%assign plane plane-1
%endrep
    and ah, 15
    or al, ah
%endrep
    stosb
    loop .pair
    inc si
    dec dx
    jnz .byte
    RESTORE
    ret

; Bounds-check input and output for every packet from the supporting file.
dm_frontunpack:
    mov di, dm_queue
; ES:SI input, DS:DI output. Exchange segments once per row so the 8088 can
; use REP for literal/repeated spans, retaining both input and output bounds.
dm_frontdecode:
    mov bx, [dm_artsize]
    mov bp, 54
    cmp byte [dm_frontcolor], 0
    je .segments
    mov bp, 216
.segments:
    push ds
    push es
    push ds
    push es
    pop ds
    pop es
    cld
.packet:
    cmp si, bx
    jae .bad
    lodsb
    or al, al
    jz .end
    xor cx, cx
    mov cl, al
    test al, 80h
    jz .repeat
    and cl, 7fh
    inc cx
    cmp cx, bp
    ja .bad
    sub bp, cx
    mov ax, si
    add ax, cx
    jc .bad
    cmp ax, bx
    ja .bad
    rep movsb
    jmp .packet
.repeat:
    cmp cx, bp
    ja .bad
    sub bp, cx
    cmp si, bx
    jae .bad
    lodsb
    rep stosb
    jmp .packet
.end:
    or bp, bp
    jnz .bad
    clc
    jmp .out
.bad:
    stc
.out:
    pop es
    pop ds
    ret

dm_frontlabels:
    cmp word [dm_artseg], 0
    jne .page
    mov si, dm_artmissing
    cmp byte [dm_artkind], 0
    jne .message
    mov si, dm_artloading
.message:
    mov cx, 24
    mov dx, 12
    mov ax, CYELLOW
    call dm_fronttext
.page:
    cmp byte [dm_help], 0
    jne .help
    call dm_frontcompose
    mov di, dm_splashlabels
    jmp .labels
.help:
    mov di, dm_helplabels
.labels:
    mov cx, [di]
    cmp cx, -1
    je .out
    mov dx, [di+2]
    mov ax, [di+4]
    mov si, [di+6]
    cmp byte [dm_frontplay], 0
    jne .draw
    cmp si, dm_helpback
    jne .start
    mov si, dm_helpunsupported
    jmp .draw
.start:
    cmp si, dm_frontstart
    jne .draw
    mov si, dm_frontunsupported
.draw:
    call dm_fronttext
    add di, 8
    jmp .labels
.out: ret

; Settings keys never decode artwork or clear the content area. Help contains
; no live settings, so it needs no pixels changed at all.
dm_frontsettings:
    cmp byte [dm_help], 0
    jne .out
    mov bx, [dm_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    call dm_frontgeom
    call dm_frontcompose
    mov word [dm_fronttextfrom], 0
    mov ax, [dm_frontheight]
    mov [dm_fronttextto], ax
    mov cx, 112
    mov dx, 115
    mov ax, CWHITE
    mov si, dm_frontsetting
    call dm_fronttext
.out: ret

dm_frontcompose:
    call dm_settingcompose
    mov ax, [dm_setting+6]
    mov [dm_frontsetting+3], ax
    mov ax, [dm_setting+16]
    mov [dm_frontsetting+7], ax
    mov al, [dm_setting+18]
    mov [dm_frontsetting+9], al
    mov si, dm_setting+27
    mov di, dm_frontsetting+12
    mov cx, 5
.music:
    lodsb
    mov [di], al
    inc di
    loop .music
    ret

dm_fronttext:
    SAVE
    cmp byte [dm_frontscale], 2
    jne .position
    shl dx, 1
.position:
    cmp dx, [dm_fronttextto]
    jae .out
    mov bx, dx
    add bx, 8
    cmp bx, [dm_fronttextfrom]
    jbe .out
    add cx, [dm_frontx]
    add dx, [dm_fronty]
    cmp byte [dm_frontcolor], 0
    jne .ink
    mov ax, CWHITE
.ink:
    call OSAPI_FONT_RUN
.out:
    RESTORE
    ret

dm_artfiles: dw dm_artvga,DM_FRONT_VGA_SIZE,dm_artherc,DM_FRONT_HRC_SIZE,dm_artcga,DM_FRONT_CGA_SIZE
dm_artvga: db 'DRMARCO.VGA',0
dm_artherc: db 'DRMARCO.HRC',0
dm_artcga: db 'DRMARCO.CGA',0
dm_artmissing: db 'Graphics unavailable: keep DRMARCO.* together',0
dm_artloading: db 'LOADING ARTWORK...',0
dm_splashlabels:
    dw 128,87,CWHITE,dm_frontstart
    dw 128,101,CYELLOW,dm_fronthelp
    dw 112,115,CWHITE,dm_frontsetting
    dw 112,124,CLCYAN,dm_frontsetup
    dw -1
dm_helplabels:
    dw 24,12,CYELLOW,dm_helptitle
    dw 24,29,CLRED,dm_helpgoal
    dw 24,40,CWHITE,dm_helpclear
    dw 24,51,CLCYAN,dm_helpmove
    dw 24,62,CWHITE,dm_helprotate
    dw 24,73,CLGREEN,dm_helppause
    dw 24,84,CWHITE,dm_helpmusic
    dw 24,95,CYELLOW,dm_helpsetup
    dw 24,118,CLCYAN,dm_helpback
    dw -1
dm_frontstart: db 'ENTER  START GAME',0
dm_fronthelp: db 'H      HOW TO PLAY',0
dm_frontsetting: db 'LV 00  LOW  FEVER',0,0
dm_frontsetup: db 'L/R:LV S:SPD M:MUSIC',0
dm_frontunsupported: db 'VGA/CGA TO PLAY',0
dm_helptitle: db 'H O W  T O  P L A Y',0
dm_helpgoal: db 'MATCH FOUR OF THE SAME COLOR.',0
dm_helpclear: db 'Clear every virus to finish.',0
dm_helpmove: db 'Left/Right: move   Down: drop',0
dm_helprotate: db 'Z / X or Up: rotate capsule',0
dm_helppause: db 'P: pause       N: new game',0
dm_helpmusic: db 'M: music       Esc: desktop',0
dm_helpsetup: db 'Setup: Left/Right level, S speed',0
dm_helpback: db 'H / Esc: BACK    ENTER: PLAY',0
dm_helpunsupported: db 'H / Esc: BACK   VGA/CGA: PLAY',0
