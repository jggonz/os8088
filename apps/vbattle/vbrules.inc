; =============================================================================
; os8088 - apps/vbattle/vbrules.inc - THE BATTLE'S RULES (SPEC.md 126.6)
;
; The engine's half of tools/vbtrules.py, which is the REFERENCE: every
; routine here says which of its functions it is, and must draw the
; generator in the same order and do the same integer arithmetic with the
; same caps, so that a battle played here and in vbtrules.Sim agrees to the
; hit point (tests/vbtrules.py and the soak row that compares them).
; The rules are the bundle's RULES block, in our bss at vb_meta+VB_RULES.
; Everything here runs in the UI task (the foreground), never in the hook.
; =============================================================================

VB_RULES    equ 4096
RL          equ vb_meta+VB_RULES
; the rules header
RH_NCH      equ 1
RH_NACT     equ 2
RH_NMENU    equ 3
RH_NST      equ 4
RH_NITEM    equ 6
RH_OCH      equ 8
RH_OACT     equ 10
RH_OMENU    equ 12
RH_OST      equ 14
RH_OPOOL    equ 16
RH_OITEM    equ 18
RH_OIMG     equ 20
RH_BANDIMG  equ 22
RH_START    equ 23
RH_ENAME    equ 24
RH_EHP      equ 34
RH_EMP      equ 36
RH_EATK     equ 38              ; ATK MAG DEF MDEF, a word each
RH_EEVADE   equ 46
RH_VICT     equ 47
RH_DEFT     equ 48
RH_FLAGS    equ 49
RH_SEED     equ 50
RH_NFOL     equ 52              ; the enemy's FOLLOW-UPS (126.6.7): how many,
RH_OFOL     equ 54              ; and their table - after, then, rule, arg
FOL_SAME    equ 0xFF            ; ...a follow-up's rule: the same target
RH_OLAY     equ 56              ; THE BAND'S LAYOUT (126.5.1): its offset, 0
RH_NPLACE   equ 58              ; none; menu items placed of their own: how
RH_OPLACE   equ 60              ; many, and where (menu, item, col, row)
; a character
CH_NAME     equ 0
CH_HP       equ 8
CH_MP       equ 10
CH_ATK      equ 12              ; ATK MAG DEF MDEF, a word each
CH_EVADE    equ 20
CH_TOP      equ 21
CH_SIDE     equ 22
CH_SZ       equ 32
; an action
AC_LABEL    equ 0
AC_TGT      equ 8
AC_COST     equ 9
AC_COSTN    equ 10
AC_ITEM     equ 12
AC_ACC      equ 13
AC_FLAGS    equ 14
AC_EFF      equ 16              ; 4 x 6: verb, a, power (word), b, c
AC_POOLS    equ 40              ; hit, miss, defended, critical, finishing
AC_BY       equ 48              ; the same five, per character
AC_SZ       equ 72
EF_V        equ 0
EF_A        equ 1
EF_P        equ 2
EF_B        equ 4
EF_C        equ 5
; a menu
MN_LABEL    equ 0
MN_N        equ 8
MN_IMG      equ 9
MN_COL      equ 10
MN_ROW      equ 11
MN_PITCH    equ 12
MN_INK      equ 13
MN_FLAGS    equ 14
MN_ITEMS    equ 16              ; 8 x (index, kind: 0 action, 1 sub-menu)
MN_SZ       equ 32
MF_ARROW    equ 1
; a stance
ST_NAME     equ 0
ST_MODS     equ 8               ; ATK MAG DEF MDEF, signed %
ST_NMOV     equ 12
ST_NRESP    equ 13
ST_RET      equ 14
ST_MOVES    equ 16              ; 6 x (action, weight, rule, arg)
ST_RESP     equ 40              ; 2 x (trigger, arg, chance, action)
ST_AUTO     equ 48              ; kind, arg, to, via
ST_SZ       equ 56
; verbs, targets, rules
V_DAMAGE    equ 1
V_HEAL      equ 2
V_RESTORE   equ 3
V_REVIVE    equ 4
V_GUARD     equ 5
V_CHARGE    equ 6
V_BOOST     equ 7
V_LOWER     equ 8
V_PASS      equ 9
V_ESCAPE    equ 10
V_STANCE    equ 11
V_LIMIT     equ 12              ; a limit gauge filled, POWER points: A 0 the
                                ; targets' (characters hit), 1 the doer's
T_ENEMY     equ 0
T_SELF      equ 1
T_ALLY      equ 2
T_ALLIES    equ 3
T_NONE      equ 4
T_CHAR      equ 5
T_CHARS     equ 6
R_RANDOM    equ 1
R_LOWEST    equ 2
R_LAST      equ 3
R_NAMED     equ 4
K_PHYS      equ 0
K_FIXED     equ 2
H_PERCENT   equ 1
H_FIXED     equ 2
C_MP        equ 1
C_ITEM      equ 2
C_LIMIT     equ 3
AF_PHYS     equ 1
AF_MAG      equ 2
CAP         equ 9999
ENEMY       equ 0x80
NONE        equ 0xFF
OVER_WIN    equ 1
OVER_LOSE   equ 2
OVER_ESC    equ 3

; --- the records ----------------------------------------------------------
; vb_char - SI = character BL's record. AX spent
vb_char:
    mov al, bl
    and ax, 3
    mov si, ax
    mov ax, CH_SZ
    mul si
    add ax, [RL+RH_OCH]
    add ax, RL
    mov si, ax
    ret

; vb_act - SI = action AL's record. AX, DX spent
vb_act:
    xor ah, ah
    mov dx, AC_SZ
    mul dx
    add ax, [RL+RH_OACT]
    add ax, RL
    mov si, ax
    ret

; vb_menurec - SI = menu AL's record. AX spent
vb_menurec:
    xor ah, ah
    mov cl, 5
    shl ax, cl
    add ax, [RL+RH_OMENU]
    add ax, RL
    mov si, ax
    ret

; vb_strec - SI = stance AL's record. AX, DX spent
vb_strec:
    xor ah, ah
    mov dx, ST_SZ
    mul dx
    add ax, [RL+RH_OST]
    add ax, RL
    mov si, ax
    ret

