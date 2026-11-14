; walink.inc - WireArena over a null modem: the lobby in the window and the
; match in the bracket (docs/plans/WIREARENA-LINK-PLAN.md; protocol v1 is
; its section 4). The port, the frames and their CRC are apps/os88cable.inc's.
;
; In the window (UI task, its one-tick timer): the search, HELLO every
; WL_BEAT ticks, binding a peer, the host by identity, Ready and START.
; In the bracket (lk_poll and lk_send only, legal there - SPEC.md 53.7):
; SNAP every third sim tick, SCORE once a second, EVENTs until acked, and
; the other player drawn as figure 0.

WL_HELLO    equ 1
WL_SNAP     equ 2               ; a player, no bots in the match
WL_SCORE    equ 3               ; the sender's row of deaths by cause
WL_EVENT    equ 4
WL_HSNAP    equ 5               ; the host's, with bots: and theirs (L2)
WL_GSNAP    equ 6               ; the guest's, with bots: and its hits on them
WL_BSCORE   equ 7               ; the host's bots' rows of deaths by cause
LK_NTYPES   equ 8
lk_lens:    db 0, 20, 15, 8, 19, 24, 17, 12 ; each type's body, its CRC too
WL_BEAT     equ 9               ; OS ticks between HELLOs (WireFleet's)
WL_QUIET    equ 55              ; ticks of silence: gone (lobby), lost (match)
WL_EVQ      equ 8               ; events waiting for their ack
WL_EVSZ     equ 14              ; an event: its kind and thirteen bytes
WL_RESEND   equ 20              ; sim ticks before the unacked go again: an
                                ; ack's round trip with bots is a SNAP
                                ; period (5), the bytes, the other's frame
                                ; (it reads once one) - 9 resent every event
EV_FIRE     equ 1               ; a projectile: its kind | its owner << 4, x,
                                ; y, z, and its velocity, as it left
EV_TAKE     equ 2               ; a pickup taken: its index
EV_ROLL     equ 3               ; (the host's) a pickup back: index, kind
WL_PSNAP    equ 3               ; sim ticks a SNAP, no bots: 10 Hz
WL_PSNAPB   equ 5               ; ...with bots: 6 Hz (LINK-PLAN 2, 11.2)
WR_X        equ 0               ; a remote figure's record (wa_lkr, ten bytes
WR_Y        equ 2               ;   a figure): where its machine says it is,
WR_Z        equ 4               ;   its height (-1: the floor's), facing,
WR_A        equ 6               ;   its flags (SNAP byte 9's)
WR_F        equ 8
WR_SZ       equ 10
WL_ROWS     equ 17              ; the window's rows of text
WL_COLS     equ 38

%include "os88cable.inc"

; --- the window -------------------------------------------------------------

; wa_lkinit - the window is open: an identity, Automatic, the timer
wa_lkinit:
    call wa_lknewid
    mov byte [wa_lkfull], 1
    call lk_auto
    mov word [lk_blk+LK_MSG], lk_m_search
    mov byte [wa_lkarena], 2    ; (the heights, two normal bots: the host
    mov byte [wa_lkdiff], 1     ; may change all three)
    mov byte [wa_lkbots], 2
    mov bx, [wa_win]
    mov ax, 1
    call OSAPI_WM_TIMER
    ret

; wa_lknewid - a fresh nonzero identity (the clock and the generator)
wa_lknewid:
    call OSAPI_RAND
    mov [wa_lkid], ax
    call pit_now
    xor ax, dx
    or ax, 1
    mov [wa_lkid+2], ax
    ret

; wa_lkclose - the window closes: the port let go. CF = 0 (it may close)
wa_lkclose:
    push si
    call lk_disconnect
    pop si
    clc
    ret

; wa_lklinked - ZF = 0 when a peer is bound and heard within WL_QUIET
wa_lklinked:
    push ax
    mov ax, [wa_lkpeer]
    or ax, [wa_lkpeer+2]
    jz .no
    call OSAPI_GET_TICKS
    sub ax, [wa_lkpheard]
    cmp ax, WL_QUIET
    jae .no
    or al, 1                    ; (ZF = 0)
    pop ax
    ret
.no:
    xor al, al                  ; (ZF = 1)
    pop ax
    ret

; wa_lktimer - the window's tick (ONTIMER, SI = the window, the lock held)
wa_lktimer:
    push si
    push es                     ; (ES = KERNEL_SEG: the window's flags)
    mov bx, [wa_win]            ; again in a tick, first: a match started
    mov ax, 1                   ; from here returns to a live timer
    call OSAPI_WM_TIMER
    call lk_tick
    mov cx, 64
    call lk_poll
    call wa_lkhello
    pop es
    push es
    call wa_lkstart
    pop es
    push es
    call wa_lkdraw
    pop es
    pop si
    ret

; wa_lkhello - HELLO, every WL_BEAT ticks while the port is held
wa_lkhello:
    cmp byte [lk_blk+LK_OPEN], 0
    je .out
    call OSAPI_GET_TICKS
    mov bx, ax
    sub ax, [wa_lkbeat]
    cmp ax, WL_BEAT
    jb .out
    mov [wa_lkbeat], bx
    mov di, wa_lkfr
    push ds
    pop es
    mov al, WL_HELLO
    stosb
    mov ax, 'WA'
    stosw
    mov al, 1
    stosb
    mov si, wa_lkid
    movsw
    movsw
    mov si, wa_lkpeer
    movsw
    movsw
    mov al, [wa_lkst]
    stosb
    mov al, [wa_lkarena]
    stosb
    mov al, [wa_lkdiff]
    stosb
    mov al, [wa_lkbots]         ; (the host's: L2)
    stosb
    mov al, [wa_lktag]
    stosb
    xor al, al
    stosb
    mov si, wa_lkfr
    mov cl, 18
    call lk_send
.out:
    ret

; wa_lkhost - ZF = 0 when we are the host (the lower identity)
wa_lkhost:
    push ax
    mov ax, [wa_lkid+2]
    cmp ax, [wa_lkpeer+2]
    jne .c
    mov ax, [wa_lkid]
    cmp ax, [wa_lkpeer]
.c:
    jb .host
    xor al, al
    pop ax
    ret
.host:
    or al, 1
    pop ax
    ret

; wa_lkstart - START (plan 4.1): the host draws a tag once both are ready
; and goes in when the guest is in; the guest goes in on the host's tag
wa_lkstart:
    call wa_lklinked
    jz .out
    cmp byte [wa_lkst], 1
    jne .out
    cmp byte [wa_lkps], 1
    jb .out
    call wa_lkhost
    jz .guest
    cmp byte [wa_lktag], 0
    jne .tagged
    call OSAPI_RAND
    or al, al
    jnz .t
    inc ax
.t:
    mov [wa_lktag], al
    mov byte [wa_lkfull], 1
    ret
.tagged:
    cmp byte [wa_lkgo], 0       ; the guest's HELLO said 2 with our tag, or
    jne wa_lkplay               ; its SNAP came
    ret
.guest:
    mov al, [wa_lkptag]
    or al, al
    jz .out
    mov [wa_lktag], al
    jmp wa_lkplay
.out:
    ret

; wa_lkplay - into the match: the host's arena, our width
wa_lkplay:
    mov byte [wa_lkgo], 0
    mov byte [wa_lkst], 2
    mov word [wa_lkbeat], 0     ; (a HELLO saying 2, now)
    sub word [wa_lkbeat], WL_BEAT
    call wa_lkhello
    mov al, [wa_lkarena]
    mov [wa_arena], al
    mov al, [wa_lkdiff]
    mov [wa_diff], al
    mov al, [wa_lkbots]
    mov [wa_lknb], al
    call wa_loadarena
    jc .back
    mov byte [wa_lkon], 1
    mov ax, wa_main
    mov bx, [wa_win]
    xor cx, cx
    call OSAPI_FSX_RUN
.back:
    mov byte [wa_lkon], 0       ; back in the lobby, not ready
    mov byte [wa_lkst], 0
    mov byte [wa_lkgo], 0
    call wa_lkhost
    jz .g
    mov byte [wa_lktag], 0
.g:
    mov byte [wa_lkfull], 1
    ret

; wa_lkcmd - the Link menu: AL = Automatic, COM1, COM2, Disconnect, Ready,
; then No bots, One bot, Two bots (the host's)
wa_lkcmd:
    mov byte [wa_lkfull], 1
    cmp al, 5
    jb .link
    sub al, 5
    call wa_lkhost
    jz .hosts
    mov [wa_lkbots], al
    ret
.hosts:
    push ds
    pop es
    mov si, wa_t_host
    xor cx, cx
    call OSAPI_TOAST
    ret
.link:
    cmp al, 0
    jne .c1
    call lk_disconnect
    call lk_auto
    mov word [lk_blk+LK_MSG], lk_m_search
    ret
.c1:
    cmp al, 3
    je .off
    ja wa_lkready
    jmp lk_connect              ; (AL = 1 or 2: COM1, COM2)
.off:
    jmp lk_disconnect

; wa_lkready - Ready, toggled (the Link menu, Space or Enter)
wa_lkready:
    xor byte [wa_lkst], 1
    and byte [wa_lkst], 1
    jnz .r
    call wa_lkhost
    jz .r
    mov byte [wa_lktag], 0      ; (the host not ready: no START pending)
.r:
    mov word [wa_lkbeat], 0     ; said at once
    sub word [wa_lkbeat], WL_BEAT
    ret

; wa_lkkey - ONKEY: Space and Enter toggle Ready
wa_lkkey:
    cmp al, ' '
    je .r
    cmp al, 13
    jne .out
.r:
    push si
    call wa_lkready
    pop si
.out:
    ret

; wa_lktake - a valid frame (lk_take, os88cable.inc): AL = its type, SI it
lk_take:
    cmp al, WL_HELLO
    je wa_lkhtake
    cmp byte [wa_lkon], 0       ; (the match's frames: the match's, or in the
    jne wa_lkmtake              ; window the guest's first SNAP - it is in)
    cmp al, WL_SNAP
    jne .out
    mov al, [si+1]
    cmp al, [wa_lktag]
    jne .out
    or al, al
    jz .out
    mov byte [wa_lkgo], 1
    call OSAPI_GET_TICKS
    mov [wa_lkpheard], ax
.out:
    ret

; wa_lkhtake - a HELLO (plan 4.1)
wa_lkhtake:
    cmp word [si+1], 'WA'
    jne .out
    cmp byte [si+3], 1
    jne .out
    mov ax, [si+4]
    or ax, [si+6]
    jz .out
    mov ax, [si+4]              ; our own identity: drawn again
    cmp ax, [wa_lkid]
    jne .other
    mov ax, [si+6]
    cmp ax, [wa_lkid+2]
    jne .other
    jmp wa_lknewid
.other:
    mov ax, [wa_lkpeer]
    or ax, [wa_lkpeer+2]
    jz .bind
    mov ax, [si+4]
    cmp ax, [wa_lkpeer]
    jne .third
    mov ax, [si+6]
    cmp ax, [wa_lkpeer+2]
    je .peer
.third:
    call wa_lklinked            ; another identity: taken only once the
    jnz .out                    ; bound one has gone quiet
.bind:
    mov ax, [si+4]
    mov [wa_lkpeer], ax
    mov ax, [si+6]
    mov [wa_lkpeer+2], ax
    mov byte [wa_lkps], 0
    mov byte [wa_lkptag], 0
    mov byte [wa_lkfull], 1
.peer:
    mov byte [lk_blk+LK_HEARD], 1
    call OSAPI_GET_TICKS
    mov [wa_lkpheard], ax
    mov al, [si+12]
    mov [wa_lkps], al
    cmp byte [wa_lkon], 0       ; in the match: a HELLO saying it is not
    je .lobby                   ; is the peer gone
    cmp al, 2
    jae .out
    mov byte [wa_lkgone], 1
    mov byte [wa_ovd], 1
    ret
.lobby:
    call wa_lkhost
    jnz .host
    mov al, [si+13]             ; a guest takes the host's arena, its
    mov [wa_lkarena], al        ; difficulty and its tag
    mov al, [si+14]
    mov [wa_lkdiff], al
    mov al, [si+15]
    cmp al, 2
    jbe .nb
    mov al, 2
.nb:
    mov [wa_lkbots], al
    mov al, [si+16]
    mov [wa_lkptag], al
    ret
.host:
    cmp byte [si+12], 2         ; the guest in, with our tag: we go too
    jne .out
    mov al, [si+16]
    or al, al
    jz .out
    cmp al, [wa_lktag]
    jne .out
    mov byte [wa_lkgo], 1
.out:
    ret

; wa_lkdraw - the window's rows that changed since they were drawn (all of
; them after a paint or a mode change): the link's line, then the help or
; the lobby. Under the lock (ONTIMER)
wa_lkdraw:
    mov bx, [wa_win]
    test word [es:bx+W_FLAGS], 2
    jz wa_lkrows.out
    call OSAPI_WM_CLIP_SET
    jc wa_lkrows.out
    ; (falls through)
; wa_lkrows - every row, drawn if it changed or [wa_lkfull]: the paint's
; body (wa_lkpaint) and the tick's (wa_lkdraw)
wa_lkrows:
    push bp
    xor bp, bp                  ; BP = the row
.row:
    call wa_lkline              ; the row's text into wa_lkln
    mov ax, bp                  ; its cached copy
    mov cl, WL_COLS
    mul cl
    add ax, wa_lkcache
    mov di, ax
    cmp byte [wa_lkfull], 0
    jne .draw
    mov si, wa_lkln
    mov cx, WL_COLS
    push di
    push ds
    pop es
    repe cmpsb
    pop di
    je .next
.draw:
    mov si, wa_lkln
    mov cx, WL_COLS
    push ds
    pop es
    rep movsb
    call wa_lkput
.next:
    inc bp
    cmp bp, WL_ROWS
    jb .row
    mov byte [wa_lkfull], 0
    pop bp
.out:
    ret

; wa_lkpaint - PAINT: every row (the kernel has set the clip)
wa_lkpaint:
    mov byte [wa_lkfull], 1
    jmp wa_lkrows

; wa_lkput - BP = a row: wa_lkln drawn there, ground and glyphs in one pass
wa_lkput:
    mov bx, [wa_win]
    call OSAPI_WM_CONTENT       ; AX = left, DX = top
    mov cx, ax
    add cx, 8
    mov ax, bp
    mov bl, 10
    mul bl
    add ax, 6
    add dx, ax
    mov si, wa_lkln
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    ret

; wa_lkline - BP = a row: its text, WL_COLS characters and a 0, in wa_lkln
wa_lkline:
    push ds
    pop es
    mov di, wa_lkln
    mov al, ' '
    mov cx, WL_COLS
    rep stosb
    mov byte [di], 0
    mov di, wa_lkln
    or bp, bp
    jnz .body
    mov si, [lk_blk+LK_MSG]     ; row 0: what the link is doing
    call wa_lklinked
    jz .copy
    mov si, wa_lt_on1
    call wa_lkcpy
    mov al, [lk_blk+LK_PORT]
    add al, '0'
    stosb
    ret
.body:
    cmp bp, 1
    je .out
    call wa_lklinked
    jnz .lobby
    mov si, wa_help             ; the help: row 2 is its first line
    mov cx, bp
    sub cx, 2
    jcxz .copy
.skip:
    lodsb
    or al, al
    jnz .skip
    cmp byte [si], 0
    je .out
    loop .skip
.copy:
    jmp wa_lkcpy
.out:
    ret
.lobby:
    mov bx, bp
    sub bx, 2
    cmp bx, 7
    jae .out
    shl bx, 1
    jmp [wa_lkrow+bx]

wa_lkrow:   dw .you, .them, .out, .arena, .width, .out, .hint
.out:
    ret
.you:
    mov si, wa_lt_you
    call wa_lkcpy
    mov di, wa_lkln+12
    mov al, [wa_lkst]
    call wa_lkstate
    call wa_lkhost
    jz .o2
    mov di, wa_lkln+28
    mov si, wa_lt_host
    jmp wa_lkcpy
.o2:
    ret
.them:
    call wa_lkpnm               ; its name, then its state
    mov di, wa_lkln+12
    mov al, [wa_lkps]
    call wa_lkstate
    call wa_lkhost
    jnz .o3
    mov di, wa_lkln+28
    mov si, wa_lt_host
    jmp wa_lkcpy
.o3:
    ret
.arena:
    mov si, wa_lt_arena
    call wa_lkcpy
    mov bl, [wa_lkarena]
    xor bh, bh
    shl bx, 1
    mov si, [wa_lt_anames+bx]
    call wa_lkcpy
    mov di, wa_lkln+17
    mov si, wa_lt_bots
    call wa_lkcpy
    mov al, [wa_lkbots]
    add al, '0'
    stosb
    mov al, ','
    stosb
    mov al, ' '
    stosb
    mov bl, [wa_lkdiff]
    xor bh, bh
    shl bx, 1
    mov si, [wa_lt_dnames+bx]
    jmp wa_lkcpy
.width:
    mov si, wa_lt_w40
    cmp byte [wa_want], 0
    jne .w
    mov si, wa_lt_w80
.w:
    jmp wa_lkcpy
.hint:
    mov si, wa_lt_hhost
    call wa_lkhost
    jnz .h
    mov si, wa_lt_hguest
.h:
    jmp wa_lkcpy

; wa_lkstate - AL = a lobby state: its word at DI
wa_lkstate:
    xor ah, ah
    shl ax, 1
    mov bx, ax
    mov si, [wa_lt_st+bx]
    ; (falls through)
; wa_lkcpy - SI = a 0-terminated string: copied to DI, short of wa_lkln's
; end (DI past it)
wa_lkcpy:
    lodsb
    or al, al
    jz .out
    cmp di, wa_lkln + WL_COLS
    jae .out
    stosb
    jmp wa_lkcpy
.out:
    ret

; wa_lkpnm - the peer's name at DI (a robot's, its identity's last four hex
; digits after it, so two machines' names differ)
wa_lkpnm:
    call wa_lkpname
    mov si, wa_lkpnam
    mov cx, 8
.c:
    lodsb
    cmp al, ' '
    je .hex
    stosb
    loop .c
.hex:
    mov al, '-'
    stosb
    mov dx, [wa_lkpeer]
    mov cx, 4
.d:
    push cx
    mov cl, 4
    rol dx, cl
    pop cx
    mov al, dl
    and al, 15
    add al, '0'
    cmp al, '9'
    jbe .s
    add al, 'A' - '9' - 1
.s:
    stosb
    loop .d
    ret

; wa_lkpname - the peer's robot name, 8 characters, into wa_lkpnam
wa_lkpname:
    push di
    mov al, [wa_lkpeer]
    and ax, WA_NNAME - 1
    mov cl, 3
    shl ax, cl
    add ax, wa_names
    mov si, ax
    mov di, wa_lkpnam
    push ds
    pop es
    mov cx, 8
    rep movsb
    pop di
    ret

wa_lt_on1:      db 'Linked on COM', 0
wa_lt_you:      db 'YOU', 0
wa_lt_host:     db '(host)', 0
wa_lt_st:       dw wa_lt_s0, wa_lt_s1, wa_lt_s2
wa_lt_s0:       db 'not ready', 0
wa_lt_s1:       db 'READY', 0
wa_lt_s2:       db 'in the arena', 0
wa_lt_arena:    db 'Arena: ', 0
wa_lt_anames:   dw wa_lt_a0, wa_lt_a1, wa_lt_a2
wa_lt_a0:       db 'full', 0
wa_lt_a1:       db 'open', 0
wa_lt_a2:       db 'heights', 0
wa_lt_bots:     db 'Bots: ', 0
wa_lt_dnames:   dw wa_lt_d0, wa_lt_d1, wa_lt_d2
wa_lt_d0:       db 'easy', 0
wa_lt_d1:       db 'normal', 0
wa_lt_d2:       db 'hard', 0
wa_lt_w40:      db 'You play 40 columns (Game menu)', 0
wa_lt_w80:      db 'You play 80 columns (Game menu)', 0
wa_lt_hhost:    db 'Space: ready. Game menu: the arena.', 0
wa_lt_hguest:   db 'Space: ready. The host picks the arena.', 0

; --- the match ---------------------------------------------------------------

; wa_lkmstart - the match begins (wa_main, after wa_reset): our side's
; counters, the other player not yet seen, the bots parked, the pickups -
; the host's rolls, which the guest waits for
wa_lkmstart:
    xor ax, ax
    mov [wa_lkdin], ax
    mov [wa_lkdout], ax
    mov [wa_lkin], al
    mov byte [wa_lkseq], 1
    mov [wa_lkqn], al
    mov [wa_lkqs], al
    mov [wa_lkrseen], al
    mov [wa_lklost], al
    mov [wa_lkgone], al
    mov [wa_lkpkk], al
    mov [wa_lkattr], al
    mov [wa_lkbd], ax
    mov [wa_lkbdin], ax
    push ds
    pop es
    mov di, wa_lkd              ; nobody dead yet
    mov cx, 8
    rep stosw
    mov di, wa_lkr              ; nothing heard of anyone
    mov cx, WR_SZ * WA_NFIG / 2
    rep stosw
    mov al, 1                   ; who we are on the wire: the host 0, the
    call wa_lkhost              ; guest 1
    jz .me
    xor al, al
.me:
    mov [wa_lkme], al
    mov ax, [wa_tkn]
    mov [wa_lkheard], ax
    mov [wa_lkqt], ax
    mov bx, [wa_lift]           ; (the guest's lift where it starts, until
    or bx, bx                   ; the host's SNAPs say)
    js .nolift
    mov ax, [wa_bxh+bx]
    mov [wa_lklift], ax
.nolift:
    mov di, wa_figs             ; off the board until the other machine
    mov cx, WA_NFIG             ; says where they are: the other player, and
.f:                             ; on the guest the host's bots (the host's
    call wa_lkremote            ; own run from their spawns)
    je .park
    call wa_lkabsent
    jne .nf
.park:
    mov byte [di+F_DEAD], 250
    mov byte [di+F_NFR], 0
.nf:
    add di, F_SIZE
    loop .f
    call wa_lkpname
    mov cx, [wa_npk]
    jcxz .out
    xor bx, bx
    call wa_lkhost
    jz .guest
.roll:                          ; the host: each pickup's kind, said
    push bx
    push cx
    mov ax, bx
    mov cl, WA_PKSZ
    mul cl
    add ax, wa_pks
    mov si, ax
    mov ah, [si+6]
    mov al, bl
    call wa_lkqroll
    pop cx
    pop bx
    inc bx
    loop .roll
.out:
    ret
.guest:                         ; the guest: none until the host says
    mov byte [wa_pkon], 0
.g:
    mov word [wa_pkt+bx], 1
    inc bx
    inc bx
    loop .g
    ret

; wa_lkqroll - AL = a pickup, AH = its kind: ROLL queued (the host's)
wa_lkqroll:
    mov byte [wa_lkev], EV_ROLL
    mov [wa_lkev+1], ax
    jmp wa_lkqueue

; wa_lkqtake - AL = a pickup taken here: TAKE queued
wa_lkqtake:
    mov byte [wa_lkev], EV_TAKE
    mov [wa_lkev+1], al
    jmp wa_lkqueue

; wa_lkqfire - SI, DI = a projectile's records, just launched here: FIRE
; queued, as it left (its owner, place and velocity). Keeps all but AX
wa_lkqfire:
    cmp byte [wa_lkon], 0
    je wa_lkqueue.out
    mov byte [wa_lkev], EV_FIRE
    mov al, [di+RG_OWN]
    call wa_lkcanon
    mov ah, al
    shl ah, 1
    shl ah, 1
    shl ah, 1
    shl ah, 1
    mov al, [si+WA_RKTYP]
    or al, ah
    mov [wa_lkev+1], al
    mov ax, [si+WA_RKX]
    mov [wa_lkev+2], ax
    mov ax, [si+WA_RKY]
    mov [wa_lkev+4], ax
    mov ax, [si+WA_RKZ]
    mov [wa_lkev+6], ax
    mov ax, [di+RG_VX]
    mov [wa_lkev+8], ax
    mov ax, [di+RG_VY]
    mov [wa_lkev+10], ax
    mov ax, [di+RG_VZ]
    mov [wa_lkev+12], ax
    ; (falls through)

; wa_lkqueue - wa_lkev's nine bytes onto the events waiting for their ack
; (a full queue drops it: eight waiting is a link already lost). Keeps all
; but AX
wa_lkqueue:
    cmp byte [wa_lkon], 0
    je .out
    cmp byte [wa_lkqn], WL_EVQ
    jae .out
    push cx
    push si
    push di
    push es
    mov al, [wa_lkqn]
    mov cl, WL_EVSZ
    mul cl
    add ax, wa_lkq
    mov di, ax
    mov si, wa_lkev
    push ds
    pop es
    mov cx, WL_EVSZ
    rep movsb
    inc byte [wa_lkqn]
    pop es
    pop di
    pop si
    pop cx
.out:
    ret

; wa_lktick - a sim tick of the match: what the cable brought, then what
; we send - a SNAP every third tick, else the next event; SCORE every 30
wa_lktick:
    cmp byte [wa_lkon], 0
    je .out
    push di
    push si
    push bp
    mov al, [wa_frame]          ; the cable once a frame, on its first tick
    cmp al, [wa_lkpf]           ; (a Read block costs ~2,800 cycles even
    je .polled                  ; when nothing came: LINK-PLAN 11.2)
    mov [wa_lkpf], al
    mov cx, 64
    call lk_poll
.polled:
    mov ax, [wa_tkn]
    sub ax, [wa_lkheard]
    cmp ax, WL_QUIET
    jb .heard
    cmp byte [wa_lklost], 0
    jne .heard
    mov byte [wa_lklost], 1     ; LINK LOST (plan 4.3)
    mov byte [wa_ovd], 1
.heard:
    mov ax, [wa_tkn]
    xor dx, dx
    mov cx, 30
    div cx
    push dx
    or dx, dx
    jnz .nsc
    call wa_lkscore
.nsc:
    pop ax
    mov cl, WL_PSNAP
    cmp byte [wa_lknb], 0
    je .p
    mov cl, WL_PSNAPB
.p:
    div cl                      ; (AH = the tick's place in the period)
    or ah, ah
    jnz .ev
    call wa_lksnap
    jmp .done
.ev:
    call wa_lkevent
.done:
    pop bp
    pop si
    pop di
.out:
    ret

; wa_lksnap - SNAP (plan 4.2)
wa_lksnap:
    push ds
    pop es
    mov di, wa_lkfr
    mov al, WL_SNAP
    mov ah, [wa_lktag]
    stosw
    mov ax, [wa_px]
    stosw
    mov ax, [wa_py]
    stosw
    mov ax, [wa_jz]             ; the feet: the floor and the jump, / 2
    mov al, ah
    cbw
    add ax, [wa_feet]
    jns .z
    xor ax, ax
.z:
    shr ax, 1
    stosb
    mov al, [wa_ang+1]
    stosb
    call wa_lkhost              ; byte 8: the host's lift's height / 2, the
    jz .pitch                   ; guest's pitch / 4 (nobody reads a pitch)
    xor al, al
    mov bx, [wa_lift]
    or bx, bx
    js .b8
    mov ax, [wa_bxh+bx]
    shr ax, 1
    jmp .b8
.pitch:
    mov ax, [wa_pitch]
    sar ax, 1
    sar ax, 1
.b8:
    stosb
    xor al, al                  ; the flags
    cmp byte [wa_pdead], 0
    jne .f1
    or al, 1
.f1:
    cmp byte [wa_mflash], 0     ; firing: its flash, or a shot since the
    jne .f1s                    ; last SNAP (the guest's, for the host's
    cmp byte [wa_lkfired], 0    ; bots to hear)
    je .f2
.f1s:
    or al, 2
    mov byte [wa_lkfired], 0
.f2:
    cmp word [wa_sh], 0
    je .f3
    or al, 4
.f3:
    mov ah, [wa_wpn]
    and ah, 7
    shl ah, 1
    shl ah, 1
    shl ah, 1
    or al, ah
    mov ah, [wa_lkpkk]
    ror ah, 1
    ror ah, 1
    and ah, 0xC0
    or al, ah
    stosb
    mov ax, [wa_lkdout]
    stosw
    mov al, [wa_lkin]
    stosb
    mov cl, 13
    cmp byte [wa_lknb], 0
    je .send
    call wa_lkhost
    jz .guest
    mov byte [wa_lkfr], WL_HSNAP ; THE HOST'S, with bots: who dealt the
    mov al, [wa_lkattr]         ; damage, and where each bot is (x / 8,
    stosb                       ; y / 8, its facing / 4, its flags)
    mov bx, wa_figs + F_SIZE
.bot:
    xor ax, ax
    xor dx, dx
    cmp bx, wa_figs + F_SIZE * 2
    ja .none
    push di
    mov di, bx
    call wa_lkabsent
    pop di
    je .none
    mov ax, [bx+F_X]
    mov dx, [bx+F_Y]
    push cx
    mov cl, 3
    shr ax, cl
    shr dx, cl
    pop cx
    mov ah, dl
    stosw
    mov ax, [bx+F_ANG]
    shr ax, 1
    shr ax, 1
    xor ah, ah                  ; (the flags: alive, its cue, its shield,
    cmp byte [bx+F_DEAD], 0     ; its death's kind)
    jne .d
    or ah, 1
.d:
    test byte [bx+F_FLASH], 0x80
    jz .c
    or ah, 2
.c:
    cmp byte [bx+F_SH], 0
    je .s
    or ah, 4
.s:
    mov dl, [bx+F_KILL]
    ror dl, 1
    ror dl, 1
    and dl, 0xC0
    or ah, dl
    stosw
    jmp .nb
.none:
    stosw
    stosw
.nb:
    add bx, F_SIZE
    cmp bx, wa_figs + F_SIZE * 2
    jbe .bot
    mov cl, 22
    jmp .send
.guest:
    mov byte [wa_lkfr], WL_GSNAP ; THE GUEST'S, with bots: its hits on them
    mov ax, [wa_lkbd]
    stosw
    mov cl, 15
.send:
    mov si, wa_lkfr
    jmp lk_send

; wa_lkscore - SCORE: our row of deaths by cause (LINK-PLAN 11.2); the
; host with bots their two rows too (BSCORE)
wa_lkscore:
    mov al, WL_SCORE
    mov ah, [wa_lktag]
    mov [wa_lkfr], ax
    mov bl, [wa_lkme]
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov ax, [wa_lkd+bx]
    mov [wa_lkfr+2], ax
    mov ax, [wa_lkd+bx+2]
    mov [wa_lkfr+4], ax
    mov si, wa_lkfr
    mov cl, 6
    call lk_send
    cmp byte [wa_lknb], 0
    je .out
    call wa_lkhost
    jz .out
    mov al, WL_BSCORE
    mov ah, [wa_lktag]
    mov [wa_lkfr], ax
    mov si, wa_lkd + 8
    mov di, wa_lkfr + 2
    push ds
    pop es
    mov cx, 4
    rep movsw
    mov si, wa_lkfr
    mov cl, 10
    jmp lk_send
.out:
    ret

; wa_lkevent - the next event not yet sent, or the oldest again once its
; ack is WL_RESEND ticks late
wa_lkevent:
    mov al, [wa_lkqn]
    or al, al
    jz .out
    mov ax, [wa_tkn]
    sub ax, [wa_lkqt]
    cmp ax, WL_RESEND
    jb .next
    mov byte [wa_lkqs], 0       ; late: all of them again
.next:
    mov al, [wa_lkqs]
    cmp al, [wa_lkqn]
    jae .out
    mov ah, [wa_lkqs]           ; its place and its sequence
    mov al, WL_EVSZ
    mul ah
    add ax, wa_lkq
    mov si, ax
    push ds
    pop es
    mov di, wa_lkfr
    mov al, WL_EVENT
    mov ah, [wa_lktag]
    stosw
    mov al, [wa_lkseq]
    add al, [wa_lkqs]
    stosb
    mov cx, WL_EVSZ
    rep movsb
    mov si, wa_lkfr
    mov cl, 3 + WL_EVSZ
    call lk_send
    jc .out
    inc byte [wa_lkqs]
    mov ax, [wa_tkn]
    mov [wa_lkqt], ax
.out:
    ret

; wa_lkmtake - a frame in the match: AL = its type, SI it
wa_lkmtake:
    mov ah, [si+1]
    cmp ah, [wa_lktag]
    jne .out
    mov dx, [wa_tkn]
    mov [wa_lkheard], dx
    cmp byte [wa_lklost], 0
    je .live
    mov byte [wa_lklost], 0     ; the link is back
    mov byte [wa_ovd], 1
.live:
    cmp al, WL_EVENT
    je wa_lkevin
    cmp al, WL_SCORE
    je .score
    cmp al, WL_BSCORE
    je .bscore
    jmp wa_lksnapin             ; (2, 5, 6: a SNAP)
.score:
    mov bl, [wa_lkme]           ; the other's row: its deaths by cause
    xor bl, 1
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov ax, [si+2]
    mov [wa_lkd+bx], ax
    mov ax, [si+4]
    mov [wa_lkd+bx+2], ax
    jmp wa_lkboard
.bscore:
    cmp byte [wa_lkme], 0       ; (the host's bots' rows: to the guest)
    je .out
    add si, 2
    mov di, wa_lkd + 8
    push ds
    pop es
    mov cx, 4
    rep movsw
    jmp wa_lkboard
.out:
    ret

; wa_lksnapin - the other player's SNAP: where it is, its ack, the damage
; it dealt us
wa_lksnapin:
    cmp byte [wa_lkgone], 0
    je .here
    mov byte [wa_lkgone], 0
    mov byte [wa_ovd], 1
.here:
    cmp byte [wa_lkrseen], 0
    jne .seen
    mov byte [wa_ovd], 1
.seen:
    mov byte [wa_lkrseen], 1
    mov ax, [si+2]
    mov [wa_lkr+WR_X], ax
    mov ax, [si+4]
    mov [wa_lkr+WR_Y], ax
    mov al, [si+6]
    xor ah, ah
    shl ax, 1
    mov [wa_lkr+WR_Z], ax
    mov al, [si+7]
    xor ah, ah
    shl ax, 1
    shl ax, 1
    mov [wa_lkr+WR_A], ax
    mov al, [si+9]
    mov [wa_lkr+WR_F], al
    call wa_lkhost              ; the guest: the host's lift; the host: the
    jnz .heard                  ; guest's shot, heard by its bots
    mov al, [si+8]
    xor ah, ah
    shl ax, 1
    mov [wa_lklift], ax
    jmp .lifted
.heard:
    test byte [si+9], 2
    jz .lifted
    push dx
    mov ax, [si+2]
    mov bx, [si+4]
    mov dl, 1
    call wa_bhear_far
    pop dx
.lifted:
    mov byte [wa_lkhby], 1      ; who hurt us: the other player, or (the
    cmp byte [si], WL_HSNAP     ; host's, with bots) whom it names
    jne .who
    mov al, [si+13]
    call wa_lkcanon             ; (the same swap, both ways)
    mov [wa_lkhby], al
    call wa_lkbots_in
.who:
    cmp byte [si], WL_GSNAP
    jne .ack0
    call wa_lkbhits
.ack0:
    mov al, [si+12]             ; THE ACK: every event up to its sequence
.ack:                           ; leaves the queue
    cmp byte [wa_lkqn], 0
    je .acked
    mov ah, al
    sub ah, [wa_lkseq]
    cmp ah, 128
    jae .acked
    push si
    push ax
    push ds
    pop es
    mov si, wa_lkq + WL_EVSZ
    mov di, wa_lkq
    mov cx, (WL_EVQ - 1) * WL_EVSZ
    rep movsb
    pop ax
    pop si
    dec byte [wa_lkqn]
    cmp byte [wa_lkqs], 0
    je .s
    dec byte [wa_lkqs]
.s:
    inc byte [wa_lkseq]
    jmp .ack
.acked:
    mov ax, [si+10]             ; THE DAMAGE: what the word grew by
    mov dx, ax
    sub ax, [wa_lkdin]
    mov [wa_lkdin], dx
    jz .out
    cmp ax, 1000
    ja .out                     ; (a word from before a restart: not ours)
    cmp byte [wa_pdead], 0
    jne .out
    mov bl, [wa_lkhby]          ; (whose: figure 0's, or a host's bot's)
    mov [wa_hurtby], bl
    mov bl, [wa_lkr+WR_F]       ; its kill: the shotgun's collapse, else the
    shr bl, 1                   ; fall
    shr bl, 1
    shr bl, 1
    and bl, 7
    cmp bl, 1
    mov bl, 0
    jne .kk
    inc bx
.kk:
    mov [wa_lkpkk], bl
    mov byte [wa_phit], 6
    mov cx, ax
.d:
    mov ax, cx
    cmp ax, 250
    jbe .one
    mov ax, 250
.one:
    sub cx, ax
    push cx
    call wa_phurt
    pop cx
    or cx, cx
    jnz .d
.out:
    ret

; wa_lkevin - an EVENT: applied once, in order
wa_lkevin:
    mov al, [wa_lkin]
    inc al
    cmp al, [si+2]
    jne .out
    mov [wa_lkin], al
    mov al, [si+3]
    cmp al, EV_FIRE
    je .fire
    cmp al, EV_TAKE
    je .take
    cmp al, EV_ROLL
    jne .out
    xor bx, bx                  ; ROLL: a pickup back, with its kind
    mov bl, [si+4]
    cmp bx, [wa_npk]
    jae .out
    mov al, bl
    mov cl, WA_PKSZ
    mul cl
    add ax, wa_pks
    mov di, ax
    mov al, [si+5]
    mov [di+6], al
    push es
    mov es, [wa_rds]
    mov [es:di+WA_RAT(wa_pks)-wa_pks+6], al
    pop es
    mov cl, bl
    mov al, 1
    shl al, cl
    or [wa_pkon], al
    shl bx, 1
    mov word [wa_pkt+bx], 0
    mov byte [wa_ovd], 1
.out:
    ret
.take:
    xor bx, bx                  ; TAKE: gone here too, its time started
    mov bl, [si+4]
    cmp bx, [wa_npk]
    jae .out
    mov al, bl
    mov cl, WA_PKSZ
    mul cl
    add ax, wa_pks
    mov di, ax
    mov cl, bl
    mov al, 1
    shl al, cl
    not al
    and [wa_pkon], al
    mov al, [di+6]
    xor ah, ah
    mov di, ax
    shl di, 1
    mov ax, [wa_pkresp+di]
    shl bx, 1
    mov [wa_pkt+bx], ax
    ret
.fire:
    jmp wa_lkspawn

; wa_lkspawn - SI = a FIRE: its projectile flies here too, its owner the
; figure that is its owner here (LINK-PLAN 3)
wa_lkspawn:
    mov bx, si
    mov si, wa_rks
    mov di, wa_rkg
.slot:
    cmp byte [si+WA_RKST], 0
    je .have
    add si, WA_RKSZ
    add di, WA_RKGSZ
    cmp si, wa_rks + WA_NRK*WA_RKSZ
    jb .slot
    ret                         ; (all four in flight: this one unseen)
.have:
    inc byte [wa_rkn]
    mov al, [bx+4]
    and al, 15
    mov [si+WA_RKTYP], al
    mov byte [si+WA_RKST], WA_RKFLY
    mov ax, [bx+5]
    mov [si+WA_RKX], ax
    mov ax, [bx+7]
    mov [si+WA_RKY], ax
    mov ax, [bx+9]
    mov [si+WA_RKZ], ax
    xor ax, ax
    mov [di+RG_XF], ax
    mov [di+RG_YF], ax
    mov [di+RG_ZF], ax
    mov ax, [bx+11]
    mov [di+RG_VX], ax
    mov ax, [bx+13]
    mov [di+RG_VY], ax
    mov ax, [bx+15]
    mov [di+RG_VZ], ax
    mov al, [bx+4]
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    call wa_lkcanon
    mov [di+RG_OWN], al
    mov word [di+RG_LIFE], WA_RKLIFE
    mov ax, 150
    cmp byte [si+WA_RKTYP], 0
    je .tone
    mov word [di+RG_LIFE], WA_GFUSE
    mov ax, 300
.tone:
    mov cx, 3
    mov dl, 0x40
    call OSAPI_SND_TONE
    ret

; wa_lkfig - DI = a figure the other machine owns (the other player, or
; on the guest the host's bots), SI its record: drawn where the SNAPs say -
; halfway there each tick - running while it moves, its cue while it
; fires, its death by its kind, gone 3 s after it, back where it respawned
wa_lkfig:
    mov al, [di+F_FLASH]        ; (its cue and its hurt flash run down)
    or al, al
    jz .fl
    dec ax
    cmp al, 0x80
    jne .fs
    xor al, al
.fs:
    mov [di+F_FLASH], al
.fl:
    cmp byte [wa_lkrseen], 0
    je .hide
    cmp byte [wa_lkgone], 0
    jne .hide
    test byte [si+WR_F], 1
    jz .dead
    cmp byte [di+F_DEAD], 0
    je .alive
    mov ax, [si+WR_X]           ; back: where it respawned, whole
    mov [di+F_X], ax
    mov ax, [si+WR_Y]
    mov [di+F_Y], ax
    mov byte [di+F_DEAD], 0
    mov byte [di+F_HP], WA_FHP
    mov byte [di+F_FLASH], 0
    mov word [di+F_PZ], 0
    call wa_runanim
.alive:
    mov ax, [si+WR_X]
    sub ax, [di+F_X]
    mov bx, ax                  ; BX nonzero: it is moving
    call wa_lkhalf
    add [di+F_X], ax
    mov ax, [si+WR_Y]
    sub ax, [di+F_Y]
    or bx, ax
    call wa_lkhalf
    add [di+F_Y], ax
    mov ax, [si+WR_Z]
    cmp ax, -1
    je .floor
    mov [di+F_Z], ax
    cmp di, wa_figs             ; (the other player on the host's lift: a
    jne .zd                     ; rider)
    call wa_lkhost
    jz .zd
    call wa_lkonlift
    jmp .zd
.floor:
    call wa_figz                ; (a bot: on the floor where it stands)
.zd:
    mov ax, [si+WR_A]
    mov [di+F_ANG], ax
    xor al, al                  ; its shield: the yellow chest
    test byte [si+WR_F], 4
    jz .sh
    mov al, WA_FHP
.sh:
    mov [di+F_SH], al
    test byte [si+WR_F], 2      ; firing: its cue
    jz .nc
    cmp byte [di+F_FLASH], 0
    jne .nc
    mov byte [di+F_FLASH], 0x83
.nc:
    cmp byte [di+F_ANIM], WA_A_FLINCH
    je .an
    or bx, bx
    jz .still
    cmp byte [di+F_ANIM], WA_A_RUN
    je .an
    call wa_runanim
.an:
    jmp wa_figtick.anim
.still:
    ret                         ; (standing: its pose held)
.dead:
    cmp byte [di+F_DEAD], 0
    jne .lying
    mov ah, [si+WR_F]           ; down: its death by the kind it says
    rol ah, 1
    rol ah, 1
    and ah, 3
    call wa_dieanim
    jmp wa_figtick.anim
.lying:
    cmp byte [di+F_DEAD], 2     ; (held there until it is alive again)
    jb .lie
    dec byte [di+F_DEAD]
.lie:
    cmp byte [di+F_DEAD], WA_BDEADT - WA_BBODYT
    ja .body
    mov byte [di+F_NFR], 0
.body:
    cmp byte [di+F_NFR], 0
    jne .an
    ret
.hide:
    mov byte [di+F_DEAD], 250
    mov byte [di+F_NFR], 0
    ret

; wa_lkremote - DI = a figure: ZF = 1 when, in a linked match, the other
; machine owns it - the other player always; on the guest, every figure.
; Keeps every register
wa_lkremote:
    cmp byte [wa_lkon], 0
    je .no
    cmp di, wa_figs
    je .out
    jmp wa_lkhost               ; (the host: ZF = 0, its bots are its own)
.no:
    or sp, sp
.out:
    ret

; wa_lkabsent - DI = a figure: ZF = 1 when it is a bot the match has none
; of (beyond the host's count). Keeps every register
wa_lkabsent:
    cmp di, wa_figs
    je .here
    push ax
    call wa_bidx                ; AL = its index, 1 or 2
    cmp al, [wa_lknb]
    pop ax
    jbe .here
    cmp al, al                  ; (ZF = 1)
    ret
.here:
    or sp, sp
    ret

; wa_lkcanon - AL = a figure's code here (0 us, 1 figure 0, 2 and 3 the
; bots): its code on the wire (0 the host's player, 1 the guest's, 2 and 3
; the bots) - and back, the same swap. Keeps all but AL
wa_lkcanon:
    cmp byte [wa_lkme], 0
    je .out
    cmp al, 2
    jae .out
    xor al, 1
.out:
    ret

; wa_lkdeath - our player died, by [wa_hurtby]: its count in our row
wa_lkdeath:
    cmp byte [wa_lkon], 0
    je wa_lkboard.out
    push ax
    push bx
    mov al, [wa_hurtby]
    call wa_lkcanon
    mov bl, [wa_lkme]
    jmp wa_lkcount
; wa_lkbdeath - DI = one of the host's bots, killed by [wa_killer]: its row
wa_lkbdeath:
    cmp byte [wa_lkon], 0
    je wa_lkboard.out
    push ax
    push bx
    call wa_bidx
    inc ax                      ; (its code: 2 or 3)
    mov bl, al
    mov al, [wa_killer]
wa_lkcount:                     ; BL = the dead one, AL the cause (codes on
    xor bh, bh                  ; the wire): D[BL][AL] + 1, the board again
    shl bx, 1
    shl bx, 1
    add bl, al
    inc byte [wa_lkd+bx]
    pop bx
    pop ax
    ; (falls through)
; wa_lkboard - the scoreboard from the deaths by cause: a death to each,
; a kill to its cause, a kill less for a suicide (AI-PLAN 5's rule). Keeps
; every register
wa_lkboard:
    push ax
    push bx
    push cx
    push si
    xor ax, ax
    mov [wa_kills], ax
    mov [wa_kills+2], ax
    mov [wa_deaths], ax
    mov [wa_deaths+2], ax
    mov si, wa_lkd
    xor bx, bx                  ; BL = the dead one, BH = the cause
.v:
    xor bh, bh
.k:
    lodsb
    or al, al
    jz .n
    mov cl, al                  ; CL = how many
    push bx
    mov al, bl                  ; a death to the dead one, here
    call wa_lkcanon
    xor ah, ah
    mov bx, ax
    add [wa_deaths+bx], cl
    pop bx
    push bx
    mov al, bh
    call wa_lkcanon
    xor ah, ah
    cmp bl, bh
    mov bx, ax
    je .self
    add [wa_kills+bx], cl
    jmp .kd
.self:
    sub [wa_kills+bx], cl       ; (a suicide: a kill less, not below 0)
    jnc .kd
    mov byte [wa_kills+bx], 0
.kd:
    pop bx
.n:
    inc bh
    cmp bh, 4
    jb .k
    inc bl
    cmp bl, 4
    jb .v
    mov byte [wa_ovd], 1
    pop si
    pop cx
    pop bx
    pop ax
.out:
    ret

; wa_lkbots_in - SI = the host's SNAP: its bots' records (the guest)
wa_lkbots_in:
    push si
    push di
    push cx
    add si, 14
    mov di, wa_lkr + WR_SZ
    mov cx, 2
.b:
    lodsb                       ; x / 8, y / 8: their middle
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, 4
    mov [di+WR_X], ax
    lodsb
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, 4
    mov [di+WR_Y], ax
    mov word [di+WR_Z], -1      ; (on the floor)
    lodsb
    xor ah, ah
    shl ax, 1
    shl ax, 1
    mov [di+WR_A], ax
    lodsb
    mov [di+WR_F], al
    add di, WR_SZ
    loop .b
    pop cx
    pop di
    pop si
    ret

; wa_lkbhits - SI = the guest's SNAP: its hits on the host's bots, what
; each byte grew by, as wa_hurt by figure 0 (the host)
wa_lkbhits:
    push si
    push di
    xor bx, bx
.b:
    mov al, [si+13+bx]
    mov ah, al
    sub al, [wa_lkbdin+bx]
    mov [wa_lkbdin+bx], ah
    jz .n
    cmp al, 128
    jae .n
    mov di, bx                  ; DI = the bot: figure 1 + BX
    inc di
    push ax
    mov ax, di
    mov ah, F_SIZE
    mul ah
    add ax, wa_figs
    mov di, ax
    pop ax
    call wa_lkabsent
    je .n
    mov byte [wa_killer], 1     ; (the guest: figure 0 here)
    mov ah, [wa_lkr+WR_F]       ; its kill by the guest's weapon
    shr ah, 1
    shr ah, 1
    shr ah, 1
    and ah, 7
    cmp ah, 1
    mov ah, 0
    jne .k
    inc ah
.k:
    push bx
    push si
    call wa_hurt
    pop si
    pop bx
.n:
    inc bx
    cmp bx, 2
    jb .b
    pop di
    pop si
    ret

; wa_bhear_far - wa_bheara, which the host's bots hear by (a near call
; across the file)
wa_bhear_far:
    jmp wa_bheara

; wa_lkonlift - DI = figure 0 on the host: one of the lift's riders when it
; stands on top of it (wa_lbn, which wa_ltick counts) - so the lift rises
; for the guest as it does for anyone here. Keeps every register
wa_lkonlift:
    push ax
    push bx
    mov bx, [wa_lift]
    or bx, bx
    js .out
    mov ax, [di+F_Z]            ; on its top: 40 units' slack, as its
    add ax, 40                  ; height comes a SNAP late while it rises
    cmp ax, [wa_bxh+bx]
    jl .out
    shl bx, 1                   ; (its box: x 8)
    mov ax, [di+F_X]
    cmp ax, [wa_boxes+bx]
    jl .out
    cmp ax, [wa_boxes+bx+4]
    jg .out
    mov ax, [di+F_Y]
    cmp ax, [wa_boxes+bx+2]
    jl .out
    cmp ax, [wa_boxes+bx+6]
    jg .out
    inc byte [wa_lbn]
.out:
    pop bx
    pop ax
    ret

; wa_lkhalf - AX = a distance: half of it, the whole within 2 (so it ends)
wa_lkhalf:
    cmp ax, 2
    jg .h
    cmp ax, -2
    jge .out
.h:
    sar ax, 1
.out:
    ret

; wa_lkpkhold - BX = a pickup x 2 whose time runs: ZF = 1 when a linked
; guest holds it at 1 until the host's ROLL. Keeps all but the flags
wa_lkpkhold:
    cmp byte [wa_lkon], 0
    je .no
    cmp word [wa_pkt+bx], 1
    jne .out
    call wa_lkhost
    jnz .out
    cmp al, al
.out:
    ret
.no:
    or sp, sp                   ; (ZF = 0)
    ret

; wa_lkpkback - BX = a pickup x 2, SI its record, just back: the host's
; ROLL. Keeps every register
wa_lkpkback:
    cmp byte [wa_lkon], 0
    je .out
    call wa_lkhost
    jz .out
    push ax
    mov ax, bx
    shr ax, 1
    mov ah, [si+6]
    call wa_lkqroll
    pop ax
.out:
    ret

; wa_lkpktaken - BX = a pickup x 2 we just took: TAKE. Keeps every register
wa_lkpktaken:
    push ax
    mov ax, bx
    shr ax, 1
    call wa_lkqtake
    pop ax
    ret

; wa_lkmsg - the overlay's line in a match: WAITING, LINK LOST, OPPONENT
; LEFT - or none
wa_lkmsg:
    cmp byte [wa_lkon], 0
    je .out
    mov si, wa_lm_gone
    cmp byte [wa_lkgone], 0
    jne .put
    mov si, wa_lm_lost
    cmp byte [wa_lklost], 0
    jne .put
    mov si, wa_lm_wait
    cmp byte [wa_lkrseen], 0
    jne .out
.put:
    mov dx, 11
    mov bx, [wa_w]
    sub bx, 20
    shr bx, 1
    mov cx, 20
    mov ah, 0x0E
    call wa_ovatt
.c:
    lodsb
    call wa_ovput
    loop .c
.out:
    ret
wa_lm_gone: db '   OPPONENT LEFT    '
wa_lm_lost: db '     LINK LOST      '
wa_lm_wait: db 'WAITING FOR OPPONENT'
