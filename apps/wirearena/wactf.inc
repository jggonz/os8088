; wactf.inc - CAPTURE THE FLAG, and the teams it plays in (docs/plans/
; WIREARENA-CTF-PLAN.md). Included by the game (wirearena.asm).
;
; THE TEAMS: a player's or a bot's team is its slot's parity (CTF-PLAN 2) -
; wa_tm, by LOCAL index (0 our player, 1.. figure 0..), made when a match
; starts. THE FLAGS: one a team, at its BASE (the arena's, wa_gflp); the
; rules run here on whichever machine decides them - alone, this one - every
; tick, over every live player and bot in the match (wc_pos): a foe's touch
; takes a flag, a teammate's returns it where it lies dropped, a carrier
; that dies drops it, a dropped flag goes home by itself, and a carrier at
; its own flag, home, scores. FRAME gets the flags' places and the figures'
; team colours (wafb.inc); the overlay the flag line and the messages.

WC_HOME     equ 0               ; a flag's state: at its base,
WC_CAR      equ 1               ; carried (WC_CARR: by whom, a local index),
WC_DROP     equ 2               ; lying where its carrier fell
WC_SZ       equ 10              ; a flag: state, carrier (bytes), x, y, z,
WC_ST       equ 0               ; the dropped one's ticks to go home (words)
WC_CARR     equ 1
WC_X        equ 2
WC_Y        equ 4
WC_Z        equ 6
WC_TMR      equ 8
WC_WIN      equ 3               ; captures to win (CTF-PLAN 9)
WC_RET      equ 900             ; a dropped flag's ticks to go home: 30 s
WC_WINT     equ 150             ; the win shown: 5 s
WC_MSGT     equ 90              ; a message shown: 3 s
WC_DZ       equ 64              ; a flag's reach in height
WC_FBASE0   equ 24              ; a flag at rest: from 24 to 104 above its
WC_FBASE1   equ 104             ; floor; carried, over its carrier's head
WC_FCAR0    equ 104
WC_FCAR1    equ 152

%macro WC_BSS 0
WA_VAR wa_gmode,1                ; THE MATCH'S MODE: 0 free-for-all, 1 CTF
WA_VAR wa_lkgmode,1              ; ...the setup's pick (kept between matches)
WA_VAR wa_tm,4                  ; each local index's team: 0 red, 1 blue
WA_VAR wa_gflp,2                ; -> the arena's bases: a count, 0 or 2,
                                ; then x, y, node, 0 each (arenagen)
WA_VAR wa_cfl,2*WC_SZ           ; the two flags, red's then blue's
WA_VAR wa_caps,2                ; each team's captures
WA_VAR wa_cwin,2                ; ticks of a win still shown (0: playing)
WA_VAR wa_cmsg,2                ; a message: its text, its ticks left, its
WA_VAR wa_cmsgt,1               ; colour
WA_VAR wa_cmsga,1
WA_VAR wa_ctfcp,1               ; the teams' block copied to the renderer
                                ; (wa_rframe): 1 every frame, 80h once
WA_VAR wc_sst,2                 ; LINKED (CTF-PLAN 7): what the host last
WA_VAR wc_scar,2                ; sent of each flag - its state, carrier -
WA_VAR wc_scap,2                ; and of the captures and the win
WA_VAR wc_swin,1
WA_VAR wc_rst,2                 ; wc_rspick's team and who; its nearest
WA_VAR wc_rmin,2                ; foe so far
%endmacro

; wa_ctfstart - a match starts (wa_main): its mode, the teams, the flags at
; home and nobody's captures; in CTF everyone on their own side
wa_ctfstart:
    mov al, [wa_lkgmode]        ; (linked: the host's, its HELLO's)
    mov si, [wa_gflp]
    cmp word [si], 2
    je .md
    xor al, al                  ; (no bases: no CTF - the setup refuses it)
.md:
    mov [wa_gmode], al
    xor cx, cx                  ; THE TEAMS: by slot, its parity
.t:
    mov al, cl
    cmp byte [wa_lkon], 0
    je .ts
    call wa_lkcanon
.ts:
    and al, 1
    mov bx, cx
    mov [wa_tm+bx], al
    inc cx
    cmp cx, WA_NFIG + 1
    jb .t
    xor bx, bx                  ; ...and each figure's colour
.c:
    mov al, [wa_tm+bx+1]
    call wc_tatt
    cmp byte [wa_gmode], 0
    jne .cs
    xor al, al
.cs:
    mov [wa_fcol+bx], al
    inc bx
    cmp bx, WA_NFIG
    jb .c
    mov byte [wa_flags+FL_ON], 0
    mov byte [wa_flags+WA_FLSZ+FL_ON], 0
    mov al, [wa_gmode]
    mov [wa_ctfon], al
    mov byte [wa_ctfcp], 0x80   ; (free-for-all: copied once, off)
    or al, al
    je .out
    mov byte [wa_ctfcp], 1      ; (a team match: every frame)
    call wc_reset
    xor ax, ax                  ; (nothing sent yet: both home, nobody's)
    mov [wc_sst], ax
    mov [wc_scar], ax
    mov [wc_scap], ax
    mov [wc_swin], al
    call wc_side                ; EVERYONE ON THEIR SIDE (CTF-PLAN 2)
    call wc_frame
.out:
    mov byte [wa_ovd], 1
    ret

; wc_reset - the flags at home and nobody's captures. Keeps all but AX, SI,
; BP
wc_reset:
    mov word [wa_caps], 0
    mov word [wa_cwin], 0
    mov si, wa_cfl
    xor bp, bp
    call wc_home
    mov si, wa_cfl + WC_SZ
    inc bp
    ; (falls through)
; wc_home - SI = a flag, BP = its team: at its base. Keeps all but AX
wc_home:
    push bx
    mov byte [si+WC_ST], WC_HOME
    mov bx, bp
    mov al, 6
    mul bl
    add ax, [wa_gflp]
    mov bx, ax
    mov ax, [bx+2]
    mov [si+WC_X], ax
    mov ax, [bx+4]
    mov [si+WC_Y], ax
    mov word [si+WC_Z], 0
    pop bx
    mov byte [wa_ovd], 1
    ret

; wc_tatt - AL = a team: AL = the attribute its figures and its flag take,
; the adapter's - red 0Ch, blue 09h; on mono ours normal, theirs bright.
; Keeps all but AX
wc_tatt:
    cmp byte [wa_mono], 0
    jne .mono
    mov ah, 0x0C
    or al, al
    jz .o
    mov ah, 0x09
.o:
    mov al, ah
    ret
.mono:
    cmp al, [wa_tm]
    mov al, 0x07
    je .mo
    mov al, 0x0F
.mo:
    ret

; wc_pos - AL = a local index (0 our player, 1.. a figure): CF = 1 when it is
; in the match and alive, its place in AX, BX and its floor in DX. Keeps
; all but AX, BX, DX
wc_pos:
    or al, al
    jnz .fig
    cmp byte [wa_pdead], 0
    jne .no
    mov ax, [wa_px]
    mov bx, [wa_py]
    mov dx, [wa_feet]
    stc
    ret
.fig:
    push di
    dec ax
    mov ah, F_SIZE
    mul ah
    add ax, wa_figs
    mov di, ax
    call wa_lkabsent            ; (ZF: not in this match)
    je .nof
    cmp byte [di+F_DEAD], 0
    jne .nof
    mov ax, [di+F_X]
    mov bx, [di+F_Y]
    mov dx, [di+F_Z]
    pop di
    stc
    ret
.nof:
    pop di
.no:
    clc
    ret

; wc_near - AX, BX = a place, DX its floor, SI = a flag: CF = 1 within its
; reach (WA_FLR across, WC_DZ up or down). Keeps every register
wc_near:
    push ax
    sub ax, [si+WC_X]
    call wa_abs
    cmp ax, WA_FLR
    jae .no
    mov ax, bx
    sub ax, [si+WC_Y]
    call wa_abs
    cmp ax, WA_FLR
    jae .no
    mov ax, dx
    sub ax, [si+WC_Z]
    call wa_abs
    cmp ax, WC_DZ
    jae .no
    pop ax
    stc
    ret
.no:
    pop ax
    clc
    ret

; wa_ctick - the sim's tick, in CTF (wa_tick, before the pickups): the
; messages' time, a win's, and each flag's rules; then FRAME's flags
wa_ctick:
    cmp byte [wa_gmode], 1
    jne .out
    cmp byte [wa_cmsgt], 0
    je .m
    dec byte [wa_cmsgt]
    jnz .m
    mov byte [wa_ovd], 1        ; (the message gone)
.m:
    cmp byte [wa_lkon], 0       ; A GUEST (CTF-PLAN 7): the host's rules,
    je .rules                   ; told by its FLAG events - here the win's
    cmp byte [wa_lkme], 0       ; time shown, and the flags carried kept
    je .rules                   ; over their carriers
    cmp word [wa_cwin], 0
    je .gw
    dec word [wa_cwin]
.gw:
    mov si, wa_cfl
    call .follow
    mov si, wa_cfl + WC_SZ
    call .follow
    jmp wc_frame
.rules:
    cmp word [wa_cwin], 0
    je .play
    dec word [wa_cwin]
    jnz .out
    jmp wc_round
.play:
    mov si, wa_cfl
    xor bp, bp
    call wc_flag
    mov si, wa_cfl + WC_SZ
    inc bp
    call wc_flag
    call wc_send
    jmp wc_frame
.out:
    ret
.follow:                        ; SI = a flag: carried, over its carrier
    cmp byte [si+WC_ST], WC_CAR
    jne .out
    mov al, [si+WC_CARR]
    call wc_pos
    jnc .out
    mov [si+WC_X], ax
    mov [si+WC_Y], bx
    mov [si+WC_Z], dx
    ret

; wc_send - THE HOST, linked (CTF-PLAN 7): a FLAG event for each flag whose
; state or carrier changed since it was last sent - both, when the
; captures or the win did. Keeps BP
wc_send:
    cmp byte [wa_lkon], 0
    je .out
    cmp byte [wa_lkme], 0
    jne .out
    xor al, al
    cmp word [wa_cwin], 0
    je .w
    inc ax
.w:
    cmp al, [wc_swin]
    jne .both
    mov ax, [wa_caps]
    cmp ax, [wc_scap]
    jne .both
    xor bx, bx
    call .if
    mov bl, 1
.if:                            ; BX = a flag: sent if it changed
    mov si, bx
    mov al, WC_SZ
    mul bl
    add ax, wa_cfl
    xchg ax, si                 ; SI = the flag, AX = BX
    mov al, [si+WC_ST]
    cmp al, [wc_sst+bx]
    jne .one
    mov al, [si+WC_CARR]
    cmp al, [wc_scar+bx]
    jne .one
.out:
    ret
.both:
    xor bx, bx
    call .one0
    mov bl, 1
.one0:
    mov si, bx
    mov al, WC_SZ
    mul bl
    add ax, wa_cfl
    mov si, ax
.one:                           ; SI = flag BX: FLAG queued, and noted
    mov byte [wa_lkev], EV_FLAG
    mov [wa_lkev+1], bl
    mov al, [si+WC_ST]
    mov [wa_lkev+2], al
    mov [wc_sst+bx], al
    mov al, [si+WC_CARR]
    mov [wc_scar+bx], al
    call wa_lkcanon             ; (the carrier by its slot, on the wire)
    mov [wa_lkev+3], al
    mov ax, [wa_caps]
    mov [wa_lkev+4], ax
    mov [wc_scap], ax
    xor al, al
    cmp word [wa_cwin], 0
    je .ws
    inc ax
.ws:
    mov [wa_lkev+6], al
    mov [wc_swin], al
    mov ax, [si+WC_X]
    mov [wa_lkev+7], ax
    mov ax, [si+WC_Y]
    mov [wa_lkev+9], ax
    mov ax, [si+WC_Z]
    mov [wa_lkev+11], ax
    jmp wa_lkqueue

; wc_evin - SI = a FLAG event (wa_lkevin's: its payload from SI+4), on a
; guest: the flag as the host says, the captures and the win - and the
; message what changed says, as the host's own rules say it there
wc_evin:
    cmp byte [wa_gmode], 1
    jne .ret
    xor bx, bx
    mov bl, [si+4]
    cmp bl, 1
    ja .ret
    push bp
    push si
    mov bp, bx                  ; BP = its team
    mov al, WC_SZ
    mul bl
    add ax, wa_cfl
    mov di, ax                  ; DI = the flag
    mov ch, [di+WC_ST]          ; CH = its state before
    mov al, [si+5]
    mov [di+WC_ST], al
    mov al, [si+6]
    call wa_lklocal
    mov [di+WC_CARR], al
    mov ax, [si+10]
    mov [di+WC_X], ax
    mov ax, [si+12]
    mov [di+WC_Y], ax
    mov ax, [si+14]
    mov [di+WC_Z], ax
    mov word [wa_cwin], 0
    cmp byte [si+9], 0
    je .nw
    mov word [wa_cwin], WC_WINT
.nw:
    mov byte [wa_ovd], 1
    mov dx, [wa_caps]           ; DX = the captures before
    mov al, [si+7]
    mov ah, [si+8]
    mov [wa_caps], ax
    cmp ax, dx
    je .nocap
    or ax, ax
    jnz .cap
    call wa_prespawn            ; the round again: our player on our side
    jmp .out
.cap:
    xor bx, bx                  ; BX = the team that scored
    cmp al, dl
    ja .sc
    inc bx
.sc:
    push bx
    mov ax, 2400
    mov cx, 6
    mov dl, 0x40
    call OSAPI_SND_TONE
    pop bx
    mov al, bl
    call wc_tatt
    mov dl, al
    shl bx, 1
    mov ax, [wc_m_sc+bx]
    cmp word [wa_cwin], 0
    je .say
    mov ax, [wc_m_win+bx]
.say:
    call wc_say
    jmp .out
.nocap:
    mov al, [di+WC_ST]
    cmp al, ch
    je .out
    cmp al, WC_CAR
    je .take
    cmp al, WC_DROP
    je .down
    mov ax, wc_m_ohome          ; home again
    mov dx, wc_m_thome
    call wc_sayk
    jmp .out
.down:
    mov ax, wc_m_odown
    mov dx, wc_m_tdown
    call wc_sayk
    jmp .out
.take:
    mov ax, 1800
    mov cx, 3
    mov dl, 0x40
    call OSAPI_SND_TONE
    cmp byte [di+WC_CARR], 0
    jne .tk
    mov ax, wc_m_you
    mov dl, 0x0F
    call wc_say
    jmp .out
.tk:
    mov ax, wc_m_they
    mov dx, wc_m_we
    call wc_sayk
.out:
    pop si
    pop bp
.ret:
    ret

; wc_round - a win shown (the host's, or alone): the captures and the flags
; anew, everyone back on their side (the kills and deaths on Tab go on) -
; the guests told, and putting their own players back themselves
wc_round:
    call wc_reset
    call wc_side
    call wc_send
    jmp wc_frame

; wc_side - our player and the bots this machine runs (alone, or the
; host's) back on their sides. Keeps nothing
wc_side:
    call wa_prespawn
    mov di, wa_figs
.b:
    call wa_lkabsent            ; (not in the match)
    je .n
    call wa_lkremote            ; (another machine's)
    je .n
    call wa_brespawn
.n:
    add di, F_SIZE
    cmp di, wa_figs + WA_NFIG*F_SIZE
    jb .b
    ret

; wc_flag - SI = a flag, BP = its team (0 red's, 1 blue's): this tick's
; rules for it (CTF-PLAN 2). Keeps SI, BP
wc_flag:
    mov al, [si+WC_ST]
    cmp al, WC_CAR
    jne .ncar
    mov al, [si+WC_CARR]        ; CARRIED: where its carrier is - or, the
    call wc_pos                 ; carrier down, lying where it fell
    jc .alive
    mov byte [si+WC_ST], WC_DROP
    mov word [si+WC_TMR], WC_RET
    mov ax, wc_m_odown
    mov dx, wc_m_tdown
    jmp wc_sayk
.alive:
    mov [si+WC_X], ax
    mov [si+WC_Y], bx
    mov [si+WC_Z], dx
    mov di, wa_cfl              ; A CAPTURE: the carrier at its own flag,
    or bp, bp                   ; which is home (theirs: the other one)
    jnz .o
    add di, WC_SZ
.o:
    cmp byte [di+WC_ST], WC_HOME
    jne .out
    push si
    mov si, di
    call wc_near
    pop si
    jnc .out
    mov bx, 1                   ; BX = the scoring team
    sub bx, bp
    inc byte [wa_caps+bx]
    call wc_home                ; (theirs back at its base)
    mov ax, 2400
    mov cx, 6
    mov dl, 0x40
    call OSAPI_SND_TONE
    cmp byte [wa_caps+bx], WC_WIN
    jb .sc
    mov word [wa_cwin], WC_WINT ; ...and its third: the win
.sc:
    mov al, bl
    call wc_tatt
    mov dl, al                  ; (in the scorers' colour)
    shl bx, 1
    mov ax, [wc_m_sc+bx]
    cmp word [wa_cwin], 0
    je .sy
    mov ax, [wc_m_win+bx]
.sy:
    jmp wc_say
.ncar:
    cmp al, WC_DROP             ; DROPPED: its time to go home
    jne .touch
    dec word [si+WC_TMR]
    jnz .touch
    call wc_home
    mov ax, wc_m_ohome
    mov dx, wc_m_thome
    jmp wc_sayk
.touch:                         ; AT HOME OR DROPPED: who touches it - the
    xor cx, cx                  ; first of us all, each tick
.e:
    mov al, cl
    call wc_pos
    jnc .en
    call wc_near
    jnc .en
    mov bx, cx
    mov al, [wa_tm+bx]
    mov bx, bp
    cmp al, bl
    jne .take
    cmp byte [si+WC_ST], WC_DROP ; a teammate: home it goes, if it lies
    jne .en                     ; dropped
    call wc_home
    mov ax, 900
    mov cx, 3
    mov dl, 0x40
    call OSAPI_SND_TONE
    mov ax, wc_m_ohome
    mov dx, wc_m_thome
    jmp wc_sayk
.en:
    inc cx
    cmp cx, WA_NFIG + 1
    jb .e
.out:
    ret
.take:
    mov byte [si+WC_ST], WC_CAR ; a foe: it is theirs to carry
    mov [si+WC_CARR], cl
    mov ax, 1800
    mov cx, 3
    mov dl, 0x40
    call OSAPI_SND_TONE
    mov byte [wa_ovd], 1
    mov al, [si+WC_CARR]
    or al, al
    jnz .tk
    mov ax, wc_m_you            ; (our player: the HUD's)
    mov dl, 0x0F
    jmp wc_say
.tk:
    mov ax, wc_m_they           ; ...else ours that have theirs, or theirs
    mov dx, wc_m_we             ; that have ours
    ; (falls through)

; wc_sayk - SI = a flag, BP = its team; AX = the message if it is OUR
; team's flag, DX if it is theirs: shown in that flag's colour
wc_sayk:
    mov bx, bp
    cmp bl, [wa_tm]
    je .ours
    xchg ax, dx
.ours:
    push ax
    mov al, bl
    call wc_tatt
    mov dl, al
    pop ax
    ; (falls through)
; wc_say - AX = a message, DL = its attribute: shown for WC_MSGT ticks
wc_say:
    mov [wa_cmsg], ax
    mov [wa_cmsga], dl
    mov byte [wa_cmsgt], WC_MSGT
    mov byte [wa_ovd], 1
    ret

; wc_frame - FRAME's flags (wafb.inc's records): where each is, drawn over
; its base or its carrier's head, in its team's colour - the one our player
; carries not drawn (the HUD says so)
wc_frame:
    mov si, wa_cfl
    mov di, wa_flags
    xor bx, bx
.f:
    mov ax, [si+WC_X]
    mov [di+FL_X], ax
    mov ax, [si+WC_Y]
    mov [di+FL_Y], ax
    mov ax, [si+WC_Z]
    mov [di+FL_Z], ax
    mov word [di+FL_FROM], WC_FBASE0 | (WC_FBASE1 << 8)
    mov byte [di+FL_ON], 1
    cmp byte [si+WC_ST], WC_CAR
    jne .at
    mov word [di+FL_FROM], WC_FCAR0 | (WC_FCAR1 << 8)
    cmp byte [si+WC_CARR], 0
    jne .at
    mov byte [di+FL_ON], 0
.at:
    mov al, bl
    call wc_tatt
    mov [di+FL_ATT], al
    add si, WC_SZ
    add di, WA_FLSZ
    inc bx
    cmp bx, 2
    jb .f
    ret

; wc_rspick - AL = a team, AH = who (a local index): SI = that team's side's
; respawn point farthest (the sum of the axis distances) from its nearest
; live foe - the first of them, with no foe alive (CTF-PLAN 2). Keeps DI
wc_rspick:
    push di
    mov [wc_rst], ax
    mov si, [wa_grp]
    lodsw
    mov cx, ax                  ; CX = the points
    xor di, di                  ; DI = the best, BP its distance
    xor bp, bp
.p:
    mov al, [si+7]              ; (its side: 1 red, 2 blue, 0 neither)
    dec al
    cmp al, [wc_rst]
    jne .np
    or di, di
    jnz .sc
    mov di, si
.sc:
    mov word [wc_rmin], 0x7FFF
    push cx
    xor cx, cx
.e:
    cmp cl, [wc_rst+1]
    je .en
    mov bx, cx
    mov al, [wa_tm+bx]
    cmp al, [wc_rst]
    je .en
    mov al, cl
    call wc_pos
    jnc .en
    sub ax, [si]
    call wa_abs
    xchg ax, bx
    sub ax, [si+2]
    call wa_abs
    add ax, bx
    cmp ax, [wc_rmin]
    jae .en
    mov [wc_rmin], ax
.en:
    inc cx
    cmp cx, WA_NFIG + 1
    jb .e
    pop cx
    mov ax, [wc_rmin]
    cmp ax, bp
    jbe .np
    mov bp, ax
    mov di, si
.np:
    add si, 8
    loop .p
    mov si, di
    pop di
    ret

; wc_bgoal - DI = a bot at the end of a leg, at node BL (wa_bnext), in CTF:
; its goal (CTF-PLAN 6), FETCHed as a pickup is - carrying their flag, its
; own base; its own flag dropped, the node nearest it; theirs at home or
; dropped and no teammate carrying it, there. A bot that fights (engaged,
; hunting) and carries nothing fights on. Keeps all but AX
wc_bgoal:
    cmp byte [wa_gmode], 1
    jne .out0
    push bx
    push cx
    push dx
    push si
    mov dh, bl                  ; DH = the node it is at
    call wa_bidx
    inc ax
    mov cl, al                  ; CL = it, a local index
    mov bl, al
    xor bh, bh
    mov ch, [wa_tm+bx]          ; CH = its team
    mov si, wa_cfl              ; SI = THEIR flag
    or ch, ch
    jnz .th
    add si, WC_SZ
.th:
    cmp byte [si+WC_ST], WC_CAR ; 1. carrying theirs: home
    jne .nc
    cmp [si+WC_CARR], cl
    jne .nc
    mov bl, ch
    jmp .base
.nc:
    cmp byte [di+F_BST], WA_BENG ; (fighting, carrying nothing: on)
    je .done
    cmp byte [di+F_BST], WA_BHUNT
    je .done
    push si                     ; 2. its own flag dropped: to it
    mov si, wa_cfl
    or ch, ch
    jz .ow
    add si, WC_SZ
.ow:
    cmp byte [si+WC_ST], WC_DROP
    pop ax
    je .lies
    mov si, ax
    mov al, [si+WC_ST]          ; 3. theirs: at home, to their base; lying
    cmp al, WC_DROP             ; dropped, to it; carried (by a teammate):
    je .lies                    ; as it was
    cmp al, WC_HOME
    jne .done
    mov bl, ch
    xor bl, 1
.base:                          ; BL = a team: its base's node
    mov al, 6
    mul bl
    mov bx, ax
    add bx, [wa_gflp]
    mov al, [bx+6]
    jmp .goal
.lies:                          ; SI = a flag lying dropped: the node
    mov ax, [si+WC_X]           ; nearest it
    mov bx, [si+WC_Y]
    call wa_bcell
.goal:
    cmp al, dh
    je .done                    ; (there: wa_bnext's roam takes it on)
    mov [di+F_BGOAL], al
    mov byte [di+F_BST], WA_BFETCH
.done:
    pop si
    pop dx
    pop cx
    pop bx
.out0:
    ret

; wa_ovctf - the overlay's CTF (wa_ovbuild; ES, DI its cursor): the flag
; line at the top - each team's name, its captures and where its flag is
; (# at home, ! carried, _ dropped) - and a message under the middle
WC_LINEW    equ 18
wa_ovctf:
    cmp byte [wa_gmode], 1
    jne .out
    xor dx, dx                  ; row 0, centred
    mov bx, [wa_w]
    sub bx, WC_LINEW
    shr bx, 1
    mov si, wc_n_red
    xor cx, cx
    call .team
    add bx, 3
    mov si, wc_n_blue
    mov cl, 1
    call .team
    cmp byte [wa_cmsgt], 0      ; THE MESSAGE
    je .out
    mov si, [wa_cmsg]
    xor cx, cx
.len:
    cmp byte [si], 0
    je .ld
    inc si
    inc cx
    jmp .len
.ld:
    mov si, [wa_cmsg]
    mov bx, [wa_w]
    sub bx, cx
    shr bx, 1
    mov dx, 10
    mov ah, [wa_cmsga]
.mc:
    lodsb
    call wa_ovput
    loop .mc
.out:
    ret
.team:                          ; SI = its name, CL = the team, BX its
    push cx                     ; column (moved on past it)
    mov al, cl
    call wc_tatt
    mov ah, al                  ; AH = its colour
    pop cx
.nm:
    lodsb
    or al, al
    jz .nd
    call wa_ovput
    jmp .nm
.nd:
    inc bx                      ; (a gap: the view shows through)
    xor ch, ch
    mov si, cx
    mov al, [wa_caps+si]
    add al, '0'
    call wa_ovput
    inc bx
    xor si, si                  ; where its flag is
    or cl, cl
    jz .g
    mov si, WC_SZ
.g:
    mov al, [wa_cfl+si+WC_ST]
    push bx
    mov bx, wc_glyph
    xlatb
    pop bx
    jmp wa_ovput

wc_glyph:   db '#', '!', '_'    ; (home, carried, dropped)
wc_n_red:   db 'RED', 0
wc_n_blue:  db 'BLUE', 0
wc_m_sc:    dw wc_m_rsc, wc_m_bsc
wc_m_win:   dw wc_m_rwin, wc_m_bwin
wc_m_rsc:   db 'RED SCORES', 0
wc_m_bsc:   db 'BLUE SCORES', 0
wc_m_rwin:  db 'RED WINS', 0
wc_m_bwin:  db 'BLUE WINS', 0
wc_m_you:   db 'YOU HAVE THE FLAG', 0
wc_m_we:    db 'WE HAVE THEIR FLAG', 0
wc_m_they:  db 'THEY HAVE OUR FLAG', 0
wc_m_odown: db 'OUR FLAG IS DOWN', 0
wc_m_tdown: db 'THEIR FLAG IS DOWN', 0
wc_m_ohome: db 'OUR FLAG IS HOME', 0
wc_m_thome: db 'THEIR FLAG IS HOME', 0