; vb_poolrec - SI = pool AL's record (a count, then the clips). AX spent
vb_poolrec:
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, [RL+RH_OPOOL]
    add ax, RL
    mov si, ax
    ret

; --- the generator: vbtrules.Rng ---------------------------------------------
; vb_rstep - AX = the next of the 16-bit xorshift (7, 9, 8). CX spent
vb_rstep:
    mov ax, [vb_rng]
    mov cx, ax
    push cx
    mov cl, 7
    shl ax, cl
    pop cx
    xor ax, cx
    mov cx, ax
    push cx
    mov cl, 9
    shr ax, cl
    pop cx
    xor ax, cx
    mov cx, ax
    push cx
    mov cl, 8
    shl ax, cl
    pop cx
    xor ax, cx
    mov [vb_rng], ax
    ret

; vb_rnd - AX = rnd(CX): (the next * CX) >> 16. CX, DX spent
vb_rnd:
    push cx
    call vb_rstep
    pop cx
    mul cx
    mov ax, dx
    ret

; vb_rpeek - AX = the next draw, not taken. CX spent
vb_rpeek:
    push word [vb_rng]
    call vb_rstep
    pop word [vb_rng]
    ret

; --- the stats: vbtrules.Sim.stat ----------------------------------------------
; vb_stat - AX = who BL's stat CL (0 ATK, 1 MAG, 2 DEF, 3 MDEF): the base
; times (8 + its steps) / 8, and the enemy's then times (100 + its stance's
; modifier) / 100. BX, CX kept; DX, SI, DI spent
vb_stat:
    push bx
    push cx
    xor ch, ch
    cmp bl, ENEMY
    je .e
    push cx
    call vb_char
    pop cx
    mov di, cx
    shl di, 1
    add si, di                      ; ([si+di] is not an 8086 address)
    mov ax, [si+CH_ATK]
    xor bh, bh
    shl bx, 1
    shl bx, 1
    add bx, cx
    mov dl, [vb_mods+bx]
    jmp short .steps
.e:
    mov di, cx
    shl di, 1
    mov ax, [RL+RH_EATK+di]
    mov bx, cx
    mov dl, [vb_emods+bx]
.steps:
    add dl, 8                       ; 8 + steps (signed, -4..4)
    xor dh, dh
    mul dx
    mov cx, 3
.s:
    shr dx, 1
    rcr ax, 1
    loop .s
    pop cx
    pop bx
    cmp bl, ENEMY
    jne .out
    push bx
    push cx
    mov bl, cl
    xor bh, bh
    push ax
    mov al, [vb_stance]
    call vb_strec
    pop ax
    mov dl, [si+ST_MODS+bx]
    mov dh, 0
    test dl, 0x80
    jz .pos
    mov dh, 0xFF
.pos:
    add dx, 100                     ; 100 + the modifier (0..227)
    mul dx
    mov cx, 100
    div cx
    pop cx
    pop bx
.out:
    ret

; vb_evade - AL = who BL's evade
vb_evade:
    cmp bl, ENEMY
    jne .c
    mov al, [RL+RH_EEVADE]
    ret
.c:
    push si
    call vb_char
    mov al, [si+CH_EVADE]
    pop si
    ret

; vb_guarding - ZF=0 when who BL is guarding
vb_guarding:
    cmp bl, ENEMY
    jne .c
    cmp byte [vb_eguard], 0
    ret
.c:
    push bx
    xor bh, bh
    cmp byte [vb_guard+bx], 0
    pop bx
    ret

; --- the arithmetic: vbtrules.Sim.damage and heal_amount ----------------------
; vb_damage - AX = the damage of kind AL (0 physical, 1 magical, 2 fixed),
; power CX, from who BL onto who BH, at variance DX (90..110)
vb_damage:
    cmp al, K_FIXED
    jne .f
    mov ax, cx
    jmp vb_cap
.f:
    push bx
    push si
    push di
    mov [vb_dvar], dx
    mov [vb_dpow], cx
    mov cl, al                      ; the attack: ATK or MAG
    push ax
    call vb_stat
    mov [vb_datk], ax
    pop ax
    mov cl, al
    add cl, 2                       ; the defence: DEF or MDEF
    mov bl, bh
    call vb_stat
    cmp ax, 255
    jbe .d
    mov ax, 255
.d:
    mov cx, 256
    sub cx, ax
    mov [vb_ddef], cx
    mov ax, [vb_dpow]
    mul word [vb_datk]
    mov cx, 4
.sh:
    shr dx, 1
    rcr ax, 1
    loop .sh
    or dx, dx                       ; min(0xFFFF, ...)
    jz .a
    mov ax, 0xFFFF
.a:
    mul word [vb_ddef]              ; >> 8: dh is 0 (< 2^24)
    mov al, ah
    mov ah, dl
    call vb_cap
    mul word [vb_dvar]
    mov cx, 100
    div cx
    pop di
    pop si
    pop bx
    jmp vb_cap

; vb_cap - AX = min(AX, 9999)
vb_cap:
    cmp ax, CAP
    jbe .o
    mov ax, CAP
.o:
    ret

; vb_heal - AX = the heal of kind AL (0 by power, 1 percent, 2 fixed),
; power CX, by who BL onto who BH, at variance DX
vb_heal:
    cmp al, H_FIXED
    jne .p
    mov ax, cx
    jmp vb_cap
.p:
    cmp al, H_PERCENT
    jne .w
    push si
    mov ax, [RL+RH_EHP]             ; the most HP: the enemy's or the char's
    cmp bh, ENEMY
    je .m
    push bx
    mov bl, bh
    call vb_char
    pop bx
    mov ax, [si+CH_HP]
.m:
    pop si
    mul cx                          ; power is at most 100 (vbtrules)
    mov cx, 100
    div cx
    ret
.w:
    push si
    push di
    mov [vb_dvar], dx
    mov [vb_dpow], cx
    mov cl, 1                       ; MAG
    call vb_stat
    mul word [vb_dpow]
    mov cx, 4
.sh:
    shr dx, 1
    rcr ax, 1
    loop .sh
    or dx, dx
    jz .c
    mov ax, CAP
