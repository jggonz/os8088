; Cartridge-directed encounters and native stage/player/audio state machines.
WT equ 0
WP equ 2
WK equ 4
WR equ 6
WB equ 8
WA equ 10
WF equ 12
WPOW equ 14
WSIZE equ 16

n_stageinit:
    SAVE
    push ds
    pop es
    xor ax,ax
    mov di,n_waveslive
    mov cx,13*WSIZE/2
    rep stosw
    mov di,n_enemies
    mov cx,12*ESIZE/2
    rep stosw
    mov di,n_blasts
    mov cx,24
    rep stosw
    mov ax,0ffffh
    mov di,n_shots
    mov cx,16*2+16*4
    rep stosw
    mov [n_picky],ax
    mov word [n_picktype],0
    mov word [n_spawned],0
    mov word [n_enemyfirecount],0
    mov word [n_bigfirecount],0
    mov word [n_burstleft],0
    mov word [n_kills],0
    mov word [n_distance],0
    mov word [n_routefrac],0
    mov word [n_scrollfrac],0
    mov word [n_scrollrows],0
    mov word [n_endclear],0
    mov di,n_motion
    mov cx,12*24/2
    xor ax,ax
    rep stosw
    mov word [n_rolls],3
    mov word [n_roll],0
    mov word [n_grace],60
    mov word [n_px],116
    mov word [n_py],176
    mov word [n_bank],n_playerart
    mov word [n_flightphase],1
    mov word [n_flightphasetime],32
    mov word [n_effectptr],0
    mov byte [n_rebuild],0
    mov byte [n_bossmade],0
    mov byte [n_bosslive],0
    mov word [n_scrollstage],0
    mov bx,[n_stage]
    dec bx
    shl bx,1
    mov ax,[n_stageevents+bx]
    mov [n_event],ax
    mov byte [n_huddirty],1
    mov ax,4
    call n_music
    RESTORE
    ret

n_campaignnew:
    SAVE
    mov word [n_entryseq],0
    mov word [n_activeplayer],0
    mov word [n_wings],0
    mov word [n_nextlife],400
    mov word [n_secretkills],0
    mov word [n_secretlimit],200
    mov word [n_playersave+0],3
    mov word [n_playersave+2],1
    mov word [n_playersave+4],0
    mov word [n_playersave+6],0
    mov word [n_playersave+8],1
    mov word [n_playersave+10],400
    mov word [n_playersave+12],0
    mov word [n_playersave+14],200
    mov word [n_playersave+16],0
    mov word [n_playersave+18],3
    mov word [n_playersave+20],1
    mov word [n_playersave+22],0
    mov word [n_playersave+24],0
    mov word [n_playersave+26],1
    mov word [n_playersave+28],400
    mov word [n_playersave+30],0
    mov word [n_playersave+32],200
    mov word [n_playersave+34],0
    mov word [n_extended],0
    call n_stageinit
    RESTORE
    ret

n_scripts:
    SAVE
    ; A boss encounter holds the scroll and all scheduled reinforcements.
    cmp byte [n_bosslive],0
    jne .out
    ; $DE42 adds -$00C0 to the scroll: 3/4 pixel per enemy update.
    add word [n_routefrac],3
    cmp word [n_routefrac],4
    jb .events
    sub word [n_routefrac],4
    inc word [n_distance]
.events:
    cmp word [n_distance],1792      ; $DC86: last page, Y <= $80
    jb .event
    cmp word [n_endclear],0
    jne .out
    mov word [n_endclear],1
    call n_clearencounters
    push es
    push ds
    pop es
    mov di,n_bullets
    mov ax,0ffffh
    mov cx,16*4
    rep stosw
    pop es
    jmp .out
.event:
    mov si,[n_event]
    mov ax,[si]
    cmp ax,[n_distance]
    ja .queues
    ; $D9C5 skips events whose route page has already passed. A saturated
    ; pool must not replay stale reinforcements on a later stretch of sea.
    push ax
    xor dx,dx
    mov bx,240
    div bx
    inc ax
    mul bx
    cmp [n_distance],ax
    pop ax
    jb .currentpage
    add word [n_event],4
    jmp .events
.currentpage:
    mov bx,[si+2]
%if !N_ROM
    ; Original-art schedules use compact wave IDs for their orange groups.
    cmp bx,4
    je .flush
    cmp bx,5
    je .flush
