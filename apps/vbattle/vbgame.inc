; =============================================================================
; os8088 - apps/vbattle/vbgame.inc - THE BATTLE'S TURNS AND MENUS (SPEC.md
; 126.6): the characters in order, then the enemy; a character's menu tree
; (its top menu, a sub-menu Enter opens, the side menu Left or Right opens,
; Esc or Left/Right back), a target picked when the action wants an ally;
; each action PLANNED by the rules (vbrules.inc), its clip played, its
; effects applied when the clip is over, and the enemy's RESPONSE after.
; The UI task's; the hook only plays what [vb_pend] names.
; =============================================================================

GS_TARGET   equ 4               ; an ally is being picked

; vb_game_init - the battle from its start
vb_game_init:
    mov byte [vb_state], GS_INTRO
    mov byte [vb_turn], 0xFF
    mov byte [vb_cursor], 0
    mov byte [vb_pend], 0xFF
    mov word [vb_pk], 0xFFFF
    mov byte [vb_want], 0xFF
    mov byte [vb_cdone], 0xFF
    mov byte [vb_quit], 0
    mov word [vb_key], 0
    mov byte [vb_mode], VM_OFF
    mov byte [vb_dirty], 0xFF
    mov byte [vb_menu], 0
    mov byte [vb_pmenu], NONE
    push ds
    pop es
    mov di, vb_zstart               ; every count of the battle's state
    mov cx, vb_zend - vb_zstart
    xor al, al
    cld
    rep stosb
    mov ax, [RL+RH_SEED]            ; the generator: the bundle's seed, or
    test byte [RL+RH_FLAGS], 1      ; the clock's
    jnz .seed
    call OSAPI_GET_TICKS
.seed:
    or ax, ax
    jnz .s1
    inc ax
.s1:
    mov [vb_rng], ax
    xor bx, bx
.c:
    cmp bl, [RL+RH_NCH]
    jae .e
    call vb_char
    mov di, bx
    shl di, 1
    mov ax, [si+CH_HP]
    mov [vb_hp+di], ax
    mov ax, [si+CH_MP]
    mov [vb_mp+di], ax
    inc bx
    jmp .c