.c:
    call vb_cap
    mul word [vb_dvar]
    mov cx, 100
    div cx
    pop di
    pop si
    jmp vb_cap

; --- the targets: vbtrules.Sim.targets ---------------------------------------
; vb_targets - [vb_qn], [vb_qt] for action SI by who [vb_qw], CHOSEN AH
vb_targets:
    mov byte [vb_qn], 0
    mov al, [si+AC_TGT]
    cmp byte [vb_qw], ENEMY
    jne .party
    cmp al, T_SELF
    je .me
    cmp al, T_ALLY
    je .me
    cmp al, T_ALLIES
    je .me
    cmp al, T_NONE
    je .out
    cmp al, T_CHARS
    je .stand
    mov al, ah                      ; the one its rule picked
    jmp short .one
.party:
    cmp al, T_SELF
    jne .p1
    mov al, [vb_qw]
    jmp short .one
.p1:
    cmp al, T_ALLY
    jne .p2
    mov al, ah
    jmp short .one
.p2:
    cmp al, T_ALLIES
    jne .p3
    call vb_hasrevive
    jnz .all
    jmp short .stand
.p3:
    cmp al, T_NONE
    je .out
.me:
    mov al, ENEMY
    cmp byte [vb_qw], ENEMY
    je .one
    mov al, ENEMY                   ; (the party's: at the enemy)
.one:
    cmp al, NONE
    je .out
    mov [vb_qt], al
    mov byte [vb_qn], 1
.out:
    ret
.all:
    xor bx, bx
.al:
    mov [vb_qt+bx], bl
    inc bx
    cmp bl, [RL+RH_NCH]
    jb .al
    mov [vb_qn], bl
    ret
.stand:
    xor bx, bx
    xor di, di
.st:
    mov ax, bx
    shl ax, 1
    push bx
    mov bx, ax
    cmp word [vb_hp+bx], 0
    pop bx
    je .sn
    mov [vb_qt+di], bl
    inc di
.sn:
    inc bx
    cmp bl, [RL+RH_NCH]
    jb .st
    mov ax, di
    mov [vb_qn], al
    ret

; vb_hasrevive - ZF=0 when action SI has a REVIVE effect. AX, CX spent
vb_hasrevive:
    push si
    mov cx, 4
.e:
    cmp byte [si+AC_EFF+EF_V], V_REVIVE
    je .y
    add si, 6
    loop .e
    pop si
    xor ax, ax                      ; ZF=1
    ret
.y:
    pop si
    or si, si                       ; ZF=0 (SI is never 0 here)
    ret

; --- the plan: vbtrules.Sim.resolve -----------------------------------------------
; vb_resolve - action AL by who BL (a character, or ENEMY) at CHOSEN BH (the
; ally picked, or the enemy's rule's pick): every roll in the reference's
; order, the outcome, the pool and the clip, into the plan [vb_q*]
vb_resolve:
    mov [vb_qa], al
    mov [vb_qw], bl
    mov [vb_qchosen], bh
    mov byte [vb_qresp], 0
    call vb_rstep                   ; 1. the pool's pick
    mov [vb_qpick], ax
    mov al, [vb_qa]
    call vb_act
    mov [vb_qact], si
    mov ah, [vb_qchosen]
    call vb_targets
    mov bl, [vb_qw]                 ; the actor charged?
    mov al, [vb_echarge]
    cmp bl, ENEMY
    je .ch
    xor bh, bh
    mov al, [vb_charge+bx]
.ch:
    mov [vb_qchg], al
    xor di, di                      ; 2. each target in turn
.t:
    mov ax, di
    cmp al, [vb_qn]
    jb .t1
    jmp .outcome
.t1:
    mov byte [vb_qhit+di], 1
    mov byte [vb_qcrit+di], 0
    mov bx, di
    shl bx, 1
    mov word [vb_qdmg+bx], 0
    mov word [vb_qheal+bx], 0
    mov byte [vb_qfirst], 1
    mov si, [vb_qact]
    mov bl, [vb_qt+di]              ; an opponent?
    mov al, [vb_qw]
    xor al, bl
    test al, ENEMY
    jz .effects
    call vb_evade                   ; its hit roll, when it can miss
    mov ah, al
    or ah, ah
    jnz .roll
    cmp byte [si+AC_ACC], 100
    jae .effects
.roll:
    push ax
    mov cx, 100
    call vb_rnd
    pop cx                          ; CH = the evade
    mov dl, [si+AC_ACC]
    xor dh, dh
    mov cl, ch
    xor ch, ch
    sub dx, cx                      ; accuracy - evade (signed)
    cmp ax, dx
    jl .hits
    mov byte [vb_qhit+di], 0
    jmp short .effects
.hits:
.effects:
    mov cx, 4
    mov [vb_qeff], si
.e:
    push cx
    mov si, [vb_qeff]
    mov al, [si+AC_EFF+EF_V]
    cmp al, V_DAMAGE
    jne .e2
    cmp byte [vb_qhit+di], 0
    je .enext
    call vb_rdamage
    jmp short .enext
.e2:
    cmp al, V_HEAL
    jne .enext
    call vb_rheal
.enext:
    add word [vb_qeff], 6
    pop cx
    loop .e
    inc di
    jmp .t
.outcome:
    call vb_outcome
    ret

; vb_rdamage - effect [vb_qeff] on target DI: its hits, each its critical
; roll (physical) and its variance (not fixed); the charge on the first
; damage; halved on a guard
vb_rdamage:
    mov word [vb_qtot], 0
    mov si, [vb_qeff]
    mov cl, [si+AC_EFF+EF_B]
    xor ch, ch
.h:
    push cx
    mov si, [vb_qeff]
    mov byte [vb_qc1], 0
    cmp byte [si+AC_EFF+EF_A], K_PHYS
    jne .var
    mov cx, 16
    call vb_rnd
    or ax, ax
    jnz .var
    mov byte [vb_qc1], 1
.var:
    mov dx, 100
    mov si, [vb_qeff]
    cmp byte [si+AC_EFF+EF_A], K_FIXED
    je .calc
    mov cx, 21
    call vb_rnd
    add ax, 90
    mov dx, ax
.calc:
    mov si, [vb_qeff]
    mov al, [si+AC_EFF+EF_A]
    mov cx, [si+AC_EFF+EF_P]
    mov bl, [vb_qw]
    mov bh, [vb_qt+di]
    call vb_damage
    cmp byte [vb_qc1], 0
    je .add
    shl ax, 1
    call vb_cap
    mov byte [vb_qcrit+di], 1
.add:
    add ax, [vb_qtot]
    call vb_cap
    mov [vb_qtot], ax
    pop cx
    loop .h
    mov ax, [vb_qtot]
    cmp byte [vb_qchg], 0
    je .nc
    cmp byte [vb_qfirst], 0
    je .nc
    shl ax, 1
    call vb_cap
.nc:
    mov byte [vb_qfirst], 0
    mov bl, [vb_qt+di]
    call vb_guarding
    jz .ng
    shr ax, 1
.ng:
    mov bx, di
    shl bx, 1
    add ax, [vb_qdmg+bx]
    call vb_cap
    mov [vb_qdmg+bx], ax
    ret

; vb_rheal - effect [vb_qeff] (a HEAL) on target DI: its variance when by
; power, its amount added
vb_rheal:
    mov dx, 100
    mov si, [vb_qeff]
    cmp byte [si+AC_EFF+EF_A], 0
    jne .c
    mov cx, 21
    call vb_rnd
    add ax, 90
    mov dx, ax
.c:
    mov si, [vb_qeff]
    mov al, [si+AC_EFF+EF_A]
    mov cx, [si+AC_EFF+EF_P]
    mov bl, [vb_qw]
    mov bh, [vb_qt+di]
    call vb_heal
    mov bx, di
    shl bx, 1
    add [vb_qheal+bx], ax
    ret

; vb_outcome - vbtrules.Sim.resolve's outcome, over the OPPONENTS' results:
; finishing (a party action that leaves the enemy at 0), critical,
; defended (every opponent hit was guarding), missed (none hit), hit. Then
; the clip: the outcome's pool, else HIT's; the target's own before the
; general one; picked by [vb_qpick]
vb_outcome:
    xor di, di
    mov word [vb_qsum], 0
    mov byte [vb_qnopp], 0
    mov byte [vb_qnhit], 0
    mov byte [vb_qanycrit], 0
    mov byte [vb_qallg], 1
.r:
    mov ax, di
    cmp al, [vb_qn]
    jae .judge
    mov bl, [vb_qt+di]
    mov al, [vb_qw]
    xor al, bl
    test al, ENEMY
    jz .rn
    inc byte [vb_qnopp]
    mov bx, di
    shl bx, 1
    mov ax, [vb_qdmg+bx]
    add [vb_qsum], ax
    cmp byte [vb_qcrit+di], 0
    je .r1
    mov byte [vb_qanycrit], 1
.r1:
    cmp byte [vb_qhit+di], 0
    je .rn
    inc byte [vb_qnhit]
    mov bl, [vb_qt+di]
    call vb_guarding
    jnz .rn
    mov byte [vb_qallg], 0
.rn:
    inc di
    jmp .r
.judge:
    mov al, 0
    cmp byte [vb_qw], ENEMY         ; finishing
    je .j1
    cmp word [vb_qsum], 0
    je .j1
    mov ax, [vb_ehp]
    cmp ax, [vb_qsum]
    mov al, 4
    jbe .set
.j1:
    mov al, 3
    cmp byte [vb_qanycrit], 0
    jne .set
    mov al, 2
    cmp byte [vb_qnhit], 0
    je .j2
    cmp byte [vb_qallg], 0
    jne .set
.j2:
    mov al, 1
    cmp byte [vb_qnopp], 0
    je .j3
    cmp byte [vb_qnhit], 0
    je .set
.j3:
    xor al, al
.set:
    mov [vb_qout], al
    ; THE CHARACTER whose own pools are looked in first: the single
    ; character it is aimed at - or, aimed at the ENEMY, the character
    ; doing it (each character's own clip of a shared Attack; the owner,
    ; 2026-11-05: vbtrules.Sim.clip_char)
    mov byte [vb_qsingle], NONE
    cmp byte [vb_qn], 1
    jne .pool
    mov al, [vb_qt]
    cmp al, ENEMY
    jne .one
    mov al, [vb_qw]                 ; aimed at the enemy: its doer, when a
    cmp al, ENEMY                   ; character
    je .pool
.one:
    mov [vb_qsingle], al
.pool:
    mov al, [vb_qout]
    mov dx, [vb_qpick]
    call vb_clipof
    mov [vb_qclip], al
    ret

; vb_clipof - AL = the clip of action [vb_qact]'s outcome AL for single
; target [vb_qsingle], picked by DX: vbtrules.Sim.clip_of. NONE: no clip
vb_clipof:
    mov [vb_qpk], dx
    or al, al
    jz .hit
    call vb_poolfor
    jnz .got
.hit:
    xor al, al
    call vb_poolfor
    jnz .got
    mov al, NONE
    ret
.got:
    mov cl, [si]                    ; the count, then (pick * count) >> 16
    xor ch, ch
    mov ax, [vb_qpk]
    mul cx
    mov bx, dx
    mov al, [si+1+bx]
    ret

; vb_poolfor - SI = pool AL of action [vb_qact] for [vb_qsingle] (its own,
; else the general one); ZF=1 none, or an empty one
vb_poolfor:
    xor ah, ah
    mov bx, ax
    mov si, [vb_qact]
    mov dl, NONE
    cmp byte [vb_qsingle], NONE
    je .gen
    push bx
    mov al, [vb_qsingle]
    mov ah, 5
    mul ah
    pop bx
    push bx
    add bx, ax
    mov dl, [si+AC_BY+bx]
    pop bx
.gen:
    cmp dl, NONE
    jne .p
    mov dl, [si+AC_POOLS+bx]
    cmp dl, NONE
    je .none
.p:
    mov al, dl
    call vb_poolrec
    cmp byte [si], 0
    ret
.none:
    cmp dl, dl                      ; ZF=1
    ret

; vb_peekclip - AL = the clip the read-ahead fetches for action AL by
; character BL at single target BH (NONE: none yet): its HIT pool's pick by
; the next draw, not taken (vbtrules.Sim.peek_clip)
vb_peekclip:
    push si
    push word [vb_qact]
    push word [vb_qsingle]
    push bx
    call vb_act
    mov [vb_qact], si
    pop bx
    mov byte [vb_qsingle], NONE
    mov al, [si+AC_TGT]
    cmp al, T_SELF
    jne .a
    mov [vb_qsingle], bl
    jmp short .p
.a:
    cmp al, T_ALLY
    jne .e
    mov [vb_qsingle], bh
    jmp short .p
.e:
    cmp al, T_ENEMY                 ; aimed at the enemy: the doer's own
    jne .p                          ; clip (vbtrules.clip_char)
    cmp bl, ENEMY
    je .p
    mov [vb_qsingle], bl
.p:
    call vb_rpeek
    mov dx, ax
    xor al, al
    call vb_clipof
    pop word [vb_qsingle]
    pop word [vb_qact]
    pop si
    ret

; --- costs: vbtrules.Sim.can_pay and pay ----------------------------------------
; vb_canpay - CF=1 character BL cannot pay action SI's cost (AL: 1 MP, 2 an
; item, 3 the limit)
vb_canpay:
    mov al, [si+AC_COST]
    xor bh, bh
    cmp al, C_MP
    jne .i
    push bx
    shl bx, 1
    mov dx, [vb_mp+bx]
    pop bx
    cmp dx, [si+AC_COSTN]
    jb .no
    jmp short .yes
