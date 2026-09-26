; Startup states: 4 splash, 5 setup, 6 intro choice, 7 dancing gorillas.
; All scenes use the same backing store and adapter paths as the game.
gr_clear:
    push ds
    pop es
    xor ax, ax
    mov di, gr_scene
    mov cx, 8192
    rep stosw
    mov di, gr_hudchars
    mov cx, 256
    rep stosw
    ret

gr_intro:
    mov byte [gr_state], 4
    ; Start on the worker's first visible frame, after the initial paint.
    mov byte [gr_introseq], 0
    call gr_clear
    mov si, gr_splashtitle
    mov dx, 16
    call gr_fronttext
    mov si, gr_portcredit
    mov dx, 32
    call gr_fronttext
    mov si, gr_mission
    mov dx, 48
    call gr_fronttext
    mov si, gr_continue
    mov dx, 112
    call gr_fronttext
    call gr_frontapes
    ret

gr_setup:
    call gr_musicstop
    push ds
    pop es
    mov byte [gr_state], 5
    mov byte [gr_paused], 0
    mov byte [gr_saved], 0
    mov byte [gr_setupfield], 0
    mov word [gr_grav10], 98
    mov byte [gr_target], 3
    mov byte [gr_players], 2
    mov si, gr_default1
    mov di, gr_name1
    mov cx, 11
    rep movsb
    mov si, gr_default2
    mov di, gr_name2
    mov cx, 11
    rep movsb
    call gr_clear
    mov si, gr_setuptitle
    mov dx, 16
    call gr_fronttext
    mov si, gr_setuphelp
    mov dx, 112
    call gr_fronttext
    call gr_inputpage
    jmp gr_fullpaint

gr_nextinput:
    call gr_frontdirtyclear
    call gr_inputpage
    jmp gr_frontflush

gr_inputpage:
    mov byte [gr_inputlen], 0
    mov byte [gr_input], 0
    xor bx, bx
    mov bl, [gr_setupfield]
    shl bx, 1
    mov si, [gr_questions+bx]
    mov dx, 48
    call gr_fronttext
    mov si, [gr_defaults+bx]
    mov dx, 64
    call gr_fronttext
    jmp gr_inputtext

gr_frontdirtyclear:
    push ds
    pop es
    mov di, gr_huddirty
    xor ax, ax
    mov cx, 256
    rep stosw
    ret

gr_frontflush:
    xor si, si
    mov di, 512
    jmp gr_hudflushspan

gr_editline:
    ; Reuse the fast glyph path for startup typing, including error erasure.
    push ds
    pop es
    mov di, gr_huddirty+320
    xor ax, ax
    mov cx, 96/2
    rep stosw
    call gr_inputtext
    mov si, 320
    mov di, 416
    jmp gr_hudflushspan

gr_inputtext:
    mov si, gr_blank
    mov dx, 96
    call gr_fronttext
    mov si, gr_input
    mov dx, 80
    jmp gr_fronttext

gr_fronttext:
    xor cx, cx
    mov al, 15
    jmp gr_text

; Name entry must receive letters such as F, N and P before game shortcuts.
; Alt+Enter and Escape are handled by gr_key before this dispatcher.
gr_frontkey:
    cmp byte [gr_state], 4
    je .setup
    cmp byte [gr_state], 7
    je .play
    cmp byte [gr_state], 6
    je .choice
    cmp al, 13
    je .accept
    xor bx, bx
    mov bl, [gr_inputlen]
    cmp al, 8
    je .back
    cmp al, 32
    jb .out
    cmp al, 126
    ja .out
    cmp bx, 10
    jae .out
    cmp byte [gr_setupfield], 0
    je .count
    cmp byte [gr_setupfield], 3
    jb .append
.count:
    cmp al, '.'
    je .decimal
    cmp al, '0'
    jb .out
    cmp al, '9'
    ja .out
    jmp short .append
.decimal:
    cmp byte [gr_setupfield], 4
    jne .out
.append:
    mov [gr_input+bx], al
    inc bx
    jmp short .store
.back:
    or bx, bx
    jz .out
    dec bx
.store:
    mov [gr_inputlen], bl
    mov byte [gr_input+bx], 0
    jmp gr_editline
.setup:
    jmp gr_setup
.choice:
    and al, 0dfh
    cmp al, 'V'
    je gr_dance
    cmp al, 'P'
    je .play
    cmp al, 13
    jne .out
.play:
    call gr_musicstop
    call gr_new
    jmp gr_fullpaint
.out:
    ret
.accept:
    mov al, [gr_setupfield]
    or al, al
    jz .players
    cmp al, 3
    jae .numeric
    cmp byte [gr_inputlen], 0
    je .next
    mov di, gr_name1
    cmp al, 1
    je .copy
    mov di, gr_name2
