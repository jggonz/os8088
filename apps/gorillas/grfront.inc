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
    mov si, gr_intropage
    call gr_frontlines
    jmp gr_frontapes

gr_setup:
    call gr_bgmstop
    call gr_musicstop
    xor ax, ax
    mov [gr_dancing], al
    mov [gr_throwticks], al
    mov [gr_blast], al
    mov [gr_roundwait], al
    mov [gr_paused], al
    mov [gr_saved], al
    mov [gr_setupfield], al
    push ds
    pop es
    mov byte [gr_state], 5
    mov word [gr_gwhole], 9
    mov word [gr_gfrac], 800
    call gr_gravity
    mov byte [gr_target], 3
    mov byte [gr_players], 2
    mov byte [gr_bgmenabled], 1
    mov si, gr_default1         ; both defaults into both names: each pair
    mov di, gr_name1            ; is adjacent, 11 bytes apart
    mov cx, 22
    rep movsb
    call gr_clear
    mov si, gr_setuppage
    call gr_frontlines
    call gr_inputpage
    jmp gr_fullpaint

gr_nextinput:
    call gr_frontdirtyclear
    call gr_inputpage
    jmp gr_frontflush

gr_inputpage:
    xor ax, ax
    mov [gr_inputlen], al
    mov [gr_input], al
    xor bx, bx
    mov bl, [gr_setupfield]
    mov al, bl
    add al, '0'
    mov [gr_qname+15], al       ; questions 1 and 2 name their player
    shl bx, 1
    mov si, [gr_questions+bx]
    mov dx, 48
    call gr_fronttext
    ; Every default reads 'Default: ' then its own text: two spans make
    ; exactly the cells one padded line would.
    mov si, gr_defmusic
    xor cx, cx
    mov dx, 64
    mov bp, 9
    mov al, 15
    call gr_textspan
    mov si, [gr_defaults+bx]
    mov cx, 9*8
    mov bp, 32-9
    call gr_textspan
    cmp byte [gr_setupfield], 5
    jne .input
    mov si, gr_musichelp
    mov dx, 112
    call gr_fronttext
.input:
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
    call gr_frontdirtyclear
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
    call gr_fronttext
    ; An edit or new question immediately shows the insertion position.
gr_cursorreset:
    ; Setup and the intro animations never run together; share their timer.
    call OSAPI_GET_TICKS
    add ax, 9
    mov [gr_animdue], ax
    mov byte [gr_cursor], '_'
gr_cursortext:
    ; Draw a separate glyph, keeping the answer and its terminator intact.
    xor cx, cx
    mov cl, [gr_inputlen]
    shl cx, 1
    shl cx, 1
    shl cx, 1
    mov dx, 80
    mov bp, 1
    mov al, 15
    mov si, gr_cursor
    jmp gr_textspan

gr_cursortick:
    call OSAPI_GET_TICKS
    mov dx, ax
    sub ax, [gr_animdue]
    js .out
    add dx, 9
    mov [gr_animdue], dx
    xor byte [gr_cursor], '_' ^ ' '
    call gr_cursortext
    mov si, 320
    mov di, 352
    jmp gr_hudflushspan
.out:
    ret

gr_fronttick:
    mov al, [gr_state]
    cmp al, 8
    je gr_animate
    cmp al, 5
    je gr_cursortick
    cmp al, 4
    je .intro
    cmp al, 7
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

gr_fronttext:
    xor cx, cx
    mov al, 15
    jmp gr_text

; Name entry must receive letters such as F, N and P before game shortcuts.
; Alt+Enter and Escape are handled by gr_key before this dispatcher.
gr_frontkey:
    cmp byte [gr_state], 8
    je .setup
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
    cmp byte [gr_setupfield], 5
    je .append
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
    ; Enter on an empty answer keeps that question's default.
    cmp byte [gr_inputlen], 0
    je .next
    call gr_frontanswer
    jnc .next
    call gr_frontdirtyclear     ; SI = the refusal, kept
    mov dx, 96
    call gr_fronttext
    jmp gr_frontflush
.next:
    inc byte [gr_setupfield]
    cmp byte [gr_setupfield], 6
    jae .chosen
    jmp gr_nextinput
.chosen:
    mov byte [gr_state], 6
    call gr_frontdirtyclear
    mov si, gr_choicepage
    call gr_frontlines
    jmp gr_frontflush

gr_dance:
    mov byte [gr_state], 7
    call gr_clear
    mov si, gr_name1
    mov di, gr_cast+3
    call gr_namecopy
    mov si, gr_name2
    mov di, gr_cast+18
    call gr_namecopy
    mov si, gr_dancepage
    call gr_frontlines
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

; Store a non-empty setup answer. CF=1 refused, SI = the message to show.
gr_frontanswer:
    mov al, [gr_setupfield]
    cmp al, 5
    jne .notmusic
    mov al, [gr_input]
    or al, 20h
    mov bl, 1
    mov si, gr_yes
    cmp al, 'y'
    je .musicword
    xor bl, bl
    mov si, gr_no
    cmp al, 'n'
    jne .badmusic
.musicword:
    cmp byte [gr_inputlen], 1
    je .musicstore
    mov di, gr_input
.musicchar:
    lodsb
    mov ah, [di]
    inc di
    or al, al
    jz .musicend
    or ah, 20h
    cmp al, ah
    je .musicchar
    jmp short .badmusic
.musicend:
    or ah, ah
    jnz .badmusic
.musicstore:
    mov [gr_bgmenabled], bl
    clc
    ret
.badmusic:
    mov si, gr_badmusic
    stc
    ret