.i:
    cmp al, C_ITEM
    jne .l
    push bx
    mov bl, [si+AC_ITEM]
    shl bx, 1
    mov dx, [vb_items+bx]
    pop bx
    mov cx, [si+AC_COSTN]
    or cx, cx
    jnz .i1
    inc cx
.i1:
    cmp dx, cx
    jb .no
    jmp short .yes
.l:
    cmp al, C_LIMIT
    jne .yes
    cmp byte [vb_lim+bx], 100
    jb .no
.yes:
    clc
    ret
.no:
    stc
    ret

; vb_pay - character BL pays action SI's cost
vb_pay:
    mov al, [si+AC_COST]
    xor bh, bh
    cmp al, C_MP
    jne .i
    mov ax, [si+AC_COSTN]
    shl bx, 1
    sub [vb_mp+bx], ax
    ret
.i:
    cmp al, C_ITEM
    jne .l
    mov cx, [si+AC_COSTN]
    or cx, cx
    jnz .i1
    inc cx
.i1:
    mov bl, [si+AC_ITEM]
    shl bx, 1
    sub [vb_items+bx], cx
    ret
.l:
    cmp al, C_LIMIT
    jne .o
    mov byte [vb_lim+bx], 0
.o:
    ret

; --- the effects: vbtrules.Sim.apply --------------------------------------------------
vb_apply:
    mov si, [vb_qact]
    call vb_hasdamage               ; the actor's charge is spent
    jz .t0
    mov bl, [vb_qw]
    cmp bl, ENEMY
    jne .cc
    mov byte [vb_echarge], 0
    jmp short .t0