%endif
    cmp bx,16
    je .flush
    cmp bx,17
    je .flush
    cmp bx,18
    je .flush
    cmp bx,23
    je .flush
    cmp bx,28
    jne .descriptor
.flush:
    ; One orange formation can occupy five separate wave records ($12..$16,
    ; $17..$1B, $1C..$20). Keep a shared reward group across those records.
    mov si,n_enemies
.detach:
    cmp word [si+EGROUP],n_orangewave
    jne .detachnext
    mov word [si+EGROUP],0
.detachnext:
    add si,ESIZE
    cmp si,n_enemies+12*ESIZE
    jb .detach
    push bx
    push es
    push ds
    pop es
    xor ax,ax
    mov di,n_orangewave
    mov cx,WSIZE/2
    rep stosw
    pop es
    pop bx
    mov di,n_waveslive
.flushloop:
    mov word [di+WR],0
    mov word [di+WF],1
    add di,WSIZE
    cmp di,n_waveslive+12*WSIZE
    jb .flushloop
.descriptor:
    shl bx,1
    shl bx,1
    shl bx,1
    add bx,n_waves
    mov di,n_waveslive
.findqueue:
    mov ax,[di+WR]
    or ax,[di+WA]
    jz .queue
    add di,WSIZE
    cmp di,n_waveslive+12*WSIZE
    jb .findqueue
    jmp .queues
.queue:
    mov ax,[bx]
    mov [di+WK],ax
    mov word [di+WPOW],0ffffh
    cmp ax,10
    jb .period
    cmp ax,22
    ja .period
    mov word [di+WPOW],0
    cmp ax,10
    je .period
    inc word [di+WPOW]
    cmp ax,11
    je .period
    inc word [di+WPOW]
    cmp ax,17
    jb .period
    mov word [di+WPOW],4
    cmp ax,20
    jb .period
    mov word [di+WPOW],3
.period:
    ; NES $DA81 ticks each wave once; $EB19 supplies period and batch size.
    mov ax,[bx+2]
    mov [di+WT],ax
    mov [di+WP],ax
    mov ax,[bx+4]
    mov [di+WB],ax
    mov ax,[bx+6]
    mov [di+WR],ax
    cmp word [di+WPOW],0ffffh
    je .queued
    add [n_orangewave+WR],ax
    mov ax,[di+WPOW]
    mov [n_orangewave+WPOW],ax
.queued:
    mov word [di+WF],0
    add word [n_event],4
    jmp .events
.queues:
    mov di,n_waveslive
.qloop:
    cmp word [di+WR],0
    je .qnext
    cmp word [di+WT],0
    je .ready
    dec word [di+WT]
    jnz .qnext
.ready:
    mov ax,[di+WP]
    mov [di+WT],ax              ; retry after the period, with no timer debt
    mov bp,[di+WB]
.batch:
    call n_scriptspawn
    jc .qnext
    dec word [di+WR]
    mov bx,[si+EGROUP]
    or bx,bx
    jz .counted
    inc word [bx+WA]
    cmp bx,di
    je .counted
    dec word [bx+WR]
.counted:
    cmp word [di+WR],0
    je .qnext
    dec bp
    jnz .batch
.qnext:
    add di,WSIZE
    cmp di,n_waveslive+12*WSIZE
    jb .qloop
    mov ax,[n_secretkills]
    cmp ax,[n_secretlimit]
    jb .bosstest
    ; NES secret aircraft uses its own actor ($18), outside the regular pool.
    mov si,n_enemies+N_ENEMY_LIMIT*ESIZE
    cmp word [si+EON],0
    jne .bosstest
    mov word [n_secretkills],0
    mov word [n_secretlimit],150
    mov word [si+EON],1
    mov word [si+EKIND],8
    mov word [si+ETYPE],49
    mov word [si+EX],24
    mov word [si+EY],212
    mov word [si+EHP],1
    mov word [si+EVX],0
    mov word [si+EDY],-3
    mov word [si+EPATH],0
    mov word [si+EGROUP],0
    mov word [si+EAGE],0
    inc word [n_spawned]
.bosstest:
    ; The NES super-bomber appears on played stages 7,15,23,31.
    mov ax,[n_stage]
    and ax,7
    cmp ax,7
    jne .out
    cmp word [n_distance],1297      ; $DDA4: boss page 2, Y < $90
    jb .out
    cmp byte [n_bossmade],0
    jne .out
    call n_clearencounters
    call n_freeslot
    jc .out
    mov word [si+EON],1
    mov word [si+EKIND],2
    mov word [si+ETYPE],255
