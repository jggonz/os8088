; apps/os88cable.inc - a null-modem link over the desktop serial lease
; (SPEC.md 115), written once for any package that links two machines:
; the port search, a framer and its CRC. docs/plans/completed/WIREARENA-LINK-PLAN.md
; section 1 is why it exists; WireArena is its first user, and WireFleet's
; own copy (apps/wirefleet/wflink.inc, SPEC.md 114.6 and 114.9) moves onto
; it in that plan's story L3.1.
;
; Its messages fit a 38-column row.
;
; THE SEARCH is SPEC.md 114.9's: Automatic tries COM2 first (COM1 is the
; mouse on most machines), and with two candidates MOVES the lease between
; them after a dwell of 40 to 71 ticks drawn afresh at each move, so two
; machines searching cannot stay in step on opposite ports. The first frame
; from a peer (the includer says so: LK_HEARD) ends the moving. Refusals 3,
; 4 and 9 take that port off the list; 1 and 2 are retried every 18 ticks.
; Open and Close are UI-task calls and illegal inside a §53 bracket
; (§53.7): call lk_tick from the window, never from inside the bracket.
; lk_poll and lk_send are Read and Write only, legal in either.
;
; THE FRAMES: 7Eh, the escaped body, 7Eh; 7Eh and 7Dh in the body go as 7Dh
; then the byte XOR 20h. A body's byte 0 is its TYPE and its last two bytes
; CRC-16/CCITT-FALSE (initial FFFFh, polynomial 1021h) over the bytes
; before them. Each type has one length, its CRC included, from the
; includer's table: a frame of another length, of an unknown type, or with
; a bad CRC is dropped whole, and a line error or an overflow poisons the
; frame being received until the next delimiter.
;
; The includer provides:
;   lk_blk     LK_SIZE bytes of state (its bss)
;   lk_sbuf    SER_BUFFER_SIZE bytes in its own region: the lease's rings
;   lk_crct    512 bytes: the CRC's table, built when a port is opened
;   lk_lens    a byte per type, the body's length with its CRC (0: no such
;              type); LK_NTYPES the table's length
;   lk_take    called with AL = the type and SI = a valid body (length and
;              CRC checked); may change any register but the stack
; and calls OS88_REGION_MOVABLE's region rule as the lease needs: the
; region is pinned while the port is held (SPEC.md 115) and declared
; movable again when it is let go.

LK_DIVISOR  equ 48              ; 2400 baud (SPEC.md 114.3)
LK_MAXF     equ 32              ; the longest body, its CRC included
LK_TXMAX    equ 136             ; two longest frames, escaped, and more

LK_MODE     equ 0               ; 0 off, 1 Automatic, 2 a named port
LK_OPEN     equ 1               ; 1: the lease is held
LK_PORT     equ 2               ; the port held: 1 COM1, 2 COM2
LK_WANT     equ 3               ; the port named, or the one a peer was found on
LK_CANDS    equ 4               ; the search's candidates: bit 0 COM1, 1 COM2
LK_HEARD    equ 5               ; the includer: a peer answered on this port
LK_RLEN     equ 6               ; the frame being received (255: poisoned)
LK_RESC     equ 7               ; ...an escape pending
LK_DIRTY    equ 8               ; 1: LK_MSG changed (the includer clears it)
LK_NOW      equ 10              ; (w) OSAPI_GET_TICKS at the last lk_tick
LK_RETRY    equ 12              ; (w) a refusal is waited out till then
LK_DWELL    equ 14              ; (w) the search moves ports then
LK_MSG      equ 16              ; (w) the line that says what the link is doing
LK_TXLEN    equ 18              ; (w) bytes framed in LK_TXB
LK_TXPOS    equ 20              ; (w) ...of which this many are in the lease
LK_STRX     equ 22              ; (w) bytes received
LK_STERR    equ 24              ; (w) ...with a line error or an overflow
LK_STFR     equ 26              ; (w) valid frames
LK_STBAD    equ 28              ; (w) frames dropped
LK_OLD      equ 9               ; 1: the module has no block verbs (115.1.2)
LK_RXB      equ 30              ; LK_MAXF bytes: the body being received
LK_TXB      equ LK_RXB + LK_MAXF ; LK_TXMAX bytes: framed bytes waiting
LK_RXT      equ LK_TXB + LK_TXMAX ; LK_RXTMAX bytes: a Read block's run
LK_RXTMAX   equ 64
LK_SIZE     equ LK_RXT + LK_RXTMAX