.cc:
    xor bh, bh
    mov byte [vb_charge+bx], 0
.t0:
    xor di, di
.t:
    mov ax, di
    cmp al, [vb_qn]
    jb .t1
    jmp .global
.t1:
    mov bl, [vb_qt+di]
    mov [vb_qcur], bl
    ; its damage, once (the total of every DAMAGE effect)
    mov si, [vb_qact]
    call vb_hasdamage
    jz .heal
    push di
    shl di, 1
    mov ax, [vb_qdmg+di]
    pop di
    or ax, ax
    jz .heal
    call vb_hurt
.heal:
    mov byte [vb_qhealed], 0
    mov si, [vb_qact]
    mov cx, 4
.e:
    push cx
    push si
    push di
    mov al, [si+AC_EFF+EF_V]
    mov bl, [vb_qcur]
    call vb_effect
    pop di
    pop si
    add si, 6
    pop cx
    loop .e
    inc di
    jmp .t
.global:
    mov si, [vb_qact]               ; what is not per target
    mov cx, 4
.g:
    mov al, [si+AC_EFF+EF_V]
    cmp al, V_STANCE
    jne .g1
    cmp byte [vb_qw], ENEMY
    jne .gn
    push cx
    push si
    mov al, [si+AC_EFF+EF_A]
    mov ah, [si+AC_EFF+EF_C]
    call vb_enter_stance
    pop si
    pop cx
    jmp short .gn
.g1:
    cmp al, V_ESCAPE
    jne .g2
    mov byte [vb_over], OVER_ESC
    jmp short .gn
.g2:
    cmp al, V_LIMIT                 ; THE DOER'S OWN LIMIT GAUGE
    jne .gn
    cmp byte [si+AC_EFF+EF_A], 1
    jne .gn
    mov bl, [vb_qw]
    cmp bl, ENEMY
    je .gn
    push cx
    push si
    mov al, [si+AC_EFF+EF_P]
    call vb_limadd
    pop si
    pop cx
.gn:
    add si, 6
    loop .g
    cmp byte [vb_over], 0
    jne .out
    cmp word [vb_ehp], 0
    jne .lose
    mov byte [vb_over], OVER_WIN
    ret
.lose:
    call vb_anystanding
    jnz .out
    mov byte [vb_over], OVER_LOSE
.out:
    ret

; vb_hasdamage - ZF=0 when action SI has a DAMAGE effect. CX spent
vb_hasdamage:
    push si
    mov cx, 4
.e:
    cmp byte [si+AC_EFF+EF_V], V_DAMAGE
    je .y
    add si, 6
    loop .e
    pop si
    cmp cx, cx
    ret
.y:
    pop si
    or si, si
    ret

; vb_anystanding - ZF=0 when a character stands. AX, BX spent
vb_anystanding:
    xor bx, bx
    xor ax, ax
.c:
    cmp bl, [RL+RH_NCH]
    jae .o
    push bx
    shl bx, 1
    or ax, [vb_hp+bx]
    pop bx
    inc bx
    jmp .c