%if N_ROM
    mov word [si+EX],48
    mov ax,[n_worldy]
    cmp byte [n_cga],0
    je .bossy
    mov bx,6
    mul bx
    mov bx,5
    div bx
.bossy:
    mov bx,88
    sub bx,ax
    mov [si+EY],bx
%else
    mov word [si+EX],100
    mov word [si+EY],24
%endif
    mov word [si+EVX],2
    mov word [si+EDY],1
    mov word [si+EAGE],0
    mov word [si+EGROUP],0
    mov word [si+EPATH],0
    mov ax,[n_stage]
    add ax,30
    mov [si+EHP],ax
    mov byte [n_bossmade],1
    mov byte [n_bosslive],1
    inc word [n_spawned]
.out:
    RESTORE
    ret

n_freeslot:
    ; NES $DAC2 allocates eight regular aircraft ($0E..$15).
    mov si,n_enemies
.loop:
    cmp word [si+EON],0
    je .yes
    add si,ESIZE
    cmp si,n_enemies+N_ENEMY_LIMIT*ESIZE
    jb .loop
    stc
    ret
.yes:
    clc
    ret

n_scriptspawn:                       ; DI=wave, preserves DI/BP
    call n_freeslot
    jc .out
    mov ax,[di+WK]
    mov [si+ETYPE],ax
    ; Ordinary actors do not keep their spawning queue alive. $DB18 frees
    ; a queue as soon as its remaining count reaches zero, even if its
    ; aircraft are still on screen. Only orange actors need a reward group.
    mov word [si+EGROUP],0
    cmp word [di+WPOW],0ffffh
    je .grouped
    mov word [si+EGROUP],n_orangewave
.grouped:
    mov word [si+EAGE],0
    mov word [si+EON],1
    mov word [si+EHP],1
    mov word [si+EPATH],0
    mov word [si+EVX],1
    mov word [si+EDY],2
    mov word [si+EAUX],0
    mov word [si+EKIND],0
    cmp ax,10
    jb .small
    cmp ax,22
    ja .large
    mov word [si+EKIND],1
    jmp .path
.large:
    mov word [si+EKIND],5
    mov word [si+EHP],3
    cmp ax,32
    jb .path
    mov word [si+EKIND],6
    mov word [si+EHP],5
    cmp ax,41
    jb .path
    mov word [si+EKIND],7
    mov word [si+EHP],15
.path:
    mov bx,ax
    sub bx,10
    shl bx,1
    mov bx,[n_paths+bx]
    mov ax,[bx]
    mov [si+EX],ax
    mov ax,[bx+2]
    mov [si+EY],ax
    add bx,4
    mov [si+EPATH],bx
    jmp .spawned
.small:
%if N_ROM
    jmp .spawned                   ; $071F sequence handled by n_motioninit
%endif
    and ax,3
    cmp ax,0
    je .position
    add ax,2
    mov [si+EKIND],ax
.position:
    mov ax,[si+ETYPE]
    xor bx,bx
    cmp ax,3
    jb .starttable
    mov bx,2
    cmp ax,5
    jb .starttable
    mov bx,4
.starttable:
    call n_rand
    xor dx,dx
    div word [n_startcounts+bx]
    mov bx,[n_smallstarts+bx]
    shl dx,1
    shl dx,1
    add bx,dx
    mov ax,[bx]
    mov [si+EX],ax
    mov ax,[bx+2]
    mov [si+EY],ax
    mov word [si+EVX],1
    mov ax,[si+EX]
    cmp ax,[n_px]
    jl .spawned
    mov word [si+EVX],-1
.spawned:
%if N_ROM
    call n_motioninit
%endif
    inc word [n_spawned]
    clc
.out:
    ret

n_pathmove:                          ; SI=actor; CF set when path ends
    mov bx,[si+EPATH]
    or bx,bx
    jz .none
    cmp word [bx],32767
    je .end
    mov ax,[bx]
    sub ax,[si+EX]
    mov dx,[bx+2]
    sub dx,[si+EY]
    mov cx,2
    cmp word [si+EKIND],6
    jb .speed
    mov cx,1
.speed:
    mov word [si+EVX],0
    mov word [si+EDY],0
    cmp ax,cx
    jg .right
    neg cx
    cmp ax,cx
    jl .left
    add [si+EX],ax
    jmp .y