; lk_auto - Link > Automatic: search both ports, starting now. Keeps all
; but AX
lk_auto:
    mov byte [lk_blk+LK_MODE], 1
    mov byte [lk_blk+LK_CANDS], 3
    mov byte [lk_blk+LK_HEARD], 0
    call OSAPI_GET_TICKS
    mov [lk_blk+LK_RETRY], ax   ; due now
    ret

; lk_connect - AL = 1 or 2: Link > Connect COM1 / COM2, that port and no
; other. CF = 1 refused (LK_MSG says why). Keeps all but AX, BX, SI
lk_connect:
    push ax
    call lk_disconnect
    pop ax
    mov [lk_blk+LK_WANT], al
    mov [lk_blk+LK_CANDS], al
    mov byte [lk_blk+LK_MODE], 2
    push ax
    or al, 0x80                 ; the user NAMED it: not the mouse (115.1.1)
    call lk_lease
    pop bx
    jnc .ok
    cmp ax, 4                   ; (a SERIAL.DRV older than 115.1.1 reads the
    jne .no                     ; flag as a port number: ask as it does)
    mov al, bl
    call lk_lease
    jnc .ok
.no:
    call lk_leaseerror
    stc
    ret
.ok:
    mov word [lk_blk+LK_MSG], lk_m_search
    mov byte [lk_blk+LK_DIRTY], 1
    clc
    ret