.o:
    or ax, ax
    ret

; vb_hurt - AX damage onto [vb_qcur]: its HP down to 0 at most; the enemy
; remembers who hit it last; a character's limit fills by the share of its
; most HP it lost
vb_hurt:
    mov bl, [vb_qcur]
    cmp bl, ENEMY
    jne .c
    sub [vb_ehp], ax
    jae .e
    mov word [vb_ehp], 0
.e:
    mov al, [vb_qw]
    mov [vb_last], al
    ret
.c:
    xor bh, bh
    push bx
    shl bx, 1
    sub [vb_hp+bx], ax
    jae .c1
    mov word [vb_hp+bx], 0
.c1:
    pop bx
    push ax
    call vb_char
    pop ax
    mov cx, 100
    mul cx
    mov cx, [si+CH_HP]
    cmp dx, cx                      ; (a quotient past 16 bits: 100 anyway)
    jae .full
    div cx
    cmp ax, 100
    jb .add
.full:
    mov ax, 100
.add:
    mov bl, [vb_qcur]
    xor bh, bh
    add al, [vb_lim+bx]
    cmp al, 100
    jbe .st
    mov al, 100
.st:
    mov [vb_lim+bx], al
    ret

; vb_limadd - character BL's limit gauge AL points fuller, to 100 at most.
; AX, BX spent
vb_limadd:
    xor bh, bh
    add al, [vb_lim+bx]
    jc .full
    cmp al, 100
    jbe .st
.full:
    mov al, 100
.st:
    mov [vb_lim+bx], al
    ret

; vb_effect - effect AL (record SI) on target BL, result DI of the plan
vb_effect:
    cmp al, V_HEAL
    jne .rs
    cmp byte [vb_qhealed], 0
    jne .o
    mov byte [vb_qhealed], 1
    push di
    shl di, 1
    mov ax, [vb_qheal+di]
    pop di
    cmp bl, ENEMY
    jne .hc
    add ax, [vb_ehp]
    jc .hcap
    cmp ax, [RL+RH_EHP]
    jbe .he
.hcap:
    mov ax, [RL+RH_EHP]
.he:
    mov [vb_ehp], ax
    ret
.hc:
    xor bh, bh
    push bx
    shl bx, 1
    cmp word [vb_hp+bx], 0
    pop bx
    je .o
    push ax
    call vb_char
    pop ax
    shl bx, 1
    add ax, [vb_hp+bx]
    jc .hcc
    cmp ax, [si+CH_HP]
    jbe .hcs
.hcc:
    mov ax, [si+CH_HP]
.hcs:
    mov [vb_hp+bx], ax
.o:
    ret
.rs:
    cmp al, V_RESTORE
    jne .rv
    mov ax, [si+AC_EFF+EF_P]
    cmp bl, ENEMY
    jne .rc
    add ax, [vb_emp]
    jc .rec
    cmp ax, [RL+RH_EMP]
    jbe .res
.rec:
    mov ax, [RL+RH_EMP]
.res:
    mov [vb_emp], ax
    ret
.rc:
    xor bh, bh
    push ax
    push si
    call vb_char
    mov cx, [si+CH_MP]
    pop si
    pop ax
    shl bx, 1
    add ax, [vb_mp+bx]
    jc .rcc
    cmp ax, cx
    jbe .rcs
.rcc:
    mov ax, cx
.rcs:
    mov [vb_mp+bx], ax
    ret
.rv:
    cmp al, V_REVIVE
    jne .gd
    cmp bl, ENEMY
    je .o
    xor bh, bh
    push bx
    shl bx, 1
    cmp word [vb_hp+bx], 0
    pop bx
    jne .o
    mov cx, [si+AC_EFF+EF_P]
    push bx
    call vb_char
    mov ax, [si+CH_HP]
    mul cx
    mov cx, 100
    div cx
    pop bx
    or ax, ax
    jnz .rv1
    inc ax
.rv1:
    shl bx, 1
    mov [vb_hp+bx], ax
    ret
.gd:
    cmp al, V_GUARD
    jne .cg
    cmp bl, ENEMY
    jne .gc
    mov byte [vb_eguard], 1
    ret
.gc:
    xor bh, bh
    mov byte [vb_guard+bx], 1
    ret
.cg:
    cmp al, V_CHARGE
    jne .bs
    cmp bl, ENEMY
    jne .ccc
    mov byte [vb_echarge], 1
    ret
.ccc:
    xor bh, bh
    mov byte [vb_charge+bx], 1
    ret
.bs:
    cmp al, V_LIMIT                 ; THE TARGETS' LIMIT GAUGE (a character
    jne .bs0                        ; hit): vbtrules.Sim.apply's
    cmp byte [si+AC_EFF+EF_A], 0
    jne .o
    cmp bl, ENEMY
    je .o
    cmp byte [vb_qhit+di], 0
    je .o
    mov al, [si+AC_EFF+EF_P]
    jmp vb_limadd                   ; (character BL)
.bs0:
    cmp al, V_BOOST
    je .mod
    cmp al, V_LOWER
    jne .o
.mod:
    cmp byte [vb_qhit+di], 0
    je .o
    mov ah, [si+AC_EFF+EF_B]        ; +steps or -steps
    cmp al, V_BOOST
    je .m1
    neg ah
.m1:
    mov cl, [si+AC_EFF+EF_A]        ; the stat
    xor ch, ch
    mov dl, [si+AC_EFF+EF_C]        ; the turns
    cmp bl, ENEMY
    jne .mc
    mov bx, cx
    lea si, [vb_emods+bx]
    lea di, [vb_emodt+bx]
    jmp short .ms