.right:
    mov [si+EVX],cx
    add [si+EX],cx
    jmp .y
.left:
    mov [si+EVX],cx
    add [si+EX],cx
.y:
    mov cx,2
    cmp word [si+EKIND],6
    jb .yspeed
    mov cx,1
.yspeed:
    cmp dx,cx
    jg .down
    neg cx
    cmp dx,cx
    jl .up
    add [si+EY],dx
    jmp .target
.down:
    mov [si+EDY],cx
    add [si+EY],cx
    jmp .target
.up:
    mov [si+EDY],cx
    add [si+EY],cx
.target:
    mov ax,[bx]
    cmp ax,[si+EX]
    jne .none
    mov ax,[bx+2]
    cmp ax,[si+EY]
    jne .none
    add word [si+EPATH],4
.none:
    clc
    ret
.end:
    stc
    ret

n_clearencounters:
    mov word [n_orangewave+WR],0
    mov word [n_orangewave+WF],1
    mov di,n_waveslive
.waves:
    mov word [di+WR],0
    mov word [di+WF],1
    add di,WSIZE
    cmp di,n_waveslive+12*WSIZE
    jb .waves
    mov si,n_enemies
.actors:
    xor ax,ax
    call n_depart
    add si,ESIZE
    cmp si,n_enemies+12*ESIZE
    jb .actors
    ret

; Procedural small-plane maneuvers, with cartridge entry positions. The NES
; routines $F054/$F176/$F42C steer relative to the player; native phases retain
; the dive/reversal, sweeping turn and fast cross-screen pass behaviors.
n_smallmove:
    mov ax,[si+EVX]
    add [si+EX],ax
    mov ax,[si+EDY]
    add [si+EY],ax
    cmp word [si+EX],-64
    jl .end
    cmp word [si+EX],304
    jg .end
    cmp word [si+EY],-96
    jl .end
    cmp word [si+EY],256
    jg .end
    cmp word [si+EAGE],350
    ja .end
    mov ax,[si+ETYPE]
    cmp ax,3
    jb .sweep
    cmp ax,5
    jae .cross
    cmp word [si+EPATH],0
    jne .return
    mov ax,[si+EY]
    cmp ax,[n_py]
    jl .out
    mov word [si+EPATH],1
    mov word [si+EDY],-3
    neg word [si+EVX]
    jmp .out
.return:
    cmp word [si+ETYPE],4
    jne .out
    cmp word [si+EY],32
    jg .out
    cmp word [si+EPATH],1
    jne .out
    mov word [si+EPATH],2
    mov word [si+EDY],3
    jmp .out
.cross:
    cmp word [si+EPATH],0
    jne .out
    mov ax,[n_py]
    sub ax,48
    cmp [si+EY],ax
    jl .out
    mov word [si+EPATH],1
    mov word [si+EDY],0
    mov word [si+EVX],3
    mov ax,[si+EX]
    cmp ax,[n_px]
    jl .out
    mov word [si+EVX],-3
    jmp .out
.sweep:
    test word [si+EAGE],31
    jnz .out
    neg word [si+EVX]
    cmp word [si+EAGE],64
    jb .out
    mov word [si+EDY],-2
.out:
    clc
    ret
.end:
    stc
    ret

n_depart:                           ; AX=0 escaped, 1 killed; SI=actor
    SAVE
    mov word [si+EON],0
    cmp word [si+EKIND],2
    jne .group
    mov byte [n_bosslive],0
    mov byte [n_rebuild],1
.group:
    mov bx,[si+EGROUP]
    or bx,bx
    jz .out
    mov word [si+EGROUP],0
    or ax,ax
    jnz .killed
    mov word [bx+WF],1
.killed:
    cmp word [bx+WA],0
    je .out
    dec word [bx+WA]
    mov ax,[bx+WA]
    or ax,[bx+WR]
    or ax,[bx+WF]
    jnz .out
    cmp word [bx+WPOW],0ffffh
    je .out
    mov ax,[bx+WPOW]
    mov [n_picktype],ax
    mov ax,[si+EX]
    mov [n_pickx],ax
    mov ax,[si+EY]
    mov [n_picky],ax
    mov ax,500
    call n_addscore
.out:
    RESTORE
    ret