.copy:
    push ds
    pop es
    mov si, gr_input
    xor cx, cx
    mov cl, [gr_inputlen]
    inc cx
    rep movsb
    jmp .next
.players:
    cmp byte [gr_inputlen], 0
    je .next
    cmp byte [gr_inputlen], 1
    jne .invalid
    mov al, [gr_input]
    sub al, '0'
    cmp al, 1
    jb .invalid
    cmp al, 2
    ja .invalid
    mov [gr_players], al
    cmp al, 2
    je .next
    mov si, gr_computer
    mov di, gr_name2
    push ds
    pop es
    mov cx, 9
    rep movsb
    jmp .next
.numeric:
    cmp byte [gr_inputlen], 0
    je .next
    ; Strict positive decimal, at most 99 points or 99.9 m/s^2.
    ; DI counts fractional digits; BP records the decimal separator.
    mov si, gr_input
    xor bx, bx
    xor bp, bp
    xor di, di
.parse:
    lodsb
    or al, al
    jz .parsed
    cmp al, '.'
    jne .digit
    cmp byte [gr_setupfield], 4
    jne .invalid
    or bp, bp
    jnz .invalid
    inc bp
    jmp short .parse
.digit:
    sub al, '0'
    xor ah, ah
    push ax
    mov ax, bx
    mov cx, 10
    mul cx
    mov bx, ax
    pop ax
    add bx, ax
    cmp bx, 999
    ja .invalid
    or bp, bp
    jz .parse
    inc di
    cmp di, 1
    ja .invalid
    jmp short .parse
.parsed:
    or bx, bx
    jz .invalid
    cmp byte [gr_setupfield], 4
    je .gravity
    cmp bx, 99
    ja .invalid
    mov [gr_target], bl
    jmp .next
.gravity:
    or bp, bp
    jz .whole
    or di, di
    jnz .gravitystore
    jmp short .invalid
.whole:
    cmp bx, 99
    ja .invalid
    mov ax, bx
    mov cx, 10
    mul cx
    mov bx, ax
.gravitystore:
    mov [gr_grav10], bx
.next:
    inc byte [gr_setupfield]
    cmp byte [gr_setupfield], 5
    jb gr_nextinput
    mov byte [gr_state], 6
    call gr_frontdirtyclear
    mov si, gr_blank
    mov dx, 80
    call gr_fronttext
    mov dx, 96
    call gr_fronttext
    mov dx, 112
    call gr_fronttext
    mov si, gr_choice
    mov dx, 48
    call gr_fronttext
    mov si, gr_choice2
    mov dx, 64
    call gr_fronttext
    jmp gr_frontflush
.invalid:
    call gr_frontdirtyclear
    mov si, gr_badinput
    mov dx, 96
    call gr_fronttext
    jmp gr_frontflush

gr_dance:
    mov byte [gr_state], 7
    call gr_clear
    mov si, gr_splashtitle
    mov dx, 16
    call gr_fronttext
    mov si, gr_starring
    mov dx, 32
    call gr_fronttext
    mov si, gr_name1
    mov di, gr_cast
    call gr_namecopy
    mov si, gr_name2
    mov di, gr_cast+15
    call gr_namecopy
    mov si, gr_cast
    mov dx, 48
    call gr_fronttext
    mov si, gr_skip
    mov dx, 112
    call gr_fronttext
    mov si, gr_musicdance
    call gr_musicstart
    call gr_frontapes
    call gr_animate
    jmp gr_fullpaint

gr_frontapes:
    mov word [gr_gx], 80
    mov word [gr_gx+2], 160
    mov word [gr_gy], 80
    mov word [gr_gy+2], 80
    mov byte [gr_animphase], 0
    mov byte [gr_pose], 0
    call OSAPI_GET_TICKS
    mov [gr_animdue], ax
    ret

gr_fronttick:
    cmp byte [gr_state], 4
    je .intro
    cmp byte [gr_state], 7
    jne .out
    cmp word [gr_musicptr], 0
    jne gr_animate
    call gr_new
    jmp gr_fullpaint
.out:
    ret
.intro:
    cmp word [gr_musicptr], 0
    jne gr_animate
    mov si, gr_musicintro
    cmp byte [gr_introseq], 0
    je .start
    mov si, gr_musicdance
    cmp byte [gr_introseq], 1
    jne gr_setup
.start:
    inc byte [gr_introseq]
    call gr_musicstart
    jmp gr_animate

gr_animate:
    call OSAPI_GET_TICKS
    mov dx, ax
    sub ax, [gr_animdue]
    js .out
    add dx, 3
    mov [gr_animdue], dx
    inc byte [gr_animphase]
    cmp byte [gr_animphase], 5
    jb .pose
    mov byte [gr_animphase], 0