.mc:
    xor bh, bh
    shl bx, 1
    shl bx, 1
    add bx, cx
    lea si, [vb_mods+bx]
    push di
    lea di, [vb_modt+bx]
    pop bx                          ; (BX: the plan's index, unused below)
.ms:
    mov al, [si]                    ; (signed bytes, held to -4..4)
    add al, ah
    cmp al, 4
    jle .m3
    mov al, 4
.m3:
    cmp al, -4
    jge .m2
    mov al, -4
.m2:
    mov [si], al
    mov [di], dl
    ret

; vb_enter_stance - the enemy into stance AL for AH of its turns (0: until
; something changes it): vbtrules.Sim.enter_stance
vb_enter_stance:
    or ah, ah
    jz .forever
    mov dl, [vb_stance]
    mov [vb_sret], dl
    mov [vb_sleft], ah
    jmp short .in
.forever:
    mov byte [vb_sret], NONE
    mov byte [vb_sleft], 0
.in:
    mov [vb_stance], al
    mov byte [vb_sturns], 0
    ret

; vb_tick - the stat changes at DS:SI (steps) and DS:DI (turns) one turn
; older: vbtrules.Sim.tick
vb_tick:
    mov cx, 4
.t:
    cmp byte [di], 0
    je .n
    dec byte [di]
    jnz .n
    mov byte [si], 0
.n:
    inc si
    inc di
    loop .t
    ret

; vb_start_turn - character BL's turn begins: its guard ends, its stat
; changes age (vbtrules.Sim.start_turn)
vb_start_turn:
    xor bh, bh
    mov byte [vb_guard+bx], 0
    shl bx, 1
    shl bx, 1
    lea si, [vb_mods+bx]
    lea di, [vb_modt+bx]
    jmp vb_tick

; --- the enemy: vbtrules.Sim.pick_target, respond and enemy_turn ---------------------
; vb_pick - AL = the character rule AL (arg AH) picks, NONE when none stands
vb_pick:
    mov [vb_qrule], ax
    call vb_anystanding
    jnz .some
    mov al, NONE
    ret
.some:
    mov al, [vb_qrule]
    cmp al, R_RANDOM
    jne .lo
    call vb_nstanding               ; CX standing
    call vb_rnd
    jmp vb_nthstanding
.lo:
    cmp al, R_LOWEST
    jne .la
    mov dx, 0xFFFF
    mov al, NONE
    xor bx, bx
.l1:
    cmp bl, [RL+RH_NCH]
    jae .l3
    push bx
    shl bx, 1
    mov cx, [vb_hp+bx]
    pop bx
    jcxz .l2
    cmp cx, dx
    jae .l2
    mov dx, cx
    mov al, bl
.l2:
    inc bx
    jmp .l1
.l3:
    ret
.la:
    cmp al, R_LAST
    jne .na
    mov al, [vb_last]
    call vb_isstanding
    jnz .ret
    jmp short .alt
.na:
    cmp al, R_NAMED
    jne .alt
    mov al, [vb_qrule+1]
    call vb_isstanding
    jnz .ret
.alt:
    mov cl, [RL+RH_NCH]             ; alternate: the next standing from
    xor ch, ch                      ; [vb_tnext], round the party
    mov al, [vb_tnext]
.a1:
    push cx
    push ax
    call vb_isstanding
    pop ax
    pop cx
    jnz .a2
    inc al
    cmp al, [RL+RH_NCH]
    jb .a3
    xor al, al
.a3:
    loop .a1
    mov al, NONE
    ret
.a2:
    mov ah, al
    inc ah
    cmp ah, [RL+RH_NCH]
    jb .a4
    xor ah, ah
.a4:
    mov [vb_tnext], ah
.ret:
    ret

; vb_isstanding - ZF=0 when character AL stands (AL a character)
vb_isstanding:
    cmp al, [RL+RH_NCH]
    jae .no
    push bx
    mov bl, al
    xor bh, bh
    shl bx, 1
    cmp word [vb_hp+bx], 0
    pop bx
    ret
.no:
    cmp al, al
    ret

; vb_nstanding - CX = how many stand
vb_nstanding:
    xor cx, cx
    xor bx, bx
.c:
    cmp bl, [RL+RH_NCH]
    jae .o
    push bx
    shl bx, 1
    cmp word [vb_hp+bx], 0
    pop bx
    je .n
    inc cx
.n:
    inc bx
    jmp .c
.o:
    ret

; vb_nthstanding - AL = the AX-th standing character
vb_nthstanding:
    mov cx, ax
    xor bx, bx
.c:
    push bx
    shl bx, 1
    cmp word [vb_hp+bx], 0
    pop bx
    je .n
    jcxz .got
    dec cx
.n:
    inc bx
    cmp bl, [RL+RH_NCH]
    jb .c
    mov al, NONE
    ret
.got:
    mov al, bl
    ret

; vb_respond - after character [vb_qw]'s action [vb_qa]: the stance's
; responses in order, each whose trigger it meets drawing its chance; the
; first that comes up is planned, at the actor (vbtrules.Sim.respond).
; CF=1 a response is planned
vb_respond:
    mov al, [vb_qa]
    mov [vb_racted], al
    mov al, [vb_qw]
    mov [vb_ractor], al
    mov si, [vb_qact]
    mov al, [si+AC_FLAGS]
    mov [vb_rflags], al
    mov al, [vb_stance]
    call vb_strec
    mov cl, [si+ST_NRESP]
    xor ch, ch
    jcxz .no
    add si, ST_RESP
.r:
    push cx
    push si
    mov al, [si]                    ; the trigger
    or al, al
    jz .match
    cmp al, 1
    jne .r2
    test byte [vb_rflags], AF_PHYS
    jnz .match
    jmp short .next
.r2:
    cmp al, 2
    jne .r3
    test byte [vb_rflags], AF_MAG
    jnz .match
    jmp short .next
.r3:
    mov al, [si+1]
    cmp al, [vb_racted]
    jne .next
.match:
    mov cx, 100
    call vb_rnd
    pop si
    push si
    mov dl, [si+2]
    xor dh, dh
    cmp ax, dx
    jb .go
.next:
    pop si
    pop cx
    add si, 4
    loop .r
.no:
    clc
    ret
.go:
    pop si
    pop cx
    mov al, [si+3]
    push ax
    mov al, [vb_ractor]
    call vb_isstanding
    mov bh, [vb_ractor]
    jnz .at
    xor ax, ax                      ; (the actor fell: the rule picks)
    call vb_pick
    mov bh, al
.at:
    pop ax
    mov bl, ENEMY
    call vb_resolve
    mov byte [vb_qresp], 1
    stc
    ret

; vb_enemy_plan - the enemy's turn (vbtrules.Sim.enemy_turn): its guard
; ends and its stat changes age; a stance whose turns are up returns (its
; return action, if any, is the turn); else the stance's automatic change
; (its action, if any, is the turn); else a move drawn by weight, at a
; target by its rule. Planned into [vb_q*]
vb_enemy_plan:
    mov byte [vb_eguard], 0
    mov si, vb_emods
    mov di, vb_emodt
    call vb_tick
    mov byte [vb_eact], NONE
    mov word [vb_qrule], 0
    mov al, NONE                    ; A FOLLOW-UP OWED (126.6.7): this turn,
    xchg al, [vb_efol]              ; whatever else - the stance's timer
    mov [vb_qfol], al               ; still counts, its return gives way
    cmp byte [vb_sleft], 0
    je .auto
    dec byte [vb_sleft]
    jnz .auto
    mov al, [vb_stance]
    call vb_strec
    mov al, [si+ST_RET]
    mov [vb_eact], al
    mov al, [vb_sret]
    mov [vb_stance], al
    mov byte [vb_sret], NONE
    mov byte [vb_sturns], 0
.auto:
    mov al, [vb_qfol]
    cmp al, NONE
    je .au0
    call vb_folrec                  ; SI = the entry
    mov al, [si+1]
    mov [vb_eact], al
    jmp .target
.au0:
    cmp byte [vb_eact], NONE
    jne .target
    cmp byte [vb_sleft], 0
    jne .move
    mov al, [vb_stance]
    call vb_strec
    mov al, [si+ST_AUTO]
    or al, al
    jz .move
    cmp al, 1
    jne .au2
    mov ax, [vb_ehp]                ; HP under arg %: ehp*100 < most*arg
    mov cx, 100
    mul cx
    mov [vb_q32], ax
    mov [vb_q32+2], dx
    mov al, [si+ST_AUTO+1]
    xor ah, ah
    mul word [RL+RH_EHP]
    cmp [vb_q32+2], dx
    jb .fire
    ja .move
    cmp [vb_q32], ax
    jb .fire
    jmp short .move
.au2:
    mov al, [vb_sturns]
    cmp al, [si+ST_AUTO+1]
    jb .move
.fire:
    mov al, [si+ST_AUTO+2]
    cmp al, [vb_stance]
    je .move
    mov ah, [si+ST_AUTO+3]
    mov [vb_eact], ah
    xor ah, ah
    call vb_enter_stance
.move:
    cmp byte [vb_eact], NONE
    jne .target
    mov al, [vb_stance]             ; a move, drawn by weight
    call vb_strec
    mov cl, [si+ST_NMOV]
    xor ch, ch
    push si
    xor dx, dx
    add si, ST_MOVES
    mov bx, cx
.w:
    mov al, [si+1]
    xor ah, ah
    add dx, ax
    add si, 4
    loop .w
    pop si
    mov cx, dx
    push si
    call vb_rnd
    pop si
    add si, ST_MOVES
.pk:
    mov dl, [si+1]
    xor dh, dh
    cmp ax, dx
    jb .took
    sub ax, dx
    add si, 4
    jmp .pk
.took:
    mov al, [si]
    mov [vb_eact], al
    mov ax, [si+2]                  ; its rule, its arg
    mov [vb_qrule], ax
.target:
    mov al, [vb_eact]
    call vb_act
    mov bh, NONE
    mov al, [si+AC_TGT]
    cmp al, T_ENEMY
    je .tp
    cmp al, T_CHAR
    jne .go
.tp:
    cmp byte [vb_qfol], NONE
    je .tq
    call vb_foltgt
    jmp short .tr
.tq:
    mov ax, [vb_qrule]
    call vb_pick
.tr:
    mov bh, al
.go:
    mov cl, [RL+RH_NFOL]            ; this turn's action OWES one: the first
    xor ch, ch                      ; follow-up naming it, at this target
    jcxz .gf
    xor al, al
.gl:
    push ax
    call vb_folrec
    pop ax
    mov dl, [si]
    cmp dl, [vb_eact]
    jne .gn
    mov [vb_efol], al
    mov [vb_eftgt], bh
    jmp short .gf
.gn:
    inc al
    loop .gl
.gf:
    inc byte [vb_sturns]
    mov al, [vb_eact]
    mov bl, ENEMY
    jmp vb_resolve

; vb_folrec - AL = a follow-up: SI = its entry (after, then, rule, arg) in
; the rules. AX spent
vb_folrec:
    xor ah, ah
    shl ax, 1
    shl ax, 1
    add ax, [RL+RH_OFOL]
    add ax, RL
    mov si, ax
    ret

; vb_foltgt - vbtrules.Sim.fol_target: AL = follow-up [vb_qfol]'s target -
; the one its first action was played at while that one stands
; (FOL_SAME), else by its rule, the same falling back to the alternate
vb_foltgt:
    mov al, [vb_qfol]
    call vb_folrec
    mov ax, [si+2]                  ; its rule, its arg
    cmp al, FOL_SAME
    jne .p
    mov al, [vb_eftgt]
    call vb_isstanding
    jz .alt
    ret
.alt:
    xor ax, ax
.p:
    jmp vb_pick

; vb_eguess - [vb_eclip] = the clip the enemy most likely plays next, for
; the read-ahead: its stance's heaviest move's HIT pool's first clip
vb_eguess:
    mov byte [vb_eclip], NONE
    mov al, [vb_stance]
    call vb_strec
    mov cl, [si+ST_NMOV]
    xor ch, ch
    jcxz .o
    add si, ST_MOVES
    xor dx, dx                      ; DL the weight, DH the action
.m:
    mov al, [si+1]
    cmp al, dl
    jbe .n
    mov dl, al
    mov dh, [si]
.n:
    add si, 4
    loop .m
    mov al, dh
    call vb_act
    mov al, [si+AC_POOLS]
    cmp al, NONE
    je .o
    call vb_poolrec
    cmp byte [si], 0
    je .o
    mov al, [si+1]
    mov [vb_eclip], al
.o:
    ret