n_awardpow:
    SAVE
    mov ax,[n_picktype]
    cmp ax,1
    je .bomb
    cmp ax,2
    je .wing
    cmp ax,3
    je .roll
    cmp ax,4
    je .life
    cmp ax,5
    je .secret
    mov word [n_weapon],2
    jmp .points
.bomb:
    mov si,n_enemies
.bombloop:
    cmp word [si+EON],0
    je .bombnext
    cmp word [si+EKIND],2
    je .bombnext
    call n_kill
.bombnext:
    add si,ESIZE
    cmp si,n_enemies+12*ESIZE
    jb .bombloop
    push ds
    pop es
    mov ax,0ffffh
    mov di,n_bullets
    mov cx,16*4
    rep stosw
    jmp .points
.wing:
    mov word [n_wings],3
    jmp .points
.roll:
    cmp word [n_rolls],9
    jae .points
    inc word [n_rolls]
    jmp .points
.life:
    cmp word [n_lives],9
    jae .points
    inc word [n_lives]
    jmp .points
.secret:
    mov ax,5000
    jmp .score
.points:
    mov ax,1000
.score:
    call n_addscore
    mov ax,15
    call n_effect
    RESTORE
    ret

n_wingfire:
    test word [n_wings],1
    jz .right
    mov cx,[n_px]
    sub cx,14
    call n_shot
.right:
    test word [n_wings],2
    jz .out
    mov cx,[n_px]
    add cx,34
    call n_shot
.out:
    ret

n_wingdraw:
    mov si,n_wingart
    mov dx,[n_py]
    add dx,4
    test word [n_wings],1
    jz .right
    mov cx,[n_px]
    sub cx,20
    call n_sprite
.right:
    test word [n_wings],2
    jz .out
    mov cx,[n_px]
    add cx,28
    call n_sprite
.out:
    ret

n_winghit:                          ; bullet CX/DX; CF=absorbed by wing
    cmp word [n_grace],0
    jne .miss
    cmp word [n_roll],0
    jne .miss
    mov ax,dx
    sub ax,[n_py]
    cmp ax,20
    jae .miss
    test word [n_wings],1
    jz .right
    mov ax,cx
    sub ax,[n_px]
    add ax,20
    cmp ax,16
    jae .right
    and word [n_wings],2
    stc
    ret
.right:
    test word [n_wings],2
    jz .miss
    mov ax,cx
    sub ax,[n_px]
    sub ax,28
    cmp ax,16
    jae .miss
    and word [n_wings],1
    stc
    ret
.miss:
    clc
    ret

n_wingcontact:                      ; SI=enemy, BX=kind*2; CF=wing collision
    cmp word [n_grace],0
    jne .miss
    cmp word [n_roll],0
    jne .miss
    mov ax,[n_py]
    add ax,16
    cmp [si+EY],ax
    jge .miss
    mov ax,[si+EY]
    add ax,[n_enemyheights+bx]
    mov dx,[n_py]
    add dx,4
    cmp ax,dx
    jle .miss
    mov ax,[si+EX]
    add ax,[n_enemywidths+bx]
    mov dx,[n_px]
    sub dx,20
    test word [n_wings],1
    jz .right
    cmp ax,dx
    jle .right
    add dx,12
    cmp [si+EX],dx
    jge .right
    and word [n_wings],2
    stc
    ret
.right:
    test word [n_wings],2
    jz .miss
    mov dx,[n_px]
    add dx,28
    cmp ax,dx
    jle .miss
    add dx,12
    cmp [si+EX],dx
    jge .miss
    and word [n_wings],1
    stc
    ret
.miss:
    clc
    ret

n_results:
    SAVE
    mov word [n_flightphase],3
    mov word [n_flightphasetime],100
    xor ax,ax
    cmp word [n_spawned],0
    je .percent
    mov ax,[n_kills]
    mov bx,100
    mul bx
    div word [n_spawned]
    cmp ax,100
    jbe .percent
    mov ax,100
.percent:
    mov [n_percent],ax
    mov bx,ax
    shl bx,1
    shl bx,1
    mov ax,[n_bonuses+bx]
    mov [n_percentbonus],ax
    mov dx,[n_bonuses+bx+2]
    mov [n_percentbonus+2],dx
    or dx,dx
    jz .award
    mov ax,50000
    call n_addscore
    mov ax,50000
.award:
    call n_addscore
    mov ax,[n_rolls]
    mov bx,1000
    mul bx
    mov [n_rollbonus],ax
    call n_addscore
    mov ax,11
    call n_music
    RESTORE
    ret

