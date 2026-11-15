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
WL_PEERS    equ 8               ; the window, on UDP: the other peers we know
WL_PEERN    equ 2               ;   (two: the third is the one told)
LK_NTYPES   equ 9
lk_lens:    db 0, 28, 15, 8, 19, 24, 17, 12, 24 ; each type's body, its CRC too
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
EV_FLAG     equ 4               ; (the host's) a CTF flag: its team, state,
                                ; carrier's slot, both teams' captures, the
                                ; win, x, y, z (CTF-PLAN 7: wactf.inc)
WL_PSNAP    equ 3               ; sim ticks a SNAP, no bots: 10 Hz
WL_PSNAPB   equ 5               ; ...with bots: 6 Hz (LINK-PLAN 2, 11.2)
WR_X        equ 0               ; a remote figure's record (wa_lkr, ten bytes
WR_Y        equ 2               ;   a figure): where its machine says it is,
WR_Z        equ 4               ;   its height (-1: the floor's), facing,
WR_A        equ 6               ;   its flags (SNAP byte 9's)
WR_F        equ 8
WR_SZ       equ 10
WL_NPEER    equ 3               ; peers at most: four players (NET-PLAN 1)
WP_ID       equ 0               ; A PEER'S RECORD (wa_pt; wa_pc the loaded
WP_ADDR     equ 4               ;   one): its identity, its address (a
WP_PS       equ 10              ;   station's, or IP and port), its lobby
WP_PHEARD   equ 11              ;   state and when heard there, the host's
WP_PTAG     equ 13              ;   tag as it said it; in the match: lost,
WP_LOST     equ 14              ;   gone, seen yet, when last heard, the
WP_GONE     equ 15              ;   damage words in and out, the last event
WP_RSEEN    equ 16              ;   applied from it, its place in our event
WP_HEARD    equ 17              ;   queue and when sent, who dealt our word's
WP_DIN      equ 19              ;   last damage, its hits on the host's bots
WP_DOUT     equ 21              ;   as last seen, its ack of our queue, its
WP_IN       equ 23              ;   slot
WP_QS       equ 24
WP_QT       equ 25
WP_ATTR     equ 27
WP_BDIN     equ 28
WP_ACK      equ 30
WP_SLOT     equ 31
WP_NB       equ 32              ; (the lobby: how many peers it has bound,
                                ; as its HELLO says - its byte 17)
WP_NICK     equ 34              ; its nick, as its HELLO says (bytes 18..25:
                                ; space-padded, blank none)
WP_SZ       equ 42
WL_ROWS     equ 6               ; the window's rows of text: what the link is
                                ; doing, then the players (wasetup.inc's
                                ; controls between, WS_LOBY)
WL_COLS     equ 40

%include "os88cable.inc"
RW_ETYPE    equ 0x88B5          ; IEEE 802's local experimental EtherType
%include "os88raw.inc"          ; ...and over the card (LINK-PLAN 12)
UD_PORT     equ 30088           ; WireArena's UDP port (NET-PLAN 3)
%include "os88udp.inc"          ; ...or on UDP, by broadcast and by address

; wa_lkup - ZF = 0 when a carrier is up: the cable's lease, the card's raw
; claim, or the UDP port. Keeps every register
wa_lkup:
    push ax
    mov al, [lk_blk+LK_OPEN]
    cmp byte [wa_lknet], 1
    jb .c
    mov al, [rw_blk+RW_ON]
    je .c
    mov al, [ud_blk+UD_ON]
.c:
    or al, al
    pop ax
    ret

; wa_lksend - SI = a body, CL = its length without its CRC: on whichever
; carrier is up - framed into the cable, or sealed and sent on the card. A
; HELLO goes to the peer once bound, else to everyone - or, joining by
; address, to that address (NET-PLAN 6); the rest to the peer
wa_lksend:
    cmp byte [wa_lknet], 0
    jne .card
    jmp lk_send
.card:
    call lk_seal
    mov di, wa_lkpmac
    cmp byte [si], WL_HELLO
    jne .to
    mov ax, [wa_lkpeer]
    or ax, [wa_lkpeer+2]
    or ax, [wa_lkpmac]
    or ax, [wa_lkpmac+2]
    jnz .to                     ; (bound, or a PEERS named it: to it)
    mov di, wa_saddr6
    cmp byte [wa_splay], 3      ; (by address)
    je .to
    mov di, rw_blk+RW_BCAST
    cmp byte [wa_lknet], 1
    je .to
    mov di, ud_blk+UD_BCAST
.to:
    cmp byte [wa_lknet], 1
    jne .udp
    jmp rw_send
.udp:
    jmp ud_send

; wa_lkpoll - CX = at most this many bytes (the cable): what came, framed
; and handed to lk_take - on the card, up to 8 frames or datagrams
wa_lkpoll:
    cmp byte [wa_lknet], 0
    jne .card
    jmp lk_poll
.card:
    mov cx, 8
    cmp byte [wa_lknet], 1
    jne .udp
    jmp rw_poll
.udp:
    jmp ud_poll

; rw_take, ud_take - a body off the card (os88raw.inc, os88udp.inc): SI, CL
; its length, BX its source - which a HELLO's binding keeps - then checked
; as the cable's are
rw_take:
ud_take:
    push si
    push di
    mov si, bx
    mov di, wa_lkfrom
    push es
    push ds
    pop es
    movsw
    movsw
    movsw
    pop es
    pop di
    pop si
    jmp lk_frame

; wa_lknetgo - Play: local network (AL = 2) or internet (3): the cable let
; go; the card is the window's tick's to open (wa_lknettry), as UDP waits
; for an address
wa_lknetgo:
    push ax
    call lk_disconnect
    call wa_lkcable
    pop ax
    mov [wa_lkwant], al
    mov word [wa_lktries], 0
    mov word [lk_blk+LK_MSG], wa_lm_addr
    ret

; wa_lknettry - the window's tick, a network wanted and not up: UDP, once
; the stack has an address; on the local network, raw frames if no address
; comes in ~5 s (no DHCP, no static address: NET-PLAN 1)
wa_lknettry:
    cmp byte [wa_lkwant], 0
    je .out
    call ud_open
    jc .not
    mov byte [wa_lknet], 2
    mov byte [wa_lkwant], 0
    mov word [lk_blk+LK_MSG], wa_lm_udp
.out:
    ret
.not:
    cmp ax, NETE_NOLINK         ; REFUSED (no card, no driver): said once,
    jne .stop                   ; not asked again a tick - each ask loads
    inc word [wa_lktries]       ; the driver off the disk again
    cmp byte [wa_lkwant], 3     ; (by address: UDP or nothing)
    je .out
    cmp word [wa_lktries], 90
    jb .out
    mov byte [wa_lkwant], 0
    call rw_open
    jc .none
    mov byte [wa_lknet], 1
    mov word [lk_blk+LK_MSG], wa_lm_neth
    ret
.stop:
    cmp byte [wa_lkwant], 3     ; (local: raw frames may still be had)
    mov byte [wa_lkwant], 0
    jne .raw
.none:
    mov word [lk_blk+LK_MSG], wa_lm_nonet
    ret
.raw:
    call rw_open
    jc .none
    mov byte [wa_lknet], 1
    mov word [lk_blk+LK_MSG], wa_lm_neth
    ret

; wa_pptr - AL = a peer's index: BX = its record in the table. Keeps all
; but BX
wa_pptr:
    push ax
    push dx
    xor ah, ah
    mov dl, WP_SZ
    mul dl
    add ax, wa_pt
    mov bx, ax
    pop dx
    pop ax
    ret

; wa_pload - AL = a peer's index: its record into wa_pc. wa_pstore - back.
; Keep every register
wa_pload:
    push cx
    push si
    push di
    push es
    push bx
    mov [wa_lkp], al
    mov byte [wa_lkld], 1
    call wa_pptr
    mov si, bx
    mov di, wa_pc
.cp:
    push ds
    pop es
    mov cx, WP_SZ / 2
    cld
    rep movsw
    pop bx
    pop es
    pop di
    pop si
    pop cx
    ret
wa_pstore:
    push cx
    push si
    push di
    push es
    push bx
    push ax
    mov byte [wa_lkld], 0
    mov al, [wa_lkp]
    call wa_pptr
    pop ax
    mov di, bx
    mov si, wa_pc
    jmp wa_pload.cp

; wa_pfind - AL = a frame's type: the peer it came from (wa_lkfrom on the
; card; on the cable, the one there is), AL = its index, CF = 0. One not in
; the table yet takes a free record (its address set, the rest zero) - by
; a HELLO in the window, never in a match (a stranger's would hold the
; event queue: nobody would ack); CF = 1 when it may not, or none is free.
; Keeps all but AX
wa_pfind:
    push bx
    push cx
    push si
    push di
    mov ah, al
    xor al, al
    cmp byte [wa_lknet], 0
    jne .card
    cmp byte [wa_lkpn], 0       ; (the cable: one peer, record 0)
    jne .ok
    jmp .new
.card:
    mov cl, [wa_lkpn]
.f:
    cmp al, cl
    jae .new
    call wa_pptr
    lea si, [bx+WP_ADDR]
    mov di, wa_lkfrom
    push cx
    mov cx, 3
    push es
    push ds
    pop es
    repe cmpsw
    pop es
    pop cx
    je .ok
    inc al
    jmp .f
.new:
    cmp ah, WL_HELLO
    jne .full
    cmp byte [wa_lkon], 0
    jne .full
    cmp byte [wa_lkpn], WL_NPEER
    jae .full
    mov al, [wa_lkpn]
    inc byte [wa_lkpn]
    call wa_pptr
    push ax
    push es
    push ds
    pop es
    mov di, bx
    xor ax, ax
    mov cx, WP_SZ / 2
    rep stosw
    lea di, [bx+WP_ADDR]
    mov si, wa_lkfrom
    movsw
    movsw
    movsw
    pop es
    pop ax
.ok:
    clc
    jmp .out
.full:
    stc
.out:
    pop di
    pop si
    pop cx
    pop bx
    ret

; wa_lkcable - back to the cable (or nothing): the card given back
wa_lkcable:
    call rw_close
    call ud_close
    mov byte [wa_lknet], 0
    mov byte [wa_lkwant], 0
    ret

; --- the window -------------------------------------------------------------

; wa_lkinit - the window is open: an identity, Automatic, the timer
wa_lkinit:
    call wa_lknewid
    call lk_crcinit             ; (the CRC's table: both carriers' - it was
    mov byte [wa_lkfull], 1     ; built only when a serial port opened)
    mov byte [wa_lkarena], 2    ; (the heights, two normal bots: the host
    mov byte [wa_lkdiff], 1     ; may change all three)
    mov byte [wa_lkbots], WA_NFIG ; (alone: all three; linked, two)
    mov word [lk_blk+LK_MSG], wa_ms_setup ; (no carrier until Play says so:
    call wa_sinit               ; the match setup, NET-PLAN 2)
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
    call rw_close
    call ud_close
    call lk_disconnect
    pop si
    clc
    ret

; wa_lklinked - ZF = 0 when a peer is bound and heard within WL_QUIET
wa_lklinked:
    push ax
    push bx
    push cx
    push dx
    call OSAPI_GET_TICKS
    mov dx, ax
    xor cx, cx
.p:
    cmp cl, [wa_lkpn]
    jae .no
    mov al, cl
    call wa_prec
    inc cx
    mov ax, [bx+WP_ID]
    or ax, [bx+WP_ID+2]
    jz .p
    mov ax, dx
    sub ax, [bx+WP_PHEARD]
    cmp ax, WL_QUIET
    jae .p
    or al, 1                    ; (ZF = 0)
    jmp .out
.no:
    xor al, al                  ; (ZF = 1)
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wa_prec - AL = a peer's index: BX = its record - wa_pc if it is the one
; loaded, else the table's. Keeps all but BX
wa_prec:
    cmp byte [wa_lkld], 0
    je wa_pptr
    cmp al, [wa_lkp]
    jne wa_pptr
    mov bx, wa_pc
    ret

; wa_pmin - DX:AX = the lowest identity of the bound peers (FFFF:FFFF none)
wa_pmin:
    push bx
    push cx
    mov ax, 0xFFFF
    mov dx, ax
    xor cx, cx
.p:
    cmp cl, [wa_lkpn]
    jae .out
    push ax
    mov al, cl
    call wa_prec
    pop ax
    inc cx
    cmp word [bx+WP_ID], 0
    jne .b
    cmp word [bx+WP_ID+2], 0
    je .p
.b:
    cmp dx, [bx+WP_ID+2]
    jb .p
    ja .take
    cmp ax, [bx+WP_ID]
    jbe .p
.take:
    mov ax, [bx+WP_ID]
    mov dx, [bx+WP_ID+2]
    jmp .p
.out:
    pop cx
    pop bx
    ret

; wa_lktimer - the window's tick (ONTIMER, SI = the window, the lock held)
wa_lktimer:
    push si
    push es                     ; (ES = KERNEL_SEG: the window's flags)
    mov bx, [wa_win]            ; again in a tick, first: a match started
    mov ax, 1                   ; from here returns to a live timer
    call OSAPI_WM_TIMER
    cmp byte [wa_lknet], 0      ; (the cable's search: not on the card)
    jne .card
    call lk_tick
.card:
    call wa_lknettry            ; (a network wanted: opened when it can be)
    mov cx, 64
    call wa_lkpoll
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

; wa_lkhello - HELLO, every WL_BEAT ticks while a carrier is up: to each
; peer bound, and to everyone (or the typed address) while there is room
; for another (NET-PLAN 6)
wa_lkhello:
    call wa_lkup
    jz .out
    call OSAPI_GET_TICKS
    mov bx, ax
    sub ax, [wa_lkbeat]
    cmp ax, WL_BEAT
    jb .out
    mov [wa_lkbeat], bx
    call wa_pdrop
    xor cx, cx
.p:
    cmp cl, [wa_lkpn]
    jae .anyone
    mov al, cl
    push cx
    call wa_pload
    mov ax, [wa_lkpeer]         ; (bound: to it, and whom else we know;
    or ax, [wa_lkpeer+2]        ; a record a PEERS named, its address and
    jnz .bound                  ; no identity yet: to it, to be bound; one
    mov ax, [wa_lkpmac]         ; with neither waits for the HELLO to
    or ax, [wa_lkpmac+2]        ; everyone)
    jz .skip
    call wa_lkhello1
    jmp .skip
.bound:
    call wa_lkhello1
    call wa_lkpeersend
.skip:
    call wa_pstore
    pop cx
    inc cx
    jmp .p
.anyone:
    cmp byte [wa_lkpn], WL_NPEER ; (full: nobody new)
    jae .out
    cmp byte [wa_lknet], 0      ; (the cable: one peer, once bound)
    jne .room
    cmp byte [wa_lkpn], 0
    jne .out
.room:
    push ds                     ; the HELLO to everyone: bound to nobody
    pop es
    mov di, wa_pc
    xor ax, ax
    mov cx, WP_SZ / 2
    rep stosw
    jmp wa_lkhello1
.out:
    ret

; wa_lkpeersend - to the loaded peer (bound), on UDP: PEERS, the others
; bound that we know, an identity and an address each (NET-PLAN 6.1) - so
; three that each typed one address find each other
wa_lkpeersend:
    cmp byte [wa_lknet], 2
    jne .out
    cmp byte [wa_lkpn], 2
    jb .out
    push ds
    pop es
    mov di, wa_lkfr
    mov ax, WL_PEERS            ; (its count, 0 so far)
    stosw
    push di
    xor ax, ax
    mov cx, WL_PEERN * 5
    rep stosw
    pop di
    xor cx, cx
.p:
    cmp cl, [wa_lkpn]
    jae .send
    cmp cl, [wa_lkp]            ; (not the one told)
    je .n
    mov al, cl
    call wa_pptr
    call wa_lkbound
    jz .n
    cmp byte [wa_lkfr+1], WL_PEERN
    jae .send
    inc byte [wa_lkfr+1]
    mov si, bx                  ; WP_ID, then WP_ADDR: ten bytes
    push cx
    mov cx, 5
    rep movsw
    pop cx
.n:
    inc cx
    jmp .p
.send:
    cmp byte [wa_lkfr+1], 0
    je .out
    mov si, wa_lkfr
    mov cl, 2 + WL_PEERN * 10
    jmp wa_lksend
.out:
    ret

; wa_lkpeersin - SI = a PEERS from a bound peer, in the window: each one
; it names that we have no record of (by identity or address), a record -
; its address, no identity, heard now - for our HELLO to bind (above)
wa_lkpeersin:
    mov ax, [wa_lkpeer]
    or ax, [wa_lkpeer+2]
    jz .out
    mov cl, [si+1]
    cmp cl, WL_PEERN
    ja .out
    add si, 2
.e:
    or cl, cl
    jz .out
    dec cx
    push cx
    call wa_lkpeer1
    pop cx
    add si, 10
    jmp .e
.out:
    ret

; wa_lkpeer1 - SI = a PEERS entry (identity, address): a record for it
; unless it is us or one we have. Keeps SI
wa_lkpeer1:
    mov ax, [si]
    cmp ax, [wa_lkid]
    jne .other
    mov ax, [si+2]
    cmp ax, [wa_lkid+2]
    je .out
.other:
    xor cx, cx
.p:
    cmp cl, [wa_lkpn]
    jae .new
    mov al, cl
    call wa_prec
    inc cx
    mov ax, [si]                ; the same identity
    cmp ax, [bx+WP_ID]
    jne .addr
    mov ax, [si+2]
    cmp ax, [bx+WP_ID+2]
    je .out
.addr:
    push si                     ; the same address
    push cx
    lea di, [bx+WP_ADDR]
    add si, 4
    mov cx, 3
    push es
    push ds
    pop es
    repe cmpsw
    pop es
    pop cx
    pop si
    je .out
    jmp .p
.new:
    cmp byte [wa_lkpn], WL_NPEER
    jae .out
    mov al, [wa_lkpn]
    inc byte [wa_lkpn]
    call wa_pptr
    push si
    push ds
    pop es
    mov di, bx
    xor ax, ax
    mov cx, WP_SZ / 2
    rep stosw
    lea di, [bx+WP_ADDR]
    add si, 4
    movsw
    movsw
    movsw
    pop si
    call OSAPI_GET_TICKS
    mov [bx+WP_PHEARD], ax
.out:
    ret

; wa_pdrop - in the window, every peer quiet WL_QUIET ticks out of the
; table (NET-PLAN 4): one that closed its window or lost its carrier would
; otherwise hold the host's start - it waits for every peer bound to be
; READY. Its HELLO again finds it a record. BX = the ticks now
wa_pdrop:
    cmp byte [wa_lkon], 0
    jne .out
    xor cx, cx
.p:
    cmp cl, [wa_lkpn]
    jae .out
    mov al, cl
    push bx
    call wa_pptr
    mov si, bx
    pop bx
    mov ax, bx
    sub ax, [si+WP_PHEARD]
    cmp ax, WL_QUIET
    jae .drop
    inc cx
    jmp .p
.drop:
    push cx                     ; the records after it down one
    mov di, si
    add si, WP_SZ
    mov al, [wa_lkpn]
    sub al, cl
    dec ax
    mov ah, WP_SZ / 2
    mul ah
    mov cx, ax
    push es
    push ds
    pop es
    cld
    rep movsw
    pop es
    pop cx
    dec byte [wa_lkpn]
    mov byte [wa_lkfull], 1
    jmp .p
.out:
    ret

; wa_lkhello1 - one HELLO, wa_pc's peer named in it (0: to everyone)
wa_lkhello1:
    mov di, wa_lkfr
    push ds
    pop es
    mov al, WL_HELLO
    stosb
    mov ax, 'WA'
    stosw
    mov al, 2                   ; (v2: the nick at its end)
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
    mov al, [wa_lkgmode]        ; (the difficulty, and in its high nibble
    mov cl, 4                   ; the mode - the host's, CTF-PLAN 7)
    shl al, cl
    or al, [wa_lkdiff]
    stosb
    mov al, [wa_lkbots]         ; (the host's: L2)
    stosb
    mov al, [wa_lktag]
    stosb
    call wa_lkbcount            ; how many peers we have bound: the start
    stosb                       ; waits until every one says what we do
    mov si, wa_nick             ; our nick
    mov cx, WA_NNAME / 2
    rep movsw
    mov si, wa_lkfr
    mov cl, 26
    jmp wa_lksend

; wa_lkbcount - AL = how many peers are bound. Keeps all but AX
wa_lkbcount:
    push bx
    push cx
    xor cx, cx
    xor ah, ah
.p:
    cmp cl, [wa_lkpn]
    jae .out
    mov al, cl
    call wa_prec
    inc cx
    call wa_lkbound
    jz .p
    inc ah
    jmp .p
.out:
    mov al, ah
    pop cx
    pop bx
    ret

; wa_lkhost - ZF = 0 when we are the host: the lowest identity of us and
; every peer bound (NET-PLAN 4: slot 0) - in a match, slot 0 as the match
; began. Keeps every register
wa_lkhost:
    push ax
    cmp byte [wa_lkon], 0
    je .lobby
    mov al, [wa_lkme]           ; (AL = FFh: slot 0)
    cmp al, 1
    sbb al, al
    test al, al
    pop ax
    ret
.lobby:
    push dx
    call wa_pmin
    cmp dx, [wa_lkid+2]
    jne .c
    cmp ax, [wa_lkid]
.c:
    jbe .guest                  ; (a peer at or below us: not the host)
    or al, 1
    pop dx
    pop ax
    ret
.guest:
    xor al, al
    pop dx
    pop ax
    ret

; wa_pishost - ZF = 0 when the loaded peer is the host: the lowest
; identity of us all. Keeps every register
wa_pishost:
    push ax
    push dx
    call wa_pmin
    cmp ax, [wa_lkpeer]
    jne .no
    cmp dx, [wa_lkpeer+2]
    jne .no
    cmp dx, [wa_lkid+2]         ; (and below us)
    jne .c
    cmp ax, [wa_lkid]
.c:
    jae .no
    or al, 1
    pop dx
    pop ax
    ret
.no:
    xor al, al
    pop dx
    pop ax
    ret

; wa_lkstart - START (LINK-PLAN 4.1, every peer now): the host draws a tag
; once every peer is ready and goes in when every peer is in with it; a
; guest goes in on the host's tag
wa_lkstart:
    call wa_lklinked
    jz .out
    cmp byte [wa_lkst], 1
    jne .out
    call wa_lkbcount            ; (AL = how many we have bound: every peer
    mov dh, al                  ; must say the same - one that has not
    call wa_lkhost              ; bound them all would start a match
    jz .guest                   ; without a player, NET-PLAN 9.5)
    xor cx, cx                  ; THE HOST: every peer bound ready (1), and
    mov dl, 1                   ; DL = 1 while every one is in (2) with our
.p:                             ; tag
    cmp cl, [wa_lkpn]
    jae .all
    mov al, cl
    call wa_pptr
    inc cx
    mov ax, [bx+WP_ID]
    or ax, [bx+WP_ID+2]
    jz .p
    cmp byte [bx+WP_PS], 1
    jb .out
    cmp byte [bx+WP_PS], 2
    jne .ready
    mov al, [bx+WP_PTAG]
    cmp al, [wa_lktag]
    je .p
.ready:
    cmp [bx+WP_NB], dh          ; (ready, and seeing everyone we see)
    jne .out
.notin:
    xor dl, dl
    jmp .p
.all:
    cmp byte [wa_lktag], 0
    jne .tagged
    call OSAPI_RAND
    or al, al
    jnz .t
    inc ax
.t:
    mov [wa_lktag], al
    ret
.tagged:
    or dl, dl
    jnz wa_lkplay
    ret
.guest:
    call wa_phost               ; the host's record: its tag, and it ready
    jc .out                     ; - and seeing as many as we do
    cmp byte [bx+WP_PS], 1
    jb .out
    cmp [bx+WP_NB], dh
    jne .out
    mov al, [bx+WP_PTAG]
    or al, al
    jz .out
    mov [wa_lktag], al
    jmp wa_lkplay
.out:
    ret

; wa_phost - BX = the host's record (the bound peer of the lowest
; identity, below ours), CF = 0; CF = 1 when we are the host. AL = its
; index. Keeps all but AX, BX
wa_phost:
    push cx
    push dx
    call wa_pmin
    mov cx, ax
    xor al, al
.p:
    cmp al, [wa_lkpn]
    jae .none
    call wa_prec
    cmp cx, [bx+WP_ID]
    jne .n
    cmp dx, [bx+WP_ID+2]
    je .got
.n:
    inc al
    jmp .p
.got:
    cmp dx, [wa_lkid+2]
    jne .c
    cmp cx, [wa_lkid]
.c:
    jae .none
    clc
    jmp .out
.none:
    stc
.out:
    pop dx
    pop cx
    ret

; wa_lkplay - into the match: the host's arena, our width
wa_lkplay:
    mov byte [wa_lkst], 2
    mov word [wa_lkbeat], 0     ; (HELLOs saying 2, now)
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
    call wa_lkhost
    jz .g
    mov byte [wa_lktag], 0
.g:
    mov byte [wa_lkfull], 1
    ret

; wa_lkcmd - the Link menu: AL = Automatic, COM1, COM2, Disconnect, Ready,
; then No bots, One bot, Two bots (the host's)
wa_lkcmd:
    cmp al, 8
    jne .nnet
    mov byte [wa_splay], 2
    mov al, 2
    jmp wa_lknetgo
.nnet:
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
    cmp al, 4
    je .rdy
    push ax
    call wa_lkcable             ; (Automatic, COM1, COM2, Disconnect: the
    pop ax                      ; card's claim given back)
    cmp al, 0
    jne .c1
    call lk_disconnect
    call lk_auto
    mov word [lk_blk+LK_MSG], lk_m_search
    ret
.c1:
    cmp al, 3
    je .off
    jmp lk_connect              ; (AL = 1 or 2: COM1, COM2)
.off:
    jmp lk_disconnect
.rdy:
    jmp wa_lkready

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
    push si
    call wa_skey                ; (the setup's keys, wasetup.inc)
    pop si
    jc .out
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
    push ax                     ; WHO SENT IT: its record (wa_pfind - a new
    call wa_pfind               ; address takes a free one), loaded for the
    mov ah, al                  ; frame and stored after it (NET-PLAN 4)
    pop bx
    jc .out
    mov al, ah
    call wa_pload
    mov al, bl
    call wa_ltake1
    call wa_pstore
.out:
    ret
wa_ltake1:
    cmp al, WL_HELLO
    je wa_lkhtake
    cmp byte [wa_lkon], 0       ; (the match's frames: the match's, or in the
    jne wa_lkmtake              ; window a peer's first SNAP - it is in)
    push ax                     ; (a bound peer's frame, whatever it is: it
    mov ax, [wa_lkpeer]         ; is heard - one still in a match sends no
    or ax, [wa_lkpeer+2]        ; HELLO, and the lobby drops the quiet)
    jz .unb
    call OSAPI_GET_TICKS
    mov [wa_lkpheard], ax
.unb:
    pop ax
    cmp al, WL_PEERS
    je wa_lkpeersin
    cmp al, WL_SNAP
    jb .out
    cmp al, WL_GSNAP
    ja .out
    cmp al, WL_SCORE
    je .out
    cmp al, WL_EVENT
    je .out
    mov al, [si+1]
    cmp al, [wa_lktag]
    jne .out
    or al, al
    jz .out
    mov byte [wa_lkps], 2       ; (as good as its HELLO saying 2 with our
    mov [wa_lkptag], al         ; tag)
    call OSAPI_GET_TICKS
    mov [wa_lkpheard], ax
.out:
    ret

; wa_lkhtake - a HELLO (plan 4.1)
wa_lkhtake:
    cmp word [si+1], 'WA'
    jne .out
    cmp byte [si+3], 2
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
    call OSAPI_GET_TICKS        ; another identity at this address: taken
    sub ax, [wa_lkpheard]       ; only once the bound one has gone quiet
    cmp ax, WL_QUIET            ; (the other machine restarted WireArena)
    jb .out
.bind:
    mov ax, [si+4]
    mov [wa_lkpeer], ax
    mov ax, [si+6]
    mov [wa_lkpeer+2], ax
    mov byte [wa_lkps], 0
    mov byte [wa_lkptag], 0
.peer:
    push si                     ; (on the card: where it is, to answer it)
    push di
    push es
    push ds
    pop es
    mov si, wa_lkfrom
    mov di, wa_lkpmac
    movsw
    movsw
    movsw
    pop es
    pop di
    pop si
    mov byte [lk_blk+LK_HEARD], 1
    call OSAPI_GET_TICKS
    mov [wa_lkpheard], ax
    mov al, [si+16]             ; (its tag: the host's own, or a guest's
    mov [wa_lkptag], al         ; echo of it)
    mov al, [si+17]             ; (how many it has bound)
    mov [wa_pc+WP_NB], al
    push si                     ; its nick: a byte off the wire is a glyph
    push bx                     ; on the screen - a control one a blank
    push cx
    mov cx, WA_NNAME
    xor bx, bx
.nk:
    mov al, [si+18+bx]
    cmp al, ' '
    jb .nkb
    cmp al, 127
    jb .nkc
.nkb:
    mov al, ' '
.nkc:
    mov [wa_pc+WP_NICK+bx], al
    inc bx
    loop .nk
    pop cx
    pop bx
    pop si
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
    call wa_pishost             ; (from the host - the lowest identity of
    jz .out                     ; us all - its match's settings)
    mov al, [si+13]             ; a guest takes the host's arena, its
    mov [wa_lkarena], al        ; difficulty and its tag
    mov al, [si+14]
    mov ah, al
    and al, 0x0F
    mov [wa_lkdiff], al
    mov cl, 4
    shr ah, cl
    and ah, 1
    mov [wa_lkgmode], ah
    mov al, [si+15]
    cmp al, 2
    jbe .nb
    mov al, 2
.nb:
    mov [wa_lkbots], al
.out:
    ret

; wa_lkdraw - the window's rows that changed since they were drawn (all of
; them after a paint or a mode change): the link's line, then the help or
; the lobby. Under the lock (ONTIMER). THE CLIP IS TAKEN ONLY WHEN A ROW OR
; A BUTTON CHANGED: taken every tick, it blinked the pointer over the
; window three times a second with nothing to draw
wa_lkdraw:
    cmp byte [wa_abon], 0       ; (the About card up: nothing under it)
    jne wa_lkrows.out
    mov bx, [wa_win]
    test word [es:bx+W_FLAGS], 2
    jz wa_lkrows.out
    mov byte [wa_lkclip], 0
    ; (falls through)
; wa_lkrows - every row, drawn if it changed or [wa_lkfull]: the paint's
; body (wa_lkpaint) and the tick's (wa_lkdraw). A paint skips a blank row:
; the kernel filled the content white before it (SPEC.md 11)
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
    jne .full
    mov si, wa_lkln
    mov cx, WL_COLS
    push di
    push ds
    pop es
    repe cmpsb
    pop di
    je .next
    call wa_lkclipon
    jc .gone
    jmp .draw
.full:
    cmp byte [wa_lkown], 0      ; (our own repaint: every row)
    jne .draw
    mov si, wa_lkln             ; (a paint: a blank row is the kernel's
    mov cx, WL_COLS             ; white already)
.blank:
    lodsb
    cmp al, ' '
    loope .blank
    jne .draw
    mov si, wa_lkln
    mov cx, WL_COLS
    push ds
    pop es
    rep movsb
    jmp .next
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
    call wa_sdraw               ; (the setup's controls and buttons, where
    jc .gone                    ; they changed)
    call wa_sbtns
    mov byte [wa_lkfull], 0
    pop bp
    cmp byte [wa_abon], 0       ; (a paint with the card up: the card last,
    je .out                     ; over everything it is opaque about)
    mov bx, [wa_win]
    mov si, wa_ablines
    call os88ui_about_d
.out:
    ret
.gone:
    mov byte [wa_lkfull], 1     ; (no clip: all of it next tick)
    pop bp
    ret

; wa_lkclipon - the window's clip, once a tick, the first time something
; is drawn: CF = 1 refused (then nothing is drawn, and the next tick draws
; it all). Keeps all but the flags
wa_lkclipon:
    cmp byte [wa_lkclip], 0
    jne .ok
    push ax
    push bx
    push es
    mov bx, [wa_win]
    call OSAPI_WM_CLIP_SET
    pop es
    pop bx
    pop ax
    jc .no
    mov byte [wa_lkclip], 1
.ok:
    clc
    ret
.no:
    stc
    ret

; wa_lkpaint - PAINT: every row (the kernel has set the clip)
wa_lkpaint:
    mov byte [wa_lkfull], 1
    mov byte [wa_lkclip], 1
    jmp wa_lkrows

; wa_lkput - BP = a row: wa_lkln drawn there, ground and glyphs in one pass
wa_lkput:
    mov bx, [wa_win]
    call OSAPI_WM_CONTENT       ; AX = left, DX = top
    mov cx, ax
    add cx, 8
    mov ax, bp                  ; row 0 at 4, the players' from WS_LOBY
    or ax, ax
    jz .r0
    dec ax
    mov bl, 10
    mul bl
    add ax, WS_LOBY - 4
.r0:
    add ax, 4
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
    jnz .linked
    cmp si, wa_lm_udp           ; (on the network: where, for a host to
    jne .copy                   ; give out to players joining by address)
    call wa_lkcpy
    mov si, ud_blk+UD_IP
    jmp wa_lkip
.linked:
    cmp byte [wa_lknet], 0
    je .com
    mov si, wa_lt_onet
    cmp byte [wa_lknet], 1
    je .raw
    mov si, wa_lt_oudp          ; (the first peer bound: by address, the
    call wa_lkcpy               ; host)
    xor cx, cx
.fp:
    mov al, cl
    call wa_pptr
    inc cx
    call wa_lkbound
    jz .fp
    lea si, [bx+WP_ADDR]
    jmp wa_lkip
.raw:
    jmp wa_lkcpy
.com:
    mov si, wa_lt_on1
    call wa_lkcpy
    mov al, [lk_blk+LK_PORT]
    add al, '0'
    stosb
    ret
.body:
    call wa_lklinked            ; rows 1..: the players, once linked
    jnz .lobby
.out:
    ret
.copy:
    jmp wa_lkcpy
.lobby:
    mov dx, bp                  ; rows 1..: YOU, each peer bound, then the
    dec dx                      ; hint (NET-PLAN 4: three peers at most)
    jz wa_lkrow.you
    xor cx, cx
.p:
    cmp cl, [wa_lkpn]
    jae .nop
    mov al, cl
    call wa_pptr
    inc cx
    call wa_lkbound
    jz .p
    dec dx
    jnz .p
    dec cx                      ; the DX'th: its row, loaded and back
    mov al, cl
    call wa_pload
    call wa_lkrow.them
    jmp wa_pstore
.nop:
    dec dx                      ; (the row after the last: the hint)
    jz wa_lkrow.hint
    ret

wa_lkrow:
.out:
    ret
.you:
    mov si, wa_lt_you           ; (our nick, if there is one)
    cmp byte [wa_snick], 0
    je .yc
    mov si, wa_snick
.yc:
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
    call wa_pishost
    jz .o3
    mov di, wa_lkln+28
    mov si, wa_lt_host
    jmp wa_lkcpy
.o3:
    ret
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
    cmp byte [wa_pc+WP_NICK], ' ' ; (its own nick: no number after it)
    ja .out
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
.out:
    ret

; wa_lksbname - BX = a figure's code here (not ours): SI = its name on a
; linked board - a player's off its identity as wa_lkpname's, a bot's its
; own - CF = 0; CF = 1 a slot the match has nobody in. Keeps all but SI
wa_lksbname:
    push ax
    push bx
    push cx
    mov al, bl
    call wa_lkcanon             ; (its slot)
    mov ah, [wa_lknp]
    add ah, [wa_lknb]
    cmp al, ah
    jae .none
    cmp al, [wa_lknp]
    jae .bot
    call wa_slotpeer
    jc .none
    lea si, [bx+WP_NICK]        ; (its nick, if it sent one)
    cmp byte [si], ' '
    ja .got
    mov al, [bx+WP_ID]
    jmp .nm
.bot:
    mov al, [wa_bname-1+bx]
.nm:
    and ax, WA_NNAME - 1
    mov cl, 3
    shl ax, cl
    add ax, wa_names
    mov si, ax
.got:
    clc
    jmp .out
.none:
    stc
.out:
    pop cx
    pop bx
    pop ax
    ret

; wa_lkpname - the peer's robot name, 8 characters, into wa_lkpnam
wa_lkpname:
    push di
    mov si, wa_pc+WP_NICK       ; (its nick, if it sent one)
    cmp byte [si], ' '
    ja .cp
    mov al, [wa_lkpeer]
    and ax, WA_NNAME - 1
    mov cl, 3
    shl ax, cl
    add ax, wa_names
    mov si, ax
.cp:
    mov di, wa_lkpnam
    push ds
    pop es
    mov cx, 8
    rep movsb
    pop di
    ret

wa_lt_on1:      db 'Linked on COM', 0
wa_ms_setup:    db 'WireArena - match setup', 0
wa_lt_onet:     db 'Linked on Ethernet', 0
wa_lt_oudp:     db 'Linked with ', 0
wa_lm_addr:     db 'Asking the network for an address...', 0
wa_lm_udp:      db 'On the network at ', 0
wa_lm_neth:     db 'Looking on Ethernet...', 0
wa_lm_nonet:    db 'No Ethernet card (ETHER.DRV)', 0
wa_lt_you:      db 'YOU', 0
wa_lt_host:     db '(host)', 0
wa_lt_st:       dw wa_lt_s0, wa_lt_s1, wa_lt_s2
wa_lt_s0:       db 'not ready', 0
wa_lt_s1:       db 'READY', 0
wa_lt_s2:       db 'in the arena', 0
wa_lt_anames:   dw wa_lt_a0, wa_lt_a1, wa_lt_a2, wa_lt_a3, wa_lt_a4
wa_lt_a0:       db 'full', 0
wa_lt_a1:       db 'open', 0
wa_lt_a2:       db 'heights', 0
wa_lt_a3:       db 'hedge', 0
wa_lt_a4:       db 'twin', 0
wa_lt_dnames:   dw wa_lt_d0, wa_lt_d1, wa_lt_d2
wa_lt_d0:       db 'easy', 0
wa_lt_d1:       db 'normal', 0
wa_lt_d2:       db 'hard', 0
wa_lt_hhost:    db 'Ready on both, and the match starts.', 0
wa_lt_hguest:   db 'Ready, and the host starts the match.', 0

; --- the match ---------------------------------------------------------------

; wa_lkmstart - the match begins (wa_main, after wa_reset): our side's
; counters, the other player not yet seen, the bots parked, the pickups -
; the host's rolls, which the guest waits for
wa_lkmstart:
    call wa_lkslots             ; who is who (NET-PLAN 4)
    xor ax, ax
    mov byte [wa_lkseq], 1
    mov [wa_lkqn], al
    mov [wa_lkpkk], al
    mov [wa_lkbd], ax
    xor cx, cx                  ; every peer's side of the match, fresh
.pp:
    cmp cl, [wa_lkpn]
    jae .ppd
    mov al, cl
    call wa_pptr
    inc cx
    xor ax, ax
    mov [bx+WP_DIN], ax
    mov [bx+WP_DOUT], ax
    mov [bx+WP_BDIN], ax
    mov [bx+WP_IN], al
    mov [bx+WP_QS], al
    mov [bx+WP_RSEEN], al
    mov [bx+WP_LOST], al
    mov [bx+WP_GONE], al
    mov [bx+WP_ATTR], al
    mov [bx+WP_ACK], al
    mov ax, [wa_tkn]
    mov [bx+WP_HEARD], ax
    mov [bx+WP_QT], ax
    jmp .pp
.ppd:
    xor ax, ax
    push ds
    pop es
    mov di, wa_lkd              ; nobody dead yet
    mov cx, 8
    rep stosw
    mov di, wa_lkr              ; nothing heard of anyone
    mov cx, WR_SZ * WA_NFIG / 2
    rep stosw
    mov cx, [wa_nlift]          ; (the guest's lifts where they start,
    jcxz .nolift                ; until the host's SNAPs say)
    xor si, si
.lift:
    mov bx, [wa_lift+si]
    mov ax, [wa_bxh+bx]
    mov [wa_lklift+si], ax
    inc si
    inc si
    loop .lift
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

; wa_lkslots - the match's slots by identity, lowest first: ours
; (wa_lkme), each bound peer's (WP_SLOT), how many players (wa_lknp), and
; the bots the slots left room for (wa_lknb: four in all, NET-PLAN 1)
wa_lkslots:
    mov byte [wa_lknp], 1
    mov byte [wa_lkme], 0
    xor cx, cx
.p:
    cmp cl, [wa_lkpn]
    jae .ours
    mov al, cl
    call wa_pptr
    inc cx
    call wa_lkbound
    jz .p
    inc byte [wa_lknp]
    mov si, bx                  ; a peer below us: our slot one higher
    mov di, wa_lkid
    call wa_idlt
    jnc .p
    inc byte [wa_lkme]
    jmp .p
.ours:
    xor cx, cx                  ; each peer's: how many of us all are below
.q:                             ; it
    cmp cl, [wa_lkpn]
    jae .bots
    mov al, cl
    call wa_pptr
    inc cx
    call wa_lkbound
    jz .q
    xor dl, dl
    mov si, wa_lkid             ; (us)
    mov di, bx
    call wa_idlt
    adc dl, 0
    push cx
    xor cx, cx
.r:
    cmp cl, [wa_lkpn]
    jae .rd
    mov al, cl
    push bx
    call wa_pptr
    mov si, bx
    pop bx
    inc cx
    cmp si, bx
    je .r
    push bx
    mov bx, si
    call wa_lkbound
    pop bx
    jz .r
    mov di, bx
    call wa_idlt
    adc dl, 0
    jmp .r
.rd:
    pop cx
    mov [bx+WP_SLOT], dl
    jmp .q
.bots:
    mov al, 4                   ; the bots: what the host asked, at most
    sub al, [wa_lknp]           ; the slots left
    cmp al, [wa_lknb]
    jae .out
    mov [wa_lknb], al
.out:
    ret

; wa_lkbound - BX = a record: ZF = 0 when a peer is bound there
wa_lkbound:
    push ax
    mov ax, [bx+WP_ID]
    or ax, [bx+WP_ID+2]
    pop ax
    ret

; wa_idlt - SI, DI = two identities (4 bytes): CF = 1 when SI's is the
; lower. Keeps all but the flags
wa_idlt:
    push ax
    mov ax, [si+2]
    cmp ax, [di+2]
    jne .c
    mov ax, [si]
    cmp ax, [di]
.c:
    pop ax
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
    call wa_lkpoll
.polled:
    mov ax, [wa_tkn]            ; this tick's part: SCORE every 30th, and a
    xor dx, dx                  ; SNAP or the next event (bit 0 SCORE, bit
    mov cx, 30                  ; 1 SNAP)
    div cx
    xor bl, bl
    or dx, dx
    jnz .nsc
    inc bx
.nsc:
    mov ax, dx
    mov cl, WL_PSNAP
    cmp byte [wa_lknb], 0
    je .p
    mov cl, WL_PSNAPB
.p:
    div cl                      ; (AH = the tick's place in the period)
    or ah, ah
    jnz .ev
    or bl, 2
.ev:
    mov [wa_lkwhat], bl
    xor cx, cx                  ; THEN EACH PEER BOUND, its record loaded
.peer:                          ; (NET-PLAN 4): what it has heard of us
    cmp cl, [wa_lkpn]
    jae .all
    mov al, cl
    call wa_pptr
    inc cx
    call wa_lkbound
    jz .peer
    push cx
    dec cx
    mov al, cl
    call wa_pload
    mov ax, [wa_tkn]
    sub ax, [wa_lkheard]
    cmp ax, WL_QUIET
    jb .heard
    cmp byte [wa_lklost], 0
    jne .heard
    mov byte [wa_lklost], 1     ; LINK LOST (plan 4.3)
    mov byte [wa_ovd], 1
.heard:
    cmp byte [wa_lkgone], 0     ; (one that left: nothing more to say)
    jne .st
    test byte [wa_lkwhat], 1
    jz .nscore
    call wa_lkscore
.nscore:
    test byte [wa_lkwhat], 2
    jz .event
    call wa_lksnap
    jmp .st
.event:
    call wa_lkevent
.st:
    call wa_pstore
    pop cx
    jmp .peer
.all:
    test byte [wa_lkwhat], 2    ; the shot latch rides two SNAPs to every
    jz .done                    ; peer (LINK-PLAN 12.3)
    cmp byte [wa_lkfired], 0
    je .done
    dec byte [wa_lkfired]
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
    call wa_lkhost              ; byte 8: one of the host's lifts, in turn
    jz .pitch                   ; (ARENAS-PLAN 4.1) - its index x 64 + its
    xor al, al                  ; height / 4 - the guest's pitch / 4
    cmp al, [wa_nlift]          ; (nobody reads a pitch)
    je .b8
    push bx
    mov bl, [wa_lkli]
    inc bl
    cmp bl, [wa_nlift]
    jb .li
    xor bl, bl
.li:
    mov [wa_lkli], bl
    xor bh, bh
    shl bx, 1
    mov bx, [wa_lift+bx]
    mov ax, [wa_bxh+bx]
    shr ax, 1                   ; (arenagen: a lift's heights to 252)
    shr ax, 1
    mov ah, [wa_lkli]
    ror ah, 1
    ror ah, 1
    or al, ah
    pop bx
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
    cmp byte [wa_lkme], 0       ; (slot 0: the host)
    jne .guest
    mov byte [wa_lkfr], WL_HSNAP ; THE HOST'S, with bots: who dealt the
    mov al, [wa_lkattr]         ; damage, and where each bot is (x / 8,
    stosb                       ; y / 8, its facing / 4, its flags): the
    mov byte [wa_lkbo], 0       ; bot slots, the players' after
.bot:
    xor ax, ax
    mov dl, [wa_lkbo]
    cmp dl, [wa_lknb]
    jae .none
    add dl, [wa_lknp]
    cmp dl, 4
    jae .none
    mov al, dl                  ; BX = its figure (the host's: slot - 1)
    dec al
    mov ah, F_SIZE
    mul ah
    add ax, wa_figs
    mov bx, ax
    xor ax, ax
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
    xor ax, ax
    stosw
    stosw
.nb:
    inc byte [wa_lkbo]
    cmp byte [wa_lkbo], 2
    jb .bot
    mov cl, 22
    jmp .send
.guest:
    mov byte [wa_lkfr], WL_GSNAP ; THE GUEST'S, with bots: its hits on them
    mov ax, [wa_lkbd]
    stosw
    mov cl, 15
.send:
    mov si, wa_lkfr
    jmp wa_lksend

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
    call wa_lksend
    cmp byte [wa_lknb], 0
    je .out
    cmp byte [wa_lkme], 0
    jne .out
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
    jmp wa_lksend
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
    call wa_lksend
    jc .out
    inc byte [wa_lkqs]
    mov ax, [wa_tkn]
    mov [wa_lkqt], ax
.out:
    ret

; wa_lkmtake - a frame in the match: AL = its type, SI it
wa_lkmtake:
    cmp al, WL_PEERS            ; (the window's: a peer still there telling
    je .out                     ; us whom it knows - its byte 1 no tag)
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
    mov bl, [wa_lkpslot]        ; the sender's row: its deaths by cause
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov ax, [si+2]
    mov dx, [si+4]
    call wa_lknews
    mov [wa_lkd+bx], ax
    mov [wa_lkd+bx+2], dx
    jmp wa_lkboard
.bscore:
    cmp byte [wa_lkme], 0       ; (the host's bots' rows: to the guests -
    je .out                     ; rows 2 and 3, those of them that are bots
    cmp byte [wa_lkpslot], 0    ; and not players)
    jne .out
    cmp byte [wa_lknp], 2
    ja .r3
    mov bx, 8
    mov ax, [si+2]
    mov dx, [si+4]
    call wa_lknews
    mov [wa_lkd+8], ax
    mov [wa_lkd+10], dx
.r3:
    cmp byte [wa_lknp], 3
    ja .bd
    mov bx, 12
    mov ax, [si+6]
    mov dx, [si+8]
    call wa_lknews
    mov [wa_lkd+12], ax
    mov [wa_lkd+14], dx
.bd:
    jmp wa_lkboard
.out:
    ret

; wa_lksnapin - a player's SNAP (the loaded peer's): where it is, its ack
; of our events, the damage it dealt us
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
    push si                     ; its figure's record (by its slot)
    mov bx, si
    mov al, [wa_lkpslot]
    call wa_slotrec
    mov ax, [bx+2]
    mov [si+WR_X], ax
    mov ax, [bx+4]
    mov [si+WR_Y], ax
    mov al, [bx+6]
    xor ah, ah
    shl ax, 1
    mov [si+WR_Z], ax
    mov al, [bx+7]
    xor ah, ah
    shl ax, 1
    shl ax, 1
    mov [si+WR_A], ax
    mov al, [bx+9]
    mov [si+WR_F], al
    mov [wa_lkrf], al           ; (its flags, for the kill's kind below)
    pop si
    cmp byte [wa_lkpslot], 0    ; the host's: one of its lifts (index x 64
    jne .notlift                ; + height / 4, wa_lksnap's)
    mov al, [si+8]
    mov bl, al
    rol bl, 1                   ; (the index: the top two bits)
    rol bl, 1
    and bx, 3
    cmp bl, [wa_nlift]          ; (fenced: a lift the arena has)
    jae .notlift
    shl bx, 1                   ; (x 2: a word's)
    and ax, 63
    shl ax, 1
    shl ax, 1
    mov [wa_lklift+bx], ax
.notlift:
    cmp byte [wa_lkme], 0       ; we the host: its shot, heard by our bots
    jne .lifted
    test byte [si+9], 2
    jz .lifted
    push dx
    mov al, [wa_lkpslot]
    call wa_lklocal
    mov dl, al
    mov ax, [si+2]
    mov bx, [si+4]
    call wa_bhear_far
    pop dx
.lifted:
    mov al, [wa_lkpslot]        ; who hurt us: the sender, or (the host's,
    call wa_lklocal             ; with bots) whom it names
    mov [wa_lkhby], al
    cmp byte [si], WL_HSNAP
    jne .who
    mov al, [si+13]
    call wa_lklocal             ; (the slot it names: its code here)
    mov [wa_lkhby], al
    call wa_lkbots_in
.who:
    cmp byte [si], WL_GSNAP     ; (a guest's hits on the bots: the host's
    jne .ack0                   ; to apply)
    cmp byte [wa_lkme], 0
    jne .ack0
    call wa_lkbhits
.ack0:
    mov al, [si+12]             ; THE ACK: its, then every event all the
    mov [wa_lkack], al          ; players have acked leaves the queue
    call wa_lkqpop
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
    mov bl, [wa_lkrf]           ; its kill by the sender's weapon: the
    shr bl, 1                   ; shotgun's collapse (1), the railgun's
    shr bl, 1                   ; bolt (3), else the fall (0)
    shr bl, 1
    and bl, 7
    mov bh, 3
    cmp bl, 5
    je .kw
    mov bh, 1
    cmp bl, 1
    je .kw
    mov bh, 0
.kw:
    mov bl, bh
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

; wa_lkqpop - the head of the event queue out while every player in the
; match (bound, not gone) has acked it - each one's place in it back one
wa_lkqpop:
    push ax
    push bx
    push cx
    push si
    push di
.head:
    cmp byte [wa_lkqn], 0
    je .out
    xor cx, cx
.p:
    cmp cl, [wa_lkpn]
    jae .pop
    mov al, cl
    call wa_prec
    inc cx
    cmp word [bx+WP_ID], 0
    jne .b
    cmp word [bx+WP_ID+2], 0
    je .p
.b:
    cmp byte [bx+WP_GONE], 0
    jne .p
    mov al, [bx+WP_ACK]
    sub al, [wa_lkseq]
    cmp al, 128
    jae .out                    ; (this one has not: the head stays)
    jmp .p
.pop:
    push es
    push ds
    pop es
    mov si, wa_lkq + WL_EVSZ
    mov di, wa_lkq
    mov cx, (WL_EVQ - 1) * WL_EVSZ
    cld
    rep movsb
    pop es
    dec byte [wa_lkqn]
    inc byte [wa_lkseq]
    xor cx, cx                  ; every player's place in it, one less
.q:
    cmp cl, [wa_lkpn]
    jae .head
    mov al, cl
    call wa_prec
    inc cx
    cmp byte [bx+WP_QS], 0
    je .q
    dec byte [bx+WP_QS]
    jmp .q
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
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
    cmp al, EV_FLAG
    je .flag
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
.flag:
    jmp wc_evin

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
    call wa_lklocal
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
    call wa_figslot             ; its owner (a bot's, the host): heard from
    mov [wa_lkfs], al           ; and still here, or off the board
    call wa_slotpeer
    jc .hide
    cmp byte [bx+WP_RSEEN], 0
    je .hide
    cmp byte [bx+WP_GONE], 0
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
    cmp byte [wa_lkme], 0       ; (another player on the host's lift: a
    jne .zd                     ; rider)
    mov al, [wa_lkfs]
    cmp al, [wa_lknp]
    jae .zd
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
    call wa_zapgo               ; (the railgun's bolt over: the dissolve)
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
    cmp byte [wa_lkme], 0       ; (a guest: every figure the others')
    jne .yes
    push ax                     ; the host: a player's figure
    call wa_figslot
    cmp al, [wa_lknp]
    pop ax
    jae .no
.yes:
    cmp al, al
    ret
.no:
    or sp, sp
    ret

; wa_lkabsent - DI = a figure: ZF = 1 when it is a bot the match has none
; of - alone, past the setup's count; linked, a slot past the players and
; the bots. Keeps every register
wa_lkabsent:
    cmp byte [wa_lkon], 0       ; ALONE: figures 0 to the setup's count - 1
    jne .linked                 ; are the bots
    push ax
    call wa_bidx                ; AL = its index (compares: no divide)
    cmp al, [wa_lknb]
    pop ax
    jb .here
    cmp al, al
    ret
.linked:
    push ax
    push bx
    call wa_figslot
    mov bl, [wa_lknp]
    add bl, [wa_lknb]
    cmp al, bl
    pop bx
    pop ax
    jb .here
    cmp al, al
    ret
.here:
    or sp, sp
    ret

; wa_lkcanon - AL = a code here (0 us, 1..3 figures 0..2): the slot it is
; on the wire (NET-PLAN 4). wa_lklocal - the other way. Our slot is
; wa_lkme; the figures are the other slots in order. With two players
; (LINK-PLAN v1) it is the swap of 0 and 1 for the guest. Keep all but AL
wa_lkcanon:
    or al, al
    jnz .fig
    mov al, [wa_lkme]
    ret
.fig:
    dec al                      ; figure i: slot i, or i + 1 from ours on
    cmp al, [wa_lkme]
    jb .out
    inc al
.out:
    ret
wa_lklocal:
    cmp al, [wa_lkme]
    jne .other
    xor al, al
    ret
.other:
    jae .out                    ; (above ours: the same; below: one more)
    inc al
.out:
    ret

; wa_figslot - DI = a figure: AL = its slot. Keeps all but AX
wa_figslot:
    push dx
    mov ax, di
    sub ax, wa_figs
    mov dl, F_SIZE
    div dl
    inc al
    pop dx
    jmp wa_lkcanon

; wa_slotfig - AL = a slot (not ours): DI = its figure. Keeps all but AX
wa_slotfig:
    call wa_lklocal
    dec al
    push dx
    mov dl, F_SIZE
    mul dl
    pop dx
    add ax, wa_figs
    mov di, ax
    ret

; wa_slotrec - AL = a slot: SI = its figure's remote record (wa_lkr).
; Keeps all but AX
wa_slotrec:
    call wa_lklocal
    dec al
    push dx
    mov dl, WR_SZ
    mul dl
    pop dx
    add ax, wa_lkr
    mov si, ax
    ret

; wa_slotpeer - AL = a slot: BX = the record of the peer that owns it (a
; player's own; a bot's, the host), CF = 0; CF = 1 none. Keeps all but BX
wa_slotpeer:
    push ax
    push cx
    cmp al, [wa_lknp]           ; (a bot: the host's, slot 0)
    jb .p
    xor al, al
.p:
    mov ah, al
    xor cx, cx
.f:
    cmp cl, [wa_lkpn]
    jae .none
    mov al, cl
    call wa_prec
    inc cx
    cmp word [bx+WP_ID], 0
    jne .b
    cmp word [bx+WP_ID+2], 0
    je .f
.b:
    cmp [bx+WP_SLOT], ah
    jne .f
    clc
    jmp .out
.none:
    stc
.out:
    pop cx
    pop ax
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
    call wa_figslot             ; (its slot)
    mov bl, al
    mov al, [wa_killer]
    call wa_lkcanon
wa_lkcount:                     ; BL = the dead one, AL the cause (codes on
    mov ah, bl                  ; the wire): D[BL][AL] + 1, the HUD's
    call wa_lkkill              ; message, the board again
    xor bh, bh
    shl bx, 1
    shl bx, 1
    add bl, al
    inc byte [wa_lkd+bx]
    pop bx
    pop ax
    ; (falls through)
; wa_lknews - BX = a row of wa_lkd (the dead one's slot x 4), DX:AX = its
; four counts as they arrive: each that ROSE is a kill the HUD says (a SCORE
; once a second: a kill another machine judged). Keeps every register
wa_lknews:
    push ax
    push cx
    push si
    push di
    mov si, ax                  ; (SI, DI = the new counts, two each)
    mov di, dx
    xor cx, cx                  ; CL = the cause's slot
.c:
    mov ax, si
    cmp cl, 2
    jb .lo
    mov ax, di
.lo:
    test cl, 1
    jz .b
    mov al, ah
.b:
    push bx
    add bl, cl
    cmp al, [wa_lkd+bx]
    pop bx
    jbe .n
    mov al, cl                  ; risen: the cause killed the dead one
    mov ah, bl
    shr ah, 1
    shr ah, 1
    call wa_lkkill
.n:
    inc cx
    cmp cl, 4
    jb .c
    pop di
    pop si
    pop cx
    pop ax
    ret

; wa_lkkill - AL = the cause's slot, AH = the dead one's: the HUD's message,
; in codes here. Keeps every register
wa_lkkill:
    push ax
    push bx
    mov bl, al
    mov al, ah
    call wa_lklocal
    xchg al, bl                 ; BL = the dead one here
    call wa_lklocal
    mov ah, bl
    call wa_hudkill
    pop bx
    pop ax
    ret

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
    call wa_lklocal
    xor ah, ah
    mov bx, ax
    add [wa_deaths+bx], cl
    pop bx
    push bx
    mov al, bh
    call wa_lklocal
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
    push bx
    add si, 14
    mov cl, [wa_lknp]           ; the bot slots, the players' after
.b:
    cmp cl, 4                   ; (four slots: the rest of the SNAP's two
    jae .done                   ; entries are none)
    push si
    mov al, cl
    call wa_slotrec
    mov di, si
    pop si
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
    inc cl
    mov al, cl
    sub al, [wa_lknp]
    cmp al, 2
    jb .b
.done:
    pop bx
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
    push ax                     ; DI = the bot: the slot the players' count
    mov al, [wa_lknp]           ; plus BX
    add al, bl
    cmp al, 4
    pop ax
    jae .n
    push ax
    mov al, [wa_lknp]
    add al, bl
    call wa_slotfig
    mov al, [wa_lkpslot]        ; (the sender: its code here, the killer)
    call wa_lklocal
    mov [wa_killer], al
    pop ax
    call wa_lkabsent
    je .n
    mov ah, [wa_lkrf]           ; its kill by the guest's weapon: the
    shr ah, 1                   ; shotgun's collapse, the railgun's bolt,
    shr ah, 1                   ; else the fall
    shr ah, 1
    and ah, 7
    cmp ah, 5
    mov ah, 3
    je .k
    mov ah, [wa_lkrf]
    and ah, 0x38
    cmp ah, 1 << 3
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
    push cx
    push si
    mov cx, [wa_nlift]
    jcxz .out
    xor si, si
.each:
    mov bx, [wa_lift+si]
    mov ax, [di+F_Z]            ; on its top: 40 units' slack, as its
    add ax, 40                  ; height comes a SNAP late while it rises
    cmp ax, [wa_bxh+bx]
    jl .next
    shl bx, 1                   ; (its box: x 8)
    mov ax, [di+F_X]
    cmp ax, [wa_boxes+bx]
    jl .next
    cmp ax, [wa_boxes+bx+4]
    jg .next
    mov ax, [di+F_Y]
    cmp ax, [wa_boxes+bx+2]
    jl .next
    cmp ax, [wa_boxes+bx+6]
    jg .next
    inc byte [wa_lbn+si]
.next:
    inc si
    inc si
    loop .each
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; wa_lkip - SI = an IP address (4 bytes): dotted, at DI (wa_lkln's room)
wa_lkip:
    mov cx, 4
.b:
    lodsb
    push cx
    xor ah, ah
    mov bl, 100                 ; (up to three digits, no leading zeros)
    div bl
    mov ch, ah
    or al, al
    jz .t
    add al, '0'
    stosb
    mov al, ch
    xor ah, ah
    mov bl, 10
    div bl
    add al, '0'
    stosb
    jmp .u
.t:
    mov al, ch
    xor ah, ah
    mov bl, 10
    div bl
    or al, al
    jz .u
    add al, '0'
    stosb
.u:
    mov al, ah
    add al, '0'
    stosb
    pop cx
    cmp cx, 1
    je .end
    mov al, '.'
    stosb
.end:
    loop .b
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
    xor dx, dx                  ; over every peer bound: DL bit 0 one left,
    xor cx, cx                  ; 1 one lost, 2 one not heard from yet, 3
                                ; the one that left the host (slot 0)
.p:
    cmp cl, [wa_lkpn]
    jae .say
    mov al, cl
    call wa_prec
    inc cx
    call wa_lkbound
    jz .p
    cmp byte [bx+WP_GONE], 0
    je .g
    or dl, 1
    cmp byte [bx+WP_SLOT], 0
    jne .p                      ; (one that left: not lost, not waited for)
    or dl, 8
    jmp .p
.g:
    cmp byte [bx+WP_LOST], 0
    je .l
    or dl, 2
.l:
    cmp byte [bx+WP_RSEEN], 0
    jne .p
    or dl, 4
    jmp .p
.say:
    mov si, wa_lm_lost          ; LOST first: a player still in the match
    test dl, 2                  ; not heard
    jnz .put
    mov si, wa_lm_wait
    test dl, 4
    jnz .put
    mov si, wa_lm_hleft         ; (the host gone: its bots with it, and
    test dl, 8                  ; the match is over in all but name)
    jnz .put
    mov si, wa_lm_gone
    test dl, 1
    jz .out
    cmp byte [wa_lknp], 2
    je .put
    mov si, wa_lm_left
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
wa_lm_left: db '   A PLAYER LEFT    '
wa_lm_hleft: db '     HOST LEFT      '
wa_lm_lost: db '     LINK LOST      '
wa_lm_wait: db 'WAITING FOR OPPONENT'