; lk_disconnect - Link > Disconnect: the port let go, no search. Keeps all
; but AX
lk_disconnect:
    mov byte [lk_blk+LK_MODE], 0
    cmp byte [lk_blk+LK_OPEN], 0
    je .out
    mov al, 1
    call OSAPI_SERIAL
    mov byte [lk_blk+LK_OPEN], 0
    OS88_REGION_MOVABLE         ; (the rings are the lease's no longer)
.out:
    mov word [lk_blk+LK_MSG], lk_m_off
    mov byte [lk_blk+LK_DIRTY], 1
    ret

; lk_lease - AL = the port (bit 7: the user named it): opened, or a lease
; already held MOVED there (115.1). CF = 1 refused, AX = the refusal and
; the lease closed. Keeps all but AX, BX, SI
lk_lease:
    push ax
    and al, 0x7F
    mov [lk_blk+LK_PORT], al
    pop ax
    cmp byte [lk_blk+LK_OPEN], 0
    jne .open
    push ax
    push dx
    mov dx, ds                  ; pinned before its rings are published (DS:
    xor ax, ax                  ; the region's name - on kern_mega CS is its
    call OSAPI_MEM_MOVABLE      ; code half)
    pop dx
    pop ax
.open:
    mov bl, al
    mov bh, LK_DIVISOR
    xor ax, ax
    mov si, lk_sbuf
    call OSAPI_SERIAL
    jc .refused
    mov byte [lk_blk+LK_OPEN], 1
    call lk_crcinit
    xor ax, ax
    mov [lk_blk+LK_OLD], al
    mov [lk_blk+LK_HEARD], al
    mov [lk_blk+LK_RLEN], al
    mov [lk_blk+LK_RESC], al
    mov [lk_blk+LK_TXLEN], ax
    mov [lk_blk+LK_TXPOS], ax
    call OSAPI_RAND             ; 40..71 ticks here before moving
    and ax, 31
    add ax, 40
    mov bx, ax
    call OSAPI_GET_TICKS
    add ax, bx
    mov [lk_blk+LK_DWELL], ax
    clc
    ret
.refused:
    push ax
    mov byte [lk_blk+LK_OPEN], 0
    OS88_REGION_MOVABLE
    pop ax
    stc
    ret

; lk_leaseerror - AX = a refusal: LK_MSG the line that says what to do.
; Keeps all but AX, BX
lk_leaseerror:
    cmp ax, 9
    jbe .known
    mov ax, 5
.known:
    shl ax, 1
    mov bx, ax
    mov ax, [lk_errs+bx]
    mov [lk_blk+LK_MSG], ax
    mov byte [lk_blk+LK_DIRTY], 1
    ret

; lk_tick - the window's tick, OUTSIDE any bracket: the search moves on.
; Keeps all but AX, BX, SI
lk_tick:
    call OSAPI_GET_TICKS
    mov [lk_blk+LK_NOW], ax
    cmp byte [lk_blk+LK_MODE], 1
    jne .out
    cmp byte [lk_blk+LK_OPEN], 0
    jne .live
    mov ax, [lk_blk+LK_NOW]
    sub ax, [lk_blk+LK_RETRY]
    js .out                     ; (a refusal waited out)
    mov al, [lk_blk+LK_CANDS]
    or al, al
    jz .none
    mov ah, [lk_blk+LK_WANT]    ; the found port first, then COM2
    or ah, ah
    jnz .first
    mov ah, 2
.first:
    test al, ah
    jnz .try
    xor ah, 3
.try:
    mov al, ah
    call lk_lease
    jc lk_refusal
    mov word [lk_blk+LK_MSG], lk_m_search
    mov byte [lk_blk+LK_DIRTY], 1
.out:
    ret
.none:
    mov byte [lk_blk+LK_MODE], 0
    mov word [lk_blk+LK_MSG], lk_m_noport
    mov byte [lk_blk+LK_DIRTY], 1
    ret
.live:
    cmp byte [lk_blk+LK_HEARD], 0
    jne .found
    cmp byte [lk_blk+LK_CANDS], 3 ; (one candidate: nothing to move to)
    jne .out
    mov ax, [lk_blk+LK_NOW]
    sub ax, [lk_blk+LK_DWELL]
    js .out
    mov al, [lk_blk+LK_PORT]
    xor al, 3
    call lk_lease               ; MOVE to the other port
    jc lk_refusal
    ret
.found:
    mov al, [lk_blk+LK_PORT]    ; a peer here: this is the port
    mov [lk_blk+LK_WANT], al
    mov [lk_blk+LK_CANDS], al
    ret

; lk_refusal - AX = a refusal during the search: 3, 4 and 9 a candidate
; fewer; 1 and 2 waited out 18 ticks; anything else ends it, its line said
lk_refusal:
    cmp ax, 3
    je .drop
    cmp ax, 4
    je .drop
    cmp ax, 9
    je .drop
    push ax
    call lk_leaseerror
    pop ax
    mov bx, [lk_blk+LK_NOW]
    add bx, 18
    mov [lk_blk+LK_RETRY], bx
    cmp ax, 2
    jbe .out
    mov byte [lk_blk+LK_MODE], 0
.out:
    ret
.drop:
    mov bx, [lk_blk+LK_NOW]
    mov [lk_blk+LK_RETRY], bx   ; the next candidate at once
    mov al, [lk_blk+LK_PORT]
    not al
    and [lk_blk+LK_CANDS], al
    jnz .out
    mov byte [lk_blk+LK_MODE], 0
    mov word [lk_blk+LK_MSG], lk_m_noport
    mov byte [lk_blk+LK_DIRTY], 1
    ret

; lk_poll - CX = at most this many bytes: what the cable brought, framed
; (lk_take per valid frame), then what waits to go, into the lease. Legal
; inside a bracket. Changes every register but DS, ES, BP and the stack
lk_poll:
    cmp byte [lk_blk+LK_OPEN], 0
    je .out
    cmp byte [lk_blk+LK_OLD], 0
    jne .rx
    cmp cx, LK_RXTMAX           ; READ BLOCK (115.1.2): the run in one call
    jbe .n
    mov cx, LK_RXTMAX
.n:
    mov di, lk_blk+LK_RXT
    mov al, 5
    call OSAPI_SERIAL
    jc .old
    or ah, ah
    jz .clean
    inc word [lk_blk+LK_STERR]  ; (a line error or an overflow in the run:
    mov byte [lk_blk+LK_RLEN], 255 ; the frame under way is not trusted)
    mov byte [lk_blk+LK_RESC], 0
.clean:
    xor ah, ah
    mov cx, ax
    jcxz .flush
    add [lk_blk+LK_STRX], ax
    mov si, lk_blk+LK_RXT
.blk:
    lodsb
    push cx
    push si
    call lk_byte
    pop si
    pop cx
    loop .blk
.flush:
    jmp lk_flush
.old:
    mov byte [lk_blk+LK_OLD], 1 ; (an older SERIAL.DRV: a byte a call)
    mov cx, 16
.rx:
    push cx
    mov al, 2
    call OSAPI_SERIAL
    jc .empty
    inc word [lk_blk+LK_STRX]
    or ah, ah
    jz .byte
    inc word [lk_blk+LK_STERR]  ; (a line error or an overflow: the frame
    mov byte [lk_blk+LK_RLEN], 255 ; under way is not to be trusted)
    mov byte [lk_blk+LK_RESC], 0
.byte:
    call lk_byte
    pop cx
    loop .rx
    jmp lk_flush
.empty:
    pop cx
    jmp lk_flush
.out:
    ret

; lk_byte - AL = a byte off the cable, into the frame being received
lk_byte:
    cmp al, 0x7E
    je .delim
    cmp byte [lk_blk+LK_RLEN], LK_MAXF
    jae .bad                    ; (too long, or poisoned)
    cmp byte [lk_blk+LK_RESC], 0
    jne .esc
    cmp al, 0x7D
    jne .store
    mov byte [lk_blk+LK_RESC], 1
    ret
.esc:
    xor al, 0x20
    mov byte [lk_blk+LK_RESC], 0
.store:
    mov bl, [lk_blk+LK_RLEN]
    xor bh, bh
    mov [lk_blk+LK_RXB+bx], al
    inc byte [lk_blk+LK_RLEN]
    ret
.bad:
    mov byte [lk_blk+LK_RLEN], 255
    mov byte [lk_blk+LK_RESC], 0
    ret
.delim:
    mov cl, [lk_blk+LK_RLEN]
    mov byte [lk_blk+LK_RLEN], 0
    cmp byte [lk_blk+LK_RESC], 0
    mov byte [lk_blk+LK_RESC], 0
    jne .drop
    or cl, cl
    jz .out                     ; (two delimiters: an idle line)
    mov si, lk_blk+LK_RXB
    jmp lk_frame
.drop:
    inc word [lk_blk+LK_STBAD]
.out:
    ret

; lk_frame - SI = a body, CL = its length with its CRC: checked - a known
; type, that type's length, its CRC - and handed to lk_take, or dropped and
; counted. The framer's last step, and a whole carrier's for one that
; delivers bodies already delimited (apps/os88raw.inc's Ethernet)
lk_frame:
    cmp cl, LK_MAXF
    ja .drop
    mov bl, [si]
    cmp bl, LK_NTYPES
    jae .drop
    xor bh, bh
    cmp cl, [lk_lens+bx]
    jne .drop
    push si
    xor ch, ch
    dec cx
    dec cx
    mov ax, 0xFFFF
    call lk_crc                 ; (SI past the body: its CRC)
    cmp ax, [si]
    pop si
    jne .drop
    inc word [lk_blk+LK_STFR]
    mov al, [si]
    jmp lk_take
.drop:
    inc word [lk_blk+LK_STBAD]
    ret

; lk_seal - SI = a body, CL = its length WITHOUT the CRC: the CRC stored
; after it, CL = the length with it (a carrier that sends whole bodies).
; Keeps all but AX, CX
lk_seal:
    push si
    push cx
    xor ch, ch
    mov ax, 0xFFFF
    call lk_crc
    mov [si], ax                ; (SI past the body)
    pop cx
    pop si
    add cl, 2
    ret

; lk_send - SI = a body, CL = its length WITHOUT the CRC: framed, its CRC
; appended, behind what waits, and the lease fed. CF = 1: no room (send it
; again later). Changes every register but DS, ES, BP and the stack
lk_send:
    push es                     ; (borrowed for the moves: a callback's is
    cmp byte [lk_blk+LK_OPEN], 0 ; the kernel's, SPEC.md 20)
    je .full
    mov di, [lk_blk+LK_TXPOS]   ; (what went already: the rest moved down
    or di, di                   ; first)
    jz .room
    push si
    push cx
    mov cx, [lk_blk+LK_TXLEN]
    sub cx, di
    lea si, [lk_blk+LK_TXB+di]
    mov di, lk_blk+LK_TXB
    push ds
    pop es
    rep movsb
    sub di, lk_blk+LK_TXB
    mov [lk_blk+LK_TXLEN], di
    mov word [lk_blk+LK_TXPOS], 0
    pop cx
    pop si
.room:
    xor ch, ch
    mov ax, cx                  ; the worst case: every byte escaped, and
    shl ax, 1                   ; the CRC's, and two delimiters
    add ax, 6
    add ax, [lk_blk+LK_TXLEN]
    cmp ax, LK_TXMAX
    ja .full
    push si
    push cx
    mov ax, 0xFFFF
.c0:                            ; (tools' stops: the CRC's cost)
    call lk_crc
.c1:
    mov dx, ax                  ; DX = the CRC
    pop cx
    pop si
    mov di, [lk_blk+LK_TXLEN]
    add di, lk_blk+LK_TXB
    push ds
    pop es
    mov al, 0x7E
    stosb
.b:
    lodsb
    call .put
    loop .b
    mov al, dl
    call .put
    mov al, dh
    call .put
    mov al, 0x7E
    stosb
    sub di, lk_blk+LK_TXB
    mov [lk_blk+LK_TXLEN], di
    call lk_flush
    pop es
    clc
    ret
.full:
    pop es
    stc
    ret
.put:
    cmp al, 0x7E
    je .e
    cmp al, 0x7D
    jne .s
.e:
    mov ah, al
    mov al, 0x7D
    stosb
    mov al, ah
    xor al, 0x20
.s:
    stosb
    ret

; lk_flush - framed bytes into the lease's TX ring until it is full: one
; Write block, or a Write a byte on an older module. Changes AX, BX, CX
lk_flush:
    mov bx, [lk_blk+LK_TXPOS]
    cmp byte [lk_blk+LK_OLD], 0
    jne .b
    mov cx, [lk_blk+LK_TXLEN]   ; WRITE BLOCK (115.1.2): what waits, in one
    sub cx, bx                  ; call
    jbe .out
    push si
    lea si, [lk_blk+LK_TXB+bx]
    mov al, 4
.c0:                            ; (tools' stops: a Write's cost)
    call OSAPI_SERIAL
.c1:
    pop si
    jc .wold
    add bx, ax
    jmp .out
.wold:
    mov byte [lk_blk+LK_OLD], 1
.b:
    cmp bx, [lk_blk+LK_TXLEN]
    jae .out
    mov cl, [lk_blk+LK_TXB+bx]
    mov al, 3
    call OSAPI_SERIAL
    jc .out
    inc bx
    jmp .b
.out:
    mov [lk_blk+LK_TXPOS], bx
    ret

; lk_crc - CRC-16/CCITT-FALSE: AX the seed, SI the bytes, CX the count. AX
; the CRC, SI past them. A byte a table lookup (lk_crct): ~40 cycles where
; the bit loop was ~400 (LINK-PLAN 11.2). Changes CX
lk_crc:
    jcxz .out
    push bx
.byte:
    mov bl, ah
    xor bl, [si]
    inc si
    xor bh, bh
    shl bx, 1
    mov ah, al
    xor al, al
    xor ax, [lk_crct+bx]
    loop .byte
    pop bx
.out:
    ret

; lk_crcinit - lk_crct: each byte's CRC step, from the polynomial 1021h
lk_crcinit:
    push ax
    push bx
    push cx
    push dx
    xor bx, bx
.t:
    mov ah, bl
    xor al, al
    mov cx, 8
.bit:
    shl ax, 1
    jnc .nx
    xor ax, 0x1021
.nx:
    loop .bit
    mov dx, bx
    shl bx, 1
    mov [lk_crct+bx], ax
    mov bx, dx
    inc bl
    jnz .t
    pop dx
    pop cx
    pop bx
    pop ax
    ret

lk_errs:    dw lk_m_search, lk_m_busy, lk_m_mouse, lk_m_mport, lk_m_none
            dw lk_m_buffer, lk_m_search, lk_m_search, lk_m_drv, lk_m_bios
lk_m_off:    db 'Not linked: Link > Automatic', 0
lk_m_search: db 'Looking for the cable...', 0
lk_m_busy:   db 'Serial port busy; trying again', 0
lk_m_mouse:  db 'Move the mouse, or Link > COM1/COM2', 0
lk_m_mport:  db 'That port is the mouse: use the other', 0
lk_m_none:   db 'No UART on that port', 0
lk_m_bios:   db 'Port not in the BIOS list: see SETUP', 0
lk_m_buffer: db 'The serial lease refused the buffer', 0
lk_m_drv:    db 'Needs SERIAL.DRV: insert system disk', 0
lk_m_noport: db 'No free serial port for the cable', 0