n_flightphaseupdate:
    SAVE
    inc word [n_frames]
    cmp word [n_flightphase],1
    je .takeoff
    cmp word [n_flightphase],2
    je .landing
    cmp word [n_flightphase],4
    je .turn
    cmp word [n_flightphasetime],0
    je .advance
    dec word [n_flightphasetime]
    jmp .out
.takeoff:
    cmp word [n_py],204
    jae .takewait
    inc word [n_py]
.takewait:
    dec word [n_flightphasetime]
    jnz .out
    mov word [n_flightphase],0
%if N_ROM
    ; $D901/$D90E scroll 60 * 3/4 pixels during takeoff. Our takeoff
    ; animation is shorter, but combat starts at the same route position.
    mov word [n_distance],45
    mov word [n_worldpage],1
    mov word [n_worldy],211         ; 240 - 45 + 16-row HUD
    cmp byte [n_cga],0
    je .takeoffroute
    mov word [n_worldy],176         ; scaled route + 14-row HUD
.takeoffroute:
    mov byte [n_rebuild],1
%endif
    mov ax,[n_stage]               ; stages 1-10, 11-21, 22-32: three scores
    mov bx,14
    cmp ax,11
    jb .score
    mov bx,23
    cmp ax,22
    jb .score
    inc bx
.score:
    mov ax,bx
    call n_music
    jmp .out
.landing:
    mov ax,116
    cmp [n_px],ax
    je .landy
    jb .landright
    dec word [n_px]
    jmp .landy
.landright:
    inc word [n_px]
.landy:
    cmp word [n_py],120
    jbe .landed
    dec word [n_py]
    jmp .out
.landed:
    call n_results
    jmp .out
.turn:
    dec word [n_flightphasetime]
    jnz .out
    call n_stageinit
    jmp .out
.advance:
    inc word [n_stage]
    cmp word [n_stage],32
    ja .win
    call n_stageinit
    call n_palette
    jmp .out
.win:
    mov word [n_stage],32
    mov byte [n_state],3
    mov ax,7
    call n_music
.out:
    RESTORE
    ret

n_turn:
    SAVE
    mov word [n_wings],0
    mov word [n_weapon],1
    mov bx,[n_activeplayer]
    mov ax,18
    mul bx
    mov bx,ax
    add bx,n_playersave
    mov di,n_savefields
    mov cx,9
.save:
    mov si,[di]
    mov ax,[si]
    mov [bx],ax
    add di,2
    add bx,2
    loop .save
    cmp word [n_players],2
    jne .single
    mov ax,[n_activeplayer]
    xor ax,1
    mov bx,ax
    push ax
    mov ax,18
    mul bx
    mov bx,ax
    pop ax
    add bx,n_playersave
    cmp word [bx],0
    je .single
    mov [n_activeplayer],ax
    mov di,n_savefields
    mov cx,9
.load:
    mov si,[di]
    mov ax,[bx]
    mov [si],ax
    add di,2
    add bx,2
    loop .load
    call n_stageinit
    mov word [n_flightphase],4
    mov word [n_flightphasetime],45
    mov byte [n_huddirty],1
    jmp .out
.single:
    cmp word [n_lives],0
    jne .out
    mov byte [n_state],2
    mov ax,7
    call n_music
.out:
    RESTORE
    ret
n_savefields: dw n_lives,n_weapon,n_scorelo,n_scorehi,n_stage,n_nextlife,n_secretkills,n_secretlimit,n_extended

n_flightphaseart:
    SAVE
    cmp word [n_flightphase],3
    je .results
    cmp word [n_flightphase],4
    je .ready
    cmp word [n_flightphase],1
    jne .out
.ready:
    mov si,n_msgready
    mov ax,[n_activeplayer]
    add al,'1'
    mov [n_msgready+7],al
    mov cx,64
    mov dx,88
    call n_text
    jmp .out
.results:
    mov si,n_msgresults
    mov cx,56
    mov dx,64
    call n_text
    mov ax,[n_percent]
    mov si,n_resultpct+9
    mov cx,3
    call n_decimal
    mov si,n_resultpct
    mov cx,48
    mov dx,96
    call n_text
    mov ax,[n_percentbonus]
    mov si,n_resultbonus+16
    mov cx,6
    cmp word [n_percentbonus+2],0
    je .bonusdigits
    xor ax,ax