.pose:
    xor byte [gr_pose], 1
    jmp gr_frontsprites
.out:
    ret

%include "grdraw.inc"

; Frontend exposure/mode changes: clear video once, then use font bands.
; Converting all 32K logical pixels through the scene scaler costs seconds
; on an XT, even though almost the entire menu is empty sky.
gr_frontpaint:
    push ds
    pop es
    cmp byte [gr_vga], 0
    jne .vga
    cmp byte [gr_cga], 0
    jne .cga
    call gr_background
    jmp short .text
.vga:
    mov dx, 03c4h
    mov ax, 0f02h
    out dx, ax
    mov ax, 0a000h
    mov es, ax
    xor di, di
    xor ax, ax
    mov cx, 19200
    rep stosw
    jmp short .text
.cga:
    mov ax, 0b800h
    mov es, ax
    xor di, di
    xor ax, ax
    mov cx, 8192
    rep stosw
.text:
    ; The cleared background already supplies spaces and unused cells.
    xor bx, bx
.cell:
    xor al, al
    cmp byte [gr_hudchars+bx], 32
    jbe .mark
    inc ax
.mark:
    mov [gr_huddirty+bx], al
    inc bx
    cmp bx, 512
    jb .cell
    call gr_frontflush
    cmp byte [gr_state], 4
    je gr_frontsprites
    cmp byte [gr_state], 7
    je gr_frontsprites
    ret

gr_namecopy:
    ; Copy a name into a ten-column, space-padded display field.
    push ax
    push cx
    mov cx, 10
.loop:
    mov al, ' '
    cmp byte [si], 0
    je .space
    lodsb
.space:
    mov [di], al
    inc di
    loop .loop
    pop cx
    pop ax
    ret

; Nonblocking, instance-owned score. Notes expire even when the window loses
; focus; the worker resumes the sequence only while the window can advance.
gr_musicservice:
    cmp word [gr_musicptr], 0
    je .out
    cmp byte [gr_paused], 0
    jne .out
    cmp byte [gr_abon], 0
    jne .out
    SAVE
    call gr_musictick
    RESTORE
.out:
    ret

gr_musicstart:
    mov [gr_musicptr], si
    call OSAPI_GET_TICKS
    mov [gr_musicdue], ax
    jmp gr_musictick

gr_musicstop:
    mov word [gr_musicptr], 0
    xor ax, ax
    xor cx, cx
    mov dl, 40h
    call OSAPI_SND_TONE
    ret

gr_musictick:
    mov si, [gr_musicptr]
    or si, si
    jz .out
    call OSAPI_GET_TICKS
    mov dx, ax
    sub ax, [gr_musicdue]
    js .out
    ; Preserve musical timing across drawing; resync after a long focus pause.
    cmp ax, 9
    ja .resync
    mov dx, [gr_musicdue]
.resync:
    lodsw
    cmp ax, 0ffffh
    je gr_musicstop
    mov cx, [si]
    add si, 2
    mov [gr_musicptr], si
    add dx, cx
    mov [gr_musicdue], dx
    mov dl, 40h
    call OSAPI_SND_TONE
.out:
    ret

gr_splashtitle: db '      G O R I L L A S',0
gr_portcredit: db '   After Microsoft QBasic 1990',0
gr_mission: db '  Lob bananas over the skyline!',0
gr_continue: db '   Press any key for setup',0
gr_setuptitle: db '         GAME SETUP',0
gr_questions: dw gr_qplayers, gr_qname1, gr_qname2, gr_qpoints, gr_qgravity
gr_defaults: dw gr_defplayers, gr_defname1, gr_defname2, gr_defpoints, gr_defgravity
gr_qname1: db 'Name of Player 1 (10 characters)',0
gr_qname2: db 'Name of Player 2 (10 characters)',0
gr_qpoints: db 'Play how many total points? 1-99',0
gr_qgravity: db 'Gravity in m/s^2 (0.1 to 99.9)',0
gr_defname1: db 'Default: Player 1',0
gr_defname2: db 'Default: Player 2 / Computer',0
gr_defpoints: db 'Default: 3',0
gr_defgravity: db 'Default: 9.8 (Earth)',0
gr_setuphelp: db 'Backspace: edit   Enter: next',0
gr_badinput: db 'Enter a positive value in range.',0
gr_default1: db 'Player 1',0,0,0
gr_default2: db 'Player 2',0,0,0
gr_choice: db '       V = View intro',0
gr_choice2: db '       P = Play game',0
gr_starring: db '           STARRING',0
gr_cast: db '           AND           ',0
gr_skip: db '     Press any key to play',0

gr_qplayers: db 'Number of players (1 or 2)',0
gr_defplayers: db 'Default: 2 (1 plays computer)',0
gr_computer: db 'Computer',0