.notmusic:
    or al, al
    jz .players
    cmp al, 3
    jae .numeric
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
    jmp short .ok
.players:
    cmp byte [gr_inputlen], 1
    jne .bad
    mov al, [gr_input]
    sub al, '0'
    cmp al, 1
    jb .bad
    cmp al, 2
    ja .bad
    mov [gr_players], al
    cmp al, 2
    je .ok
    mov si, gr_computer
    mov di, gr_name2
    push ds
    pop es
    mov cx, 9
    rep movsb
    jmp short .ok
.numeric:
    mov si, gr_input
    call gr_decimalparse
    jc .bad
    cmp byte [gr_setupfield], 4
    je .gravity
    or dx, dx
    jnz .bad
    cmp ax, 99
    ja .bad
    or ax, ax
    jz .bad
    mov [gr_target], al
    jmp short .ok
.gravity:
    mov bx, ax
    or bx, dx
    jz .bad
    mov [gr_gwhole], ax
    mov [gr_gfrac], dx
    call gr_gravity
.ok:
    clc
    ret
.bad:
    mov si, gr_badinput
    stc
    ret
; SI = {db logical row, dw NUL text}..., db 0: letter whole frontend lines.
gr_frontlines:
    lodsb
    or al, al
    jz .out
    cbw                         ; rows are 8..120
    xchg dx, ax
    lodsw
    push si
    xchg si, ax
    call gr_fronttext
    pop si
    jmp short gr_frontlines
.out:
    ret

%include "grdraw.inc"

; Full content (including borders around the centered logical scene).
gr_fullpaint:
    call gr_inktables
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

; Frontend exposure/mode changes: clear video once, then use font bands.
; Converting all 32K logical pixels through the scene scaler costs seconds
; on an XT, even though almost the entire menu is empty sky.
align 2
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
    mov al, [gr_state]
    cmp al, 5                   ; setup and the intro choice have no sprites
    je .out
    cmp al, 6
    je .out
    jmp gr_frontsprites
.out:
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
    cmp byte [gr_paused], 0
    jne .out
    cmp byte [gr_abon], 0
    jne .out
    SAVE
    call gr_bgmtick
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
    lodsb                       ; the pitch in bits 7..3, ticks below
    or al, al
    jz gr_musicstop
    mov [gr_musicptr], si
    mov bl, al
    shr bl, 1
    shr bl, 1
    and bx, 3eh                 ; the pitch's word in gr_musichz
    and ax, 7
    xchg cx, ax
    add dx, cx
    mov [gr_musicdue], dx
    mov ax, [gr_musichz+bx]
    mov dl, 40h
    call OSAPI_SND_TONE
.out:
    ret

align 2
gr_intropage:
    db 16
    dw gr_splashtitle
    db 32
    dw gr_portcredit
    db 48
    dw gr_mission
    db 56
    dw gr_help1
    db 64
    dw gr_help2
    db 72
    dw gr_help3
    db 104
    dw gr_help4
    db 112
    dw gr_continue
    db 0
gr_setuppage:
    db 16
    dw gr_setuptitle
    db 112
    dw gr_setuphelp
    db 0
gr_dancepage:
    db 16
    dw gr_splashtitle
    db 32
    dw gr_starring
    db 48
    dw gr_cast
    db 112
    dw gr_skip
    db 0
gr_splashtitle: db '      G O R I L L A S',0
gr_portcredit: db '   After Microsoft QBasic 1990',0
gr_mission: db ' Hit your rival with a banana.',0
gr_help1: db ' Aim with angle and velocity.',0
gr_help2: db ' Mind gravity and the rooftops.',0
gr_help3: db ' Arrow direction = wind flow.',0
gr_help4: db ' Arrow length = wind strength.',0
gr_continue: db '   Press any key for setup',0
gr_setuptitle: db '         GAME SETUP',0
gr_questions: dw gr_qplayers, gr_qname, gr_qname, gr_qpoints, gr_qgravity, gr_qmusic
gr_defaults: dw gr_defplayers, gr_default1, gr_defname2, gr_defpoints, gr_defgravity, gr_defmusic+9
gr_qname: db 'Name of Player 1 (10 characters)',0   ; digit set per question
gr_qpoints: db 'First to how many points? 1-99',0
gr_qgravity: db 'Gravity: 0.001 to 9999.999',0
gr_qmusic: db 'Gameplay music? Yes or No',0
gr_defmusic: db 'Default: Yes (Y/N also accepted)',0   ; the prefix of every default
gr_musichelp: db 'M toggles music during gameplay',0
gr_badmusic: db 'Enter Yes or No (Y/N).',0
gr_yes: db 'yes',0
gr_no: db 'no',0
gr_defname2: db 'Player 2 / Computer',0
gr_defpoints: db '3',0
gr_defgravity: db '9.8 (Earth)',0
gr_setuphelp: db 'Backspace: edit   Enter: next',0
gr_badinput: db 'Enter a positive value in range.',0
gr_default1: db 'Player 1',0,0,0
gr_default2: db 'Player 2',0,0,0
gr_choicepage: db 80
    dw gr_blank
    db 96
    dw gr_blank
    db 112
    dw gr_blank
    db 48
    dw gr_choice
    db 64
    dw gr_choice2
    db 0
gr_choice: db '       V = View intro',0
gr_choice2: db '       P = Play game',0
gr_starring: db '           STARRING',0
gr_cast: db '              AND               ',0
gr_skip: db '     Press any key to play',0

gr_qplayers: db 'Number of players (1 or 2)',0
gr_defplayers: db '2 (1 plays computer)',0
gr_computer: db 'Computer',0
gr_cursor: db '_',0