.e:
    mov ax, [RL+RH_EHP]
    mov [vb_ehp], ax
    mov ax, [RL+RH_EMP]
    mov [vb_emp], ax
    mov al, [RL+RH_START]
    mov [vb_stance], al
    mov byte [vb_sret], NONE
    mov byte [vb_efol], NONE        ; (no follow-up owed at the start)
    call vb_lay_init                ; (the band's layout, 126.5.1)
    mov word [vb_mlow], 0xFFFF      ; (the music's lowest, after a rebuild)
    xor bx, bx                      ; the party's items
.i:
    cmp bl, [RL+RH_NITEM]
    jae .msg
    mov ax, ITEM_SZ
    mul bl
    add ax, [RL+RH_OITEM]
    mov si, ax
    mov ax, [RL+si+8]
    mov di, bx
    shl di, 1
    mov [vb_items+di], ax
    inc bx
    jmp .i
.msg:
    call vb_eguess
    mov di, vb_mbuf                 ; "SCORPION appears!"
    mov si, RL+RH_ENAME
    mov cx, 10
    call vb_strncpy
    mov si, vb_s_appears
    call vb_strcpy
    mov word [vb_msg], vb_mbuf
    ret

ITEM_SZ     equ 12

vb_game:
    mov ax, [vb_key]
    mov word [vb_key], 0
    mov bl, [vb_state]
    cmp bl, GS_INTRO
    je .intro
    cmp bl, GS_MENU
    jne .g1
    jmp vb_menu_key
.g1:
    cmp bl, GS_TARGET
    jne .g2
    jmp vb_target_key
.g2:
    cmp bl, GS_WAIT
    je .wait
    cmp byte [vb_eplay], 0          ; GS_END: Esc, and on an END loop Enter
    je .r                           ; or Space - the screen fades, and the
    call .go                        ; play starts over (126.3.1)
    jne .r
    mov byte [vb_fwant], 3
    ret
.go:                                ; ZF=1 AL is Enter or Space, and no
    cmp byte [vb_fon], 0            ; fade is under way
    jne .gx
    test byte [vb_meta+VH_HFLAGS], HF_ANYKEY
    jz .g0                          ; (ANY key, the battle says: Esc never
    or ax, ax                       ; gets here - it leaves. AX = 0 is NO
    jz .gn                          ; key: the dispatch runs every pass)
    cmp al, al
    ret
.gn:
    or ax, 1                        ; (ZF=0)
    ret
.g0:
    cmp al, 13
    je .gx
    cmp al, ' '
.gx:
    ret
.intro:
    cmp byte [vb_tplay], 0          ; THE TITLE goes round until Enter or
    je .in                          ; Space: then a fade, or its clip
    cmp byte [vb_lwant], 0
    jne .r
    call .go
    jne .r
    cmp byte [vb_meta+VH_LEAVE], 0xFF
    jne .lc
    mov byte [vb_fwant], 1
    ret
.lc:
    mov byte [vb_lwant], 1
    ret
.in:
    cmp byte [vb_bandok], 0         ; THE SCENE OPENING'S first frame is up:
    je .r                           ; the band under it, the menu not yet
    cmp byte [vb_banddone], 0
    jne .i2
    mov byte [vb_banddone], 1
    mov byte [vb_gkey], 0xFE        ; (a ground never drawn: rebuilt)
    or byte [vb_dirty], 8
.i2:
    cmp byte [vb_mode], VM_IDLE     ; ...and it has seamed onto the loop
    jne .r
    mov byte [vb_cdone], 0xFF
    jmp vb_next_turn
.wait:
    mov al, [vb_cdone]
    cmp al, [vb_wclip]
    jne .r
    mov byte [vb_cdone], 0xFF
    jmp vb_finish_plan
.r:
    ret

; --- the menus ----------------------------------------------------------------
; vb_item - AL = item [vb_cursor] of menu [vb_menu]'s index, AH its kind (0
; an action, 1 a sub-menu); SI the menu; CX its items
vb_item:
    mov al, [vb_menu]
    call vb_menurec
    mov cl, [si+MN_N]
    xor ch, ch
    mov bl, [vb_cursor]
    xor bh, bh
    shl bx, 1
    mov ax, [si+MN_ITEMS+bx]
    ret

vb_menu_key:
    or ax, ax
    jnz .k
    jmp vb_hover
.k:
    cmp ah, 0x48                    ; Up
    je .up
    cmp ah, 0x50                    ; Down
    je .down
    cmp ah, 0x4B                    ; Left
    je .side
    cmp ah, 0x4D                    ; Right
    je .side
    cmp al, 27                      ; Esc (a sub-menu or the side's)
    je .back
    cmp al, 13
    je .enter
    cmp al, ' '
    je .enter
    jmp vb_hover
.up:
    cmp byte [vb_cursor], 0
    je .h
    dec byte [vb_cursor]
    jmp short .moved
.down:
    call vb_item
    mov al, [vb_cursor]
    inc al
    xor ah, ah
    cmp ax, cx
    jae .h
    mov [vb_cursor], al
.moved:
    or byte [vb_dirty], 1
    mov al, [vb_meta+VH_UI]         ; MOVE
    call vb_say
.h:
    jmp vb_hover
.side:
    cmp byte [vb_pmenu], NONE
    jne .back
    mov bl, [vb_turn]
    call vb_char
    mov al, [si+CH_SIDE]
    cmp al, NONE
    je .h
    jmp short .open
.back:
    cmp byte [vb_pmenu], NONE
    je .h
    mov al, [vb_pmenu]
    mov [vb_menu], al
    mov al, [vb_pcur]
    mov [vb_cursor], al
    mov byte [vb_pmenu], NONE
    or byte [vb_dirty], 9
    mov al, [vb_meta+VH_UI+2]       ; CLOSE
    call vb_say
    jmp vb_hover
.enter:
    call vb_item
    or ah, ah
    jz .action
.open:                              ; AL the menu to open
    mov ah, [vb_menu]
    mov [vb_pmenu], ah
    mov ah, [vb_cursor]
    mov [vb_pcur], ah
    mov [vb_menu], al
    mov byte [vb_cursor], 0
    or byte [vb_dirty], 9
    mov al, [vb_meta+VH_UI+1]       ; OPEN
    call vb_say
    jmp vb_hover
.action:
    mov [vb_mact], al
    call vb_act
    mov bl, [vb_turn]
    call vb_canpay
    jnc .afford
    jmp vb_deny
.afford:
    cmp byte [si+AC_TGT], T_ALLY
    jne .now
    mov byte [vb_tcur], 0xFF        ; an ally: who?
    call vb_tnext_sel
    cmp byte [vb_tcur], 0xFF
    jne .t
    jmp vb_deny
.t:
    mov byte [vb_state], GS_TARGET
    mov word [vb_msg], vb_s_who
    or byte [vb_dirty], 6
    mov al, [vb_meta+VH_UI+1]       ; OPEN
    call vb_say
    jmp vb_hover
.now:
    mov al, [vb_mact]
    mov bh, NONE
    jmp vb_commit

; vb_deny - the action cannot be paid for (or has no one to aim at)
vb_deny:
    mov word [vb_msg], vb_s_cannot
    or byte [vb_dirty], 4
    mov al, [vb_meta+VH_UI+4]       ; DENY
    jmp vb_say

; vb_tnext_sel - [vb_tcur] on to the next character the action [vb_mact]
; may be aimed at (the standing; the fallen, for a REVIVE), round from
; [vb_tcur]+1; left as it was when there is none
vb_tnext_sel:
    mov al, [vb_mact]
    call vb_act
    call vb_hasrevive
    mov dl, 0                       ; DL=1: the fallen are wanted
    jz .w
    mov dl, 1
.w:
    mov cl, [RL+RH_NCH]
    xor ch, ch
    mov al, [vb_tcur]
.n:
    inc al
    cmp al, [RL+RH_NCH]
    jb .c
    xor al, al
.c:
    push ax
    call vb_isstanding
    pop ax
    mov dh, 1
    jnz .st
    mov dh, 0
.st:
    xor dh, dl                      ; standing XOR wants-the-fallen
    jnz .got
    loop .n
    ret
.got:
    mov [vb_tcur], al
    ret

vb_target_key:
    or ax, ax
    jz vb_hover
    cmp ah, 0x48
    je .move
    cmp ah, 0x50
    je .move
    cmp al, 13
    je .take
    cmp al, ' '
    je .take
    cmp al, 27
    je .back
    cmp ah, 0x4B
    je .back
    cmp ah, 0x4D
    je .back
    jmp vb_hover
.move:
    call vb_tnext_sel
    or byte [vb_dirty], 2
    mov al, [vb_meta+VH_UI]
    call vb_say
    jmp vb_hover
.back:
    mov byte [vb_state], GS_MENU
    or byte [vb_dirty], 3
    mov word [vb_msg], vb_mbuf
    mov al, [vb_meta+VH_UI+2]
    call vb_say
    jmp vb_hover
.take:
    mov al, [vb_mact]
    mov bh, [vb_tcur]
    jmp vb_commit

; vb_hover - what the read-ahead fetches while the cursor rests: the clip
; the action under it would most likely play
vb_hover:
    mov byte [vb_want], 0xFF
    cmp byte [vb_state], GS_TARGET
    je .t
    call vb_item
    or ah, ah
    jnz .o
    push ax                         ; AN ACTION THAT CANNOT BE TAKEN - its MP,
    call vb_act                     ; its item, its limit - has its clip read
    mov bl, [vb_turn]               ; for nothing: not wanted (VBATTLE-RAM-
    call vb_canpay                  ; PLAN R3, the owner's "we never need to
    pop ax                          ; preload a disabled/unreachable
    jc .o                           ; action's files")
    mov bl, [vb_turn]
    mov bh, NONE
    jmp short .p
.t:
    mov al, [vb_mact]
    mov bl, [vb_turn]
    mov bh, [vb_tcur]
.p:
    call vb_peekclip
    mov [vb_want], al
.o:
    ret

; vb_commit - character [vb_turn] takes action AL at BH: paid, planned,
; played
vb_commit:
    push ax
    push bx
    call vb_act
    mov bl, [vb_turn]
    call vb_pay
    mov al, [vb_meta+VH_UI+3]       ; TAKE
    call vb_say
    pop bx
    pop ax
    mov bl, [vb_turn]
    call vb_resolve
    jmp vb_begin_plan

; --- the turns ------------------------------------------------------------------
; vb_next_turn - the next character standing, or the enemy after the last
vb_next_turn:
.on:
    inc byte [vb_turn]
    mov al, [vb_turn]
    cmp al, [RL+RH_NCH]
    jae .enemy
    call vb_isstanding
    jz .on                          ; down: skipped
    mov bl, [vb_turn]
    call vb_start_turn
    mov bl, [vb_turn]
    call vb_char
    mov al, [si+CH_TOP]
    mov [vb_menu], al
    mov byte [vb_pmenu], NONE
    mov byte [vb_cursor], 0
    mov byte [vb_state], GS_MENU
    call vb_eguess
    or byte [vb_dirty], 0x0B
    jmp vb_hover
.enemy:
    mov byte [vb_turn], 0xFF        ; the characters' turns start again after
    mov byte [vb_want], 0xFF
    call vb_enemy_plan
    jmp vb_begin_plan

; vb_begin_plan - the plan [vb_q*] begins: its line ("SCORPION: Sting"), its
; clip to the hook; a plan with no clip is over at once
vb_begin_plan:
    mov di, vb_mbuf
    mov bl, [vb_qw]
    call vb_who
    mov ax, ': '
    stosw
    mov si, [vb_qact]
    mov cx, 8
    call vb_strncpy
    mov word [vb_msg], vb_mbuf
    mov byte [vb_state], GS_WAIT
    or byte [vb_dirty], 0x0E        ; the menu goes: the band is rebuilt
    mov al, [vb_qclip]
    cmp al, NONE
    jne .clip
    jmp vb_finish_plan
.clip:
    mov [vb_wclip], al
    mov byte [vb_cdone], 0xFF
    mov [vb_pend], al               ; the hook takes it at its anchor
    ret

; vb_who - who BL's name at DS:DI, DI past it
vb_who:
    cmp bl, ENEMY
    jne .c
    mov si, RL+RH_ENAME
    mov cx, 10
    jmp vb_strncpy
.c:
    call vb_char
    mov cx, 8
    jmp vb_strncpy

; vb_finish_plan - the plan's clip is over: its effects, its line, the
; ending if it is one, the enemy's response if it has one, the next turn
vb_finish_plan:
    call vb_apply
    call vb_say_result
    or byte [vb_dirty], 6
    cmp byte [vb_over], 0
    jne vb_end_battle
    cmp byte [vb_qw], ENEMY
    je .next
    cmp byte [vb_qresp], 0
    jne .next
    call vb_respond
    jnc .next
    jmp vb_begin_plan
.next:
    jmp vb_next_turn

; vb_end_battle - victory, defeat or an escape: its clip and its line
vb_end_battle:
    mov al, [vb_over]
    mov bl, [RL+RH_VICT]
    mov si, vb_s_victory
    mov di, VH_ENDS
    cmp al, OVER_WIN
    je .w
    mov bl, [RL+RH_DEFT]
    mov si, vb_s_defeat
    mov di, VH_ENDS+2
    cmp al, OVER_LOSE
    je .w
    mov bl, NONE                    ; escaped: its own clip has played
    mov si, vb_s_escaped
    jmp short .e
.w:
    mov ax, [vb_meta+di]            ; how it goes on, and its end loop
    mov [vb_ehow], al               ; (126.3.1): the line says what a key
    mov [vb_eloop], ah              ; does
    push di
    sub di, VH_ENDS
    shr di, 1
    mov ah, [vb_meta+VH_ETHEN+di]   ; ...and what follows it, when it plays
    mov [vb_ethen], ah              ; once
    pop di
    cmp al, EH_HOLD
    je .e
    cmp di, VH_ENDS
    jne .wd
    mov si, vb_s_victl
    cmp al, EH_AGAIN
    jne .e
    mov si, vb_s_victa
    jmp short .e
.wd:
    mov si, vb_s_defl
    cmp al, EH_AGAIN
    jne .e
    mov si, vb_s_defa
.e:
    mov [vb_msg], si
    mov byte [vb_state], GS_END
    mov byte [vb_turn], 0xFF
    mov byte [vb_want], 0xFF
    or byte [vb_dirty], 0x0F
    cmp bl, NONE
    je .o
    mov [vb_wclip], bl
    mov [vb_pend], bl
.o:
    ret

; vb_say_result - the line after the plan: "Sting: 60", "Attack: MISS",
; "Sweep: 40 MISS 38!", "Cure: +90"
vb_say_result:
    mov di, vb_mbuf
    mov si, [vb_qact]
    mov cx, 8
    call vb_strncpy
    mov al, ':'
    stosb
    xor bx, bx
.t:
    cmp bl, [vb_qn]
    jae .o
    cmp di, vb_mbuf + 34
    jae .o
    mov al, ' '
    stosb
    cmp byte [vb_qhit+bx], 0
    jne .h
    mov si, vb_s_miss
    push bx
    call vb_strcpy
    pop bx
    jmp short .n
.h:
    push bx
    shl bx, 1
    mov ax, [vb_qdmg+bx]
    mov dx, [vb_qheal+bx]
    pop bx
    or ax, ax
    jz .heal
    push bx
    call vb_utoa
    pop bx
    cmp byte [vb_qcrit+bx], 0
    je .n
    mov al, '!'
    stosb
    jmp short .n
.heal:
    or dx, dx
    jz .none
    mov al, '+'
    stosb
    mov ax, dx
    push bx
    call vb_utoa
    pop bx
    jmp short .n
.none:
    dec di                          ; (nothing to say of this one)
.n:
    inc bx
    jmp .t
.o:
    mov byte [di], 0
    mov word [vb_msg], vb_mbuf
    ret

; vb_say - menu sound AL (FFh none) for the hook to start: the foreground
; asks, the hook mixes (vb_uisound)
vb_say:
    cmp al, 0xFF
    je .out
    cmp al, [vb_meta+VH_NSFX]
    jae .out
    inc al
    mov [vb_ui], al
.out:
    ret