.bonusdigits:
    call n_decimal
    cmp word [n_percentbonus+2],0
    je .bonuslabel
    mov byte [n_resultbonus+11],'1'
.bonuslabel:
    mov si,n_resultbonus
    mov cx,40
    mov dx,112
    call n_text
    mov ax,[n_rollbonus]
    mov si,n_resultroll+15
    mov cx,5
    call n_decimal
    mov si,n_resultroll
    mov cx,40
    mov dx,128
    call n_text
    mov si,n_msgcontinue
    mov cx,48
    mov dx,152
    call n_text
.out:
    RESTORE
    ret
n_decimal:                          ; AX, SI=last digit, CX=count
    mov bx,10
.loop:
    xor dx,dx
    div bx
    add dl,'0'
    mov [si],dl
    dec si
    loop .loop
    ret
n_msgready: db 'PLAYER 1  READY',0
n_msgresults: db 'MISSION RESULTS',0
n_resultpct: db 'DOWNED 000%',0
n_resultbonus: db 'PERCENT    000000',0
n_resultroll: db 'ROLL BONUS 00000',0
n_msgcontinue: db 'ENTER TO CONTINUE',0

n_enemyframe:                       ; DI=actor, return SI=sprite
    mov bx,[di+EKIND]
    shl bx,1
    mov si,[n_enemyarts+bx]
    cmp bx,12
    jae .out
    cmp bx,4
    je .out
    mov bx,n_dirs28
    cmp word [di+EKIND],1
    jne .scout
    mov bx,n_dirs10
.scout:
    cmp word [di+EKIND],4
    jne .diver
    mov bx,n_dirs40
.diver:
    cmp word [di+EKIND],5
    jne .angle
    mov bx,n_dirs60
.angle:
    mov ax,4
    cmp word [di+EDY],0
    jg .down
    je .horizontal
    xor ax,ax
    cmp word [di+EVX],0
    je .index
    mov ax,1
    jg .index
    mov ax,7
    jmp .index
.horizontal:
    cmp word [di+EVX],0
    je .index
    mov ax,2
    jg .index
    mov ax,6
    jmp .index
.down:
    cmp word [di+EVX],0
    je .index
    jg .right
    inc ax
    jmp .index
.right:
    dec ax
    and ax,7
.index:
    cmp bx,n_dirs60
    je .angleindex
    test word [di+EAGE],4
    jz .angleindex
    add ax,8
.angleindex:
    shl ax,1
    add bx,ax
    mov si,[bx]
.out:
    ret
n_dirs10: dw n_dir10_0art,n_dir10_1art,n_dir10_2art,n_dir10_3art,n_dir10_4art,n_dir10_5art,n_dir10_6art,n_dir10_7art,n_dir10_8art,n_dir10_9art,n_dir10_10art,n_dir10_11art,n_dir10_12art,n_dir10_13art,n_dir10_14art,n_dir10_15art
n_dirs28: dw n_dir28_0art,n_dir28_1art,n_dir28_2art,n_dir28_3art,n_dir28_4art,n_dir28_5art,n_dir28_6art,n_dir28_7art,n_dir28_8art,n_dir28_9art,n_dir28_10art,n_dir28_11art,n_dir28_12art,n_dir28_13art,n_dir28_14art,n_dir28_15art
n_dirs40: dw n_dir40_0art,n_dir40_1art,n_dir40_2art,n_dir40_3art,n_dir40_4art,n_dir40_5art,n_dir40_6art,n_dir40_7art,n_dir40_8art,n_dir40_9art,n_dir40_10art,n_dir40_11art,n_dir40_12art,n_dir40_13art,n_dir40_14art,n_dir40_15art
n_dirs60: dw n_dir60_0art,n_dir60_1art,n_dir60_2art,n_dir60_3art,n_dir60_4art,n_dir60_5art,n_dir60_6art,n_dir60_7art

%include "audio.inc"

n_hightext:
    SAVE
    mov bx,[n_highlo]
    mov bp,[n_highhi]
    mov si,n_highmsg+10
    mov cx,10
.digit:
    mov ax,bp
    xor dx,dx
    div cx
    mov bp,ax
    mov ax,bx
    div cx
    mov bx,ax
    add dl,'0'
    mov [si],dl
    dec si
    cmp si,n_highmsg+5
    jae .digit
    mov si,n_highmsg
    mov cx,80
    mov dx,200
    call n_text
    RESTORE
    ret
n_highmsg: db 'HIGH 000000',0
