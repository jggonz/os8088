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
WC_RET      equ 900             ; a dropped flag's ticks to go home: 30 s
WC_WINT     equ 300             ; the win shown: 10 s (MODES-PLAN 2)
WC_MSGT     equ 90              ; a message shown: 3 s
WC_HOLD     equ 300             ; the hill held a point's time: 10 s
WC_NONE     equ 0               ; THE HILL (king of the hill, MODES-PLAN 4):
                                ; flag 0's record, its state the holder -
                                ; none, or 1 + a team; WC_TMR its time held
WC_DZ       equ 64              ; a flag's reach in height
WC_FBASE0   equ 24              ; a flag at rest: from 24 to 104 above its
WC_FBASE1   equ 104             ; floor; carried, over its carrier's head
WC_FCAR0    equ 104
WC_FCAR1    equ 152

%macro WC_BSS 0
WA_VAR wa_gmode,1                ; THE MATCH'S MODE: 0 free-for-all, 1 CTF,
                                 ; 2 team deathmatch, 3 king of the hill
                                 ; (MODES-PLAN 1)
WA_VAR wa_gside,1                ; ...a team mode on an arena with sides:
                                 ; its spawns on them
WA_VAR wa_ghlp,2                 ; -> the arena's hill: a count, 0 or 1,
                                 ; then x, y, z, node a, node b (arenagen)
WA_VAR wa_lkgmode,1              ; ...the setup's pick (kept between matches)
WA_VAR wa_tm,4                  ; each local index's team: 0 red, 1 blue
WA_VAR wa_gflp,2                ; -> the arena's bases: a count, 0 or 2,
                                ; then x, y, node, 0 each (arenagen)
WA_VAR wa_cfl,2*WC_SZ           ; the two flags, red's then blue's
WA_VAR wa_caps,2                ; each team's captures (team deathmatch:
                                ; its kills)
WA_VAR wa_ggoal,1               ; the score that wins, 0 none (Win at)
WA_VAR wc_wbuf,WA_NNAME+6       ; "<NAME> WINS"
WA_VAR wc_wby,1                 ; the win, as GAME says it: 0 none, else 1 +
                                ; the winner's slot (free-for-all) or team
WA_VAR wc_sgame,4               ; LINKED (MODES-PLAN 8): what the host last
                                ; sent in GAME - the scores, the win, the
                                ; hill
WA_VAR wc_grace,2               ; ticks a new arena's win waits (the old
                                ; board's rows may still come in)
WA_VAR wc_rowe,1                ; the host's: a new arena's rolls owed
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
    mov bl, [wa_arena]          ; (an arena that does not play it: free-
    xor bh, bh                  ; for-all - the setup offers no such pair)
    mov ah, [wa_amodes+bx]
    mov cl, al
    shr ah, cl
    test ah, 1
    jnz .md
    xor al, al
.md:
    mov [wa_gmode], al
    mov byte [wa_gside], 0      ; (sides: a team mode where team deathmatch
    or al, al                   ; plays - each side has its respawns)
    jz .ns
    test byte [wa_amodes+bx], 1 << 2
    jz .ns
    mov byte [wa_gside], 1
.ns:
    mov bl, al                  ; THE SCORE TO WIN: Win at's, for the mode
    xor bh, bh                  ; (wasetup.inc's tables)
    mov al, WA_NGOAL
    mul bl
    add al, [wa_sgoal+bx]
    mov bx, ax
    mov al, [wa_sgval+bx]
    mov [wa_ggoal], al
    xor ax, ax                  ; nobody's score, no win, no message (and,
    mov [wa_caps], ax           ; linked, none of it sent: the guests start
    mov [wa_cwin], ax           ; there too)
    mov [wa_cmsgt], al
    mov [wc_wby], al
    mov [wc_sgame], ax
    mov [wc_sgame+2], ax
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
    mov byte [wa_ctfcp], 0x80   ; (free-for-all: copied once, off; team
    or al, al                   ; deathmatch once, its colours)
    je .out
    call wc_side                ; EVERYONE ON THEIR SIDE (CTF-PLAN 2)
    cmp byte [wa_gmode], 3
    jne .nk
    mov byte [wa_ctfcp], 1      ; (king of the hill: every frame, its flag)
    mov si, [wa_ghlp]           ; THE HILL: its flag there, nobody's
    mov di, wa_cfl
    mov ax, [si+2]
    mov [di+WC_X], ax
    mov ax, [si+4]
    mov [di+WC_Y], ax
    mov ax, [si+6]
    mov [di+WC_Z], ax
    mov byte [di+WC_ST], WC_NONE
    mov word [di+WC_TMR], 0
    call wc_frame
    jmp .out
.nk:
    cmp byte [wa_gmode], 1
    jne .out
    mov byte [wa_ctfcp], 1      ; (CTF: every frame, the flags)
    call wc_reset
    xor ax, ax                  ; (nothing sent yet: both home, nobody's)
    mov [wc_sst], ax
    mov [wc_scar], ax
    mov [wc_scap], ax
    mov [wc_swin], al
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
    cmp byte [wa_cmsgt], 0      ; (every mode: a message's time)
    je .m
    dec byte [wa_cmsgt]
    jnz .m
    mov byte [wa_ovd], 1        ; (the message gone)
.m:
    cmp word [wc_grace], 0      ; (linked: a new arena's first seconds)
    je .gr
    dec word [wc_grace]
.gr:
    cmp byte [wa_lkon], 0       ; A GUEST (CTF-PLAN 7, MODES-PLAN 8): the
    je .rules                   ; host's rules, told by its events - here
    cmp byte [wa_lkme], 0       ; the win's time shown, and CTF's flags
    je .host                    ; carried kept over their carriers
    cmp word [wa_cwin], 0
    je .gw
    dec word [wa_cwin]
.gw:
    cmp byte [wa_gmode], 1
    jne .out
    mov si, wa_cfl
    call .follow
    mov si, wa_cfl + WC_SZ
    call .follow
    jmp wc_frame
.host:
    call wc_rolls               ; (a new arena's pickups said, in turn)
.rules:                         ; ALONE, OR THE HOST: a win's time, then
    cmp word [wa_cwin], 0       ; the next arena; else the mode's rules
    je .play
    dec word [wa_cwin]
    jnz .out
    jmp wc_next
.play:
    cmp byte [wa_gmode], 1
    je .ctf
    cmp byte [wa_gmode], 3
    jne .sc
    call wc_hill                ; (king of the hill)
    jmp wc_gsend
.sc:
    call wc_score               ; (free-for-all, team deathmatch)
    jmp wc_gsend
.ctf:
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

; wc_score - FREE-FOR-ALL and TEAM DEATHMATCH, alone, a tick: team
; deathmatch's teams' kills (each its players', a suicide a kill less as on
; the board); the first to Win at's score wins. Keeps nothing
wc_score:
    mov ah, [wa_ggoal]          ; AH = the score to win
    xor bx, bx
    cmp byte [wa_gmode], 2
    je .tdm
    or ah, ah                   ; FREE-FOR-ALL: the first with as many
    jz .out                     ; kills
    cmp word [wc_grace], 0      ; (linked, a new arena: the old board's
    jne .out                    ; rows may still come in)
.f:
    cmp [wa_kills+bx], ah
    jae wc_fwin
    inc bx
    cmp bx, WA_NFIG + 1
    jb .f
.out:
    ret
.tdm:                           ; TEAM DEATHMATCH: DL red's, DH blue's
    xor dx, dx
.t:
    mov al, [wa_kills+bx]
    cmp byte [wa_tm+bx], 0
    jne .bl
    add dl, al
    jmp short .tn
.bl:
    add dh, al
.tn:
    inc bx
    cmp bx, WA_NFIG + 1
    jb .t
    cmp dx, [wa_caps]
    je .tw
    mov [wa_caps], dx
    mov byte [wa_ovd], 1
.tw:
    or ah, ah
    jz .out
    cmp word [wc_grace], 0
    jne .out
    xor bx, bx
    cmp dl, ah
    jae wc_twin
    inc bx
    cmp dh, ah
    jb .out
    jmp wc_twin

; wc_fwin - BX = a local index: its free-for-all win shown (and, the host,
; said: wc_wby). Keeps nothing
wc_fwin:
    mov word [wa_cwin], WC_WINT
    mov al, bl
    cmp byte [wa_lkon], 0       ; (its slot, on the wire)
    je .sl
    call wa_lkcanon
.sl:
    inc ax
    mov [wc_wby], al
    call wc_wtone
    mov ax, wc_m_youw           ; (ours: green; theirs: its name, white)
    mov dl, 0x0A
    or bx, bx
    jz .say
    call wa_fname               ; (BL = its code: SI = its name)
    push ds
    pop es
    mov di, wc_wbuf
    mov cx, WA_NNAME
.nm:
    lodsb
    cmp al, ' '
    jbe .nd
    stosb
    loop .nm
.nd:
    mov si, wc_m_wins
.ws:
    lodsb
    stosb
    or al, al
    jnz .ws
    mov ax, wc_wbuf
    mov dl, 0x0F
.say:
    cmp byte [wa_mono], 0
    je .sa
    mov dl, 0x0F
.sa:
    jmp wc_say

; wc_twin - BX = a team: its win shown (and said). Keeps nothing
wc_twin:
    mov word [wa_cwin], WC_WINT
    mov [wc_wby], bl
    inc byte [wc_wby]
    call wc_wtone
    mov al, bl
    call wc_tatt
    mov dl, al
    shl bx, 1
    mov ax, [wc_m_win+bx]
    jmp wc_say
wc_wtone:                       ; the win's tone. Keeps BX
    push bx
    mov ax, 2400
    mov cx, 12
    mov dl, 0x40
    call OSAPI_SND_TONE
    pop bx
    ret

; wc_hill - KING OF THE HILL, alone, a tick (MODES-PLAN 4): the teams on the
; hill - one team alone on it, not its holder, takes it (both: it stays as
; it is); its holder scores a point each WC_HOLD ticks it holds it, and
; Win at's wins. Keeps nothing
wc_hill:
    mov si, wa_cfl
    xor cx, cx
    xor bp, bp                  ; BP = the teams on it: bit 0 red, 1 blue
.e:
    mov al, cl
    call wc_pos
    jnc .en
    call wc_near
    jnc .en
    mov bx, cx
    mov al, 1
    add al, [wa_tm+bx]
    xor ah, ah
    or bp, ax
.en:
    inc cx
    cmp cx, WA_NFIG + 1
    jb .e
    or bp, bp
    jz .hold
    cmp bp, 3
    je .hold                    ; (both: contested, as it is)
    mov ax, bp                  ; (1 red, 2 blue: its state)
    cmp al, [si+WC_ST]
    je .hold
    mov [si+WC_ST], al          ; TAKEN
    mov word [si+WC_TMR], 0
    dec ax
    mov bp, ax                  ; BP = the team
    mov ax, 1800
    mov cx, 3
    mov dl, 0x40
    call OSAPI_SND_TONE
    mov ax, wc_m_hwe
    mov dx, wc_m_hthey
    call wc_sayk
    jmp wc_frame
.hold:
    mov al, [si+WC_ST]
    or al, al
    jz .f
    inc word [si+WC_TMR]        ; HELD: a point each WC_HOLD
    cmp word [si+WC_TMR], WC_HOLD
    jb .f
    mov word [si+WC_TMR], 0
    xor bh, bh
    mov bl, al
    dec bx                      ; BX = the holders
    inc byte [wa_caps+bx]
    mov byte [wa_ovd], 1
    mov al, [wa_ggoal]
    or al, al
    jz .pt
    cmp [wa_caps+bx], al
    jae .win
.pt:
    push bx
    mov ax, 2400
    mov cx, 3
    mov dl, 0x40
    call OSAPI_SND_TONE
    pop bx
    jmp short .f
.win:
    call wc_twin
.f:
    jmp wc_frame

; wc_hgoal - DI = a bot at the end of a leg, at node BL, in king of the
; hill (wc_bgoal's): its team not the holder and it not fighting, over the
; hill - to its node a, or from a to b, the route that crosses it. Keeps
; all but AX
wc_hgoal:
    push bx
    push dx
    push si
    mov dl, bl                  ; DL = its node
    call wa_bidx
    inc ax
    mov bx, ax
    mov al, [wa_tm+bx]
    inc ax
    cmp al, [wa_cfl+WC_ST]
    je .done                    ; (its team's: it roams, and fights)
    cmp byte [di+F_BST], WA_BENG
    je .done
    cmp byte [di+F_BST], WA_BHUNT
    je .done
    mov si, [wa_ghlp]
    mov al, [si+8]
    cmp al, dl
    jne .go
    mov al, [si+9]
    cmp al, dl
    je .done                    ; (one node: it is there)
.go:
    mov [di+F_BGOAL], al
    mov byte [di+F_BST], WA_BFETCH
.done:
    pop si
    pop dx
    pop bx
    ret

; wc_next - ALONE, a win's 10 s over (MODES-PLAN 5): the match on in the
; next arena of the rotation - the next after this one, round, of the
; setup's list that plays the mode - read inside the bracket (SPEC.md
; 53.7: the file slots), the scores anew and everyone on their side; the
; bots keep their names. An arena that cannot be had ends the match (its
; toast says why, once the desktop is back)
wc_next:
    mov bl, [wa_gmode]
    xor bh, bh
    mov dh, [wa_srot+bx]        ; DH = the mode's rotation (the setup's)
    mov cl, bl
    mov dl, 1
    shl dl, cl                  ; DL = the mode's bit
    mov al, [wa_arena]          ; BL = this one's item (wa_sarmap is its
    call wa_sarmap              ; own inverse)
    mov bl, al
    mov ch, WA_NARENA           ; (round once at most: this one again)
.n:
    inc bx
    cmp bl, WA_NARENA
    jb .c
    xor bl, bl
.c:
    mov al, bl
    call wa_sarmap
    mov si, ax
    and si, 0x00FF
    test [wa_amodes+si], dl
    jz .k
    mov cl, al
    mov ah, 1
    shl ah, cl
    test dh, ah
    jnz .got                    ; (in the mode and its rotation)
.k:
    dec ch
    jnz .n
    mov al, [wa_arena]
.got:
    cmp byte [wa_lkon], 0       ; THE HOST: NEXT queued for every machine
    je wc_go                    ; first (MODES-PLAN 8) - a full queue, the
    cmp byte [wa_lkqn], WL_EVQ  ; next tick
    jb .q
    mov word [wa_cwin], 1
    ret
.q:
    push ax
    mov byte [wa_lkev], EV_NEXT
    mov [wa_lkev+1], al
    call wa_lkqueue
    pop ax
    ; (falls through)
; wc_go - AL = an arena: the match on in it (wc_next's, and a guest's on
; the host's NEXT), the scores anew, everyone on their side
wc_go:
    mov [wa_lkarena], al        ; (the setup, after: where the match was)
    cmp al, [wa_arena]
    je .same
    mov [wa_arena], al
    call wa_loadarena
    jc .fail
    call wa_geominit
.same:
%if WA_NFIG != 3
  %error "wc_next banks the bots' three names"
%endif
    mov al, [wa_bname+2]
    push ax
    push word [wa_bname]
    call wa_reset
    pop word [wa_bname]
    pop ax
    mov [wa_bname+2], al
    cmp byte [wa_lkon], 0
    jne .lk
    call wa_bpark
    jmp wa_ctfstart
.lk:
    call wa_lkround             ; (linked: the link's side of a new arena)
    jmp wa_ctfstart
.fail:
    mov byte [wa_quit], 1
    ret

; wc_gsend - THE HOST, linked, a tick (MODES-PLAN 8): GAME queued when the
; teams' scores, the win or the hill changed since it was last sent - the
; free-for-all's, team deathmatch's and king of the hill's (CTF's are its
; FLAG events)
wc_gsend:
    cmp byte [wa_lkon], 0
    je .out
    cmp byte [wa_lkme], 0
    jne .out
    mov ax, [wa_caps]
    mov dl, [wc_wby]
    xor dh, dh
    cmp byte [wa_gmode], 3
    jne .h
    mov dh, [wa_cfl+WC_ST]
.h:
    cmp ax, [wc_sgame]
    jne .send
    cmp dx, [wc_sgame+2]
    je .out
.send:
    cmp byte [wa_lkqn], WL_EVQ  ; (a full queue: the next tick)
    jae .out
    mov [wc_sgame], ax
    mov [wc_sgame+2], dx
    mov byte [wa_lkev], EV_GAME
    mov [wa_lkev+1], ax
    mov [wa_lkev+3], dx
    jmp wa_lkqueue
.out:
    ret

; wc_gin - SI = a GAME event (wa_lkevin's: its payload from SI+4), on a
; guest: the scores, the hill and the win as the host says them
wc_gin:
    mov ax, [si+4]              ; THE SCORES
    cmp ax, [wa_caps]
    je .c
    mov [wa_caps], ax
    mov byte [wa_ovd], 1
.c:
    mov al, [si+7]              ; THE HILL
    cmp byte [wa_gmode], 3
    jne .w
    cmp al, 2
    ja .w
    cmp al, [wa_cfl+WC_ST]
    je .w
    mov [wa_cfl+WC_ST], al
    push si
    or al, al
    jz .hf
    xor ah, ah
    dec ax
    mov bp, ax
    mov ax, 1800
    mov cx, 3
    mov dl, 0x40
    call OSAPI_SND_TONE
    mov ax, wc_m_hwe
    mov dx, wc_m_hthey
    call wc_sayk
.hf:
    call wc_frame
    pop si
.w:
    mov al, [si+6]              ; THE WIN, once
    or al, al
    jz .out
    cmp word [wa_cwin], 0
    jne .out
    dec ax
    cmp byte [wa_gmode], 0
    jne .team
    call wa_lklocal             ; (its slot: who, here)
    cmp al, WA_NFIG
    ja .out
    xor ah, ah
    mov bx, ax
    jmp wc_fwin
.team:
    cmp al, 1
    ja .out
    xor ah, ah
    mov bx, ax
    jmp wc_twin
.out:
    ret

; wc_nin - SI = a NEXT event, on a guest: the host's next arena, now (the
; events after it are the new arena's: its pickups' rolls)
wc_nin:
    mov al, [si+4]
    cmp al, WA_NARENA
    jae .out
    jmp wc_go
.out:
    ret

; wc_rolls - THE HOST, a tick: a new arena's pickups said (ROLL), one a tick
; while the queue has room (wa_lkround owes them)
wc_rolls:
    mov al, [wc_rowe]
    or al, al
    jz .out
    cmp byte [wa_lkqn], WL_EVQ
    jae .out
    dec ax
    mov [wc_rowe], al
    xor ah, ah
    mov bx, ax
    mov cl, WA_PKSZ
    mul cl
    add ax, wa_pks
    mov si, ax
    mov ah, [si+6]
    mov al, bl
    jmp wa_lkqroll
.out:
    ret

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
    mov al, [wa_ggoal]          ; ...and Win at's: the win
    or al, al
    jz .sc
    cmp [wa_caps+bx], al
    jb .sc
    mov word [wa_cwin], WC_WINT
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
    cmp byte [wa_gmode], 3      ; KING OF THE HILL: its flag, white while
    jne .ctf                    ; nobody's, else its holders' colour
    mov si, wa_cfl
    mov di, wa_flags
    mov ax, [si+WC_X]
    mov [di+FL_X], ax
    mov ax, [si+WC_Y]
    mov [di+FL_Y], ax
    mov ax, [si+WC_Z]
    mov [di+FL_Z], ax
    mov word [di+FL_FROM], WC_FBASE0 | (WC_FBASE1 << 8)
    mov byte [di+FL_ON], 1
    mov byte [di+WA_FLSZ+FL_ON], 0
    mov al, 0x0F
    mov ah, [si+WC_ST]
    or ah, ah
    jz .hc
    mov al, ah
    dec ax
    call wc_tatt
.hc:
    mov [di+FL_ATT], al
    ret
.ctf:
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
    cmp byte [wa_gmode], 3
    je wc_hgoal
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
    cmp byte [wa_gmode], 0      ; (free-for-all: the message alone)
    je .msg
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
.msg:
    mov al, [wa_cmsgt]          ; THE MESSAGE (a win's, its 10 s)
    or al, [wa_cwin]
    or al, [wa_cwin+1]
    jz .out
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
    mov ch, ah                  ; (CH = the colour)
    aam                         ; (AH tens, AL units: 99 at most)
    push ax
    mov al, ah
    mov ah, ch
    or al, al
    jz .u                       ; (no tens: one digit)
    add al, '0'
    call wa_ovput
.u:
    pop ax
    add al, '0'
    mov ah, ch
    call wa_ovput
    inc bx
    cmp byte [wa_gmode], 3      ; (king of the hill: # by its holders)
    jne .nkh
    mov al, cl
    inc ax
    cmp al, [wa_cfl+WC_ST]
    jne .out
    mov al, '#'
    jmp wa_ovput
.nkh:
    cmp byte [wa_gmode], 1      ; (CTF: where its flag is)
    jne .out
    xor si, si
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
wc_m_youw:  db 'YOU WIN', 0
wc_m_hwe:   db 'WE HAVE THE HILL', 0
wc_m_hthey: db 'THEY HAVE THE HILL', 0
wc_m_wins:  db ' WINS', 0
wc_m_rwin:  db 'RED WINS', 0
wc_m_bwin:  db 'BLUE WINS', 0
wc_m_you:   db 'YOU HAVE THE FLAG', 0
wc_m_we:    db 'WE HAVE THEIR FLAG', 0
wc_m_they:  db 'THEY HAVE OUR FLAG', 0
wc_m_odown: db 'OUR FLAG IS DOWN', 0
wc_m_tdown: db 'THEIR FLAG IS DOWN', 0
wc_m_ohome: db 'OUR FLAG IS HOME', 0
wc_m_thome: db 'THEIR FLAG IS HOME', 0
