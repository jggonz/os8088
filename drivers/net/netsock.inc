; =============================================================================
; NET.DRV - the SOCKET half (docs/NET-STACK-PLAN.md stage B, SPEC.md 62.11)
;
; drivers/net/netpkg.inc is the ABI a package sees; this is the driver's side
; of it, and between them the kernel has learned nothing about TCP.
;
; THE SHAPE. os8088 is the master and never receives unsolicited data
; (docs/NET-PLAN.md 1.3), so a socket verb is one request-and-reply over the
; cable and the far side answers it OUT OF STATE IT ALREADY HAS. It never
; goes and waits for the network on our behalf - which is exactly what makes
; every verb non-blocking without a queue, a mux, or a framing question about
; whose reply arrived. The TCP is the far side's; ours is seven letters.
;
; THE LETTERS ARE LOWERCASE, and that is not decoration. The one-byte command
; space is SHARED with block mode's four and file mode's fifteen (SPEC.md
; 62.10.1), and after those there were six free capitals for seven verbs - so
; the socket layer takes the lowercase half outright. It also makes a trace
; readable: `G` is a file read and `r` is a socket read.
;
; ONE WIRE, TWO TASKS. Until this, everything that touched the cable ran on
; the UI task inside drv_fs_call. A socket verb comes from a PACKAGE, and the
; whole polling design (netpkg.inc) wants that package's WORKER to be what
; asks - so two tasks that pre-empt each other can now both want the wire.
; [net_busy] is the mutex, taken with interrupts off (task_spawn's own lesson,
; SPEC.md 20.6), and the two sides are deliberately ASYMMETRIC:
;
;   a FILE verb waits for it, briefly, because it is the user's click and has
;   nowhere to report "come back later" to;
;   a SOCKET verb never waits - it answers NETE_BUSY, which its caller is
;   already written to poll through.
;
; That asymmetry is what stops a worker on a 274 ms transfer turning every
; Disk window click into an error, without ever letting a package block the
; interface.
; =============================================================================

%include "nwire.inc"            ; the alphabet, SHARED with the far end

; -----------------------------------------------------------------------------
; net_pkg - DSV_PKGCALL: a package's socket verb (SPEC.md 20.11)
; in:  BL = a NETV_*, BH = DRVC_NET, ES = THE CALLING PACKAGE'S SEGMENT
; out: AX and CX are the answer; BX, DX, SI, DI come back the caller's.
;      CF=1 and AX = a NETE_*
;
; NETV_STATE is answered before anything else because it is the one verb that
; touches no wire: a package asks it to find out whether there is a link at
; all, and refusing it for want of one would make it useless.
; -----------------------------------------------------------------------------
net_pkg:
    cmp bl, NETV_IDENT
    je  nsk_ident               ; ...ANSWERED FIRST AND WITH NO CONDITIONS. It
                                ; is a question about which DRIVER this is, so
                                ; a link that is down, a partner that is old
                                ; and a cable that was never plugged in are all
                                ; irrelevant to it (SPEC.md 20.11.1)
    cmp bl, NETV_STATE
    je  nsk_state
    cmp bl, NETV_MAX
    ja  nsk_everb
    cmp byte [net_state], NS_LINKED
    jne .nolink
    cmp byte [lp_ver], NET_VER_SOCK
    jb  .old
    call nsk_claim
    jc  .busy
    call nsk_do
    mov byte [net_busy], 0      ; ...a `mov` touches no flags, so the verb's
    ret                         ; own CF and AX reach the package unchanged
.nolink:
    mov ax, NETE_NOLINK
    stc
    ret
.old:
    mov ax, NETE_REFUSED        ; a partner that serves files and not sockets
    stc                         ; (netpkg.inc's NSTF_SOCK, seen from here)
    ret
.busy:
    mov ax, NETE_BUSY
    stc
    ret

nsk_everb:
    mov ax, NETE_VERB
    stc
    ret

; --- and one that SUCCEEDS by doing nothing ---------------------------------
; NETV_BULK is a question about performance, not a request for a capability:
; the cable has one buffer size and no pool to take a bigger ring out of, so
; the honest answer is "noted" rather than NETE_VERB. A package that treated a
; refusal as fatal would stop working over the cable for having asked.
nsk_ok:
    xor ax, ax
    clc
    ret

; A TABLE AND NOT A LADDER, and the reason is the 8086 rather than taste: a
; `je` is a SHORT jump and the verbs are several hundred bytes below here, so
; a compare ladder would not assemble at all. BX is free to spend - BL was the
; verb and BH the class, and both are answered by the time we are here - and
; BP is free for the same reason net_fgate may use it.
nsk_do:
    xor bh, bh                  ; BX = the verb, bounded by net_pkg above
    shl bx, 1
    mov bp, [nsk_tab+bx]
    jmp bp                      ; ...and the verb's own `ret` goes to net_pkg

nsk_tab:
    dw nsk_ident                ; NETV_IDENT  - answered before this, and both
    dw nsk_state                ; NETV_STATE    are here so the table is
                                ;               indexed by the verb rather
                                ;               than by verb-1
    dw nsk_open                 ; NETV_OPEN
    dw nsk_status               ; NETV_STATUS
    dw nsk_send                 ; NETV_SEND
    dw nsk_recv                 ; NETV_RECV
    dw nsk_close                ; NETV_CLOSE
    dw nsk_listen               ; NETV_LISTEN
    dw nsk_accept               ; NETV_ACCEPT
    dw nsk_everb                ; NETV_RESOLVE - reserved in the numbering,
                                ; absent from the driver, and a cell here
                                ; rather than a hole so the bound above is the
                                ; only thing that has to be right
    dw nsk_addr                 ; NETV_ADDR
    dw nsk_close                ; NETV_ABORT - **THE CABLE'S ABORT IS A
                                ; CLOSE**, and honestly so. The verb exists
                                ; because a TCP socket's slot outlives its
                                ; close by a FIN handshake (SPEC.md 77.25);
                                ; over the cable there is no handshake to
                                ; outlive - the partner frees its end when
                                ; told - so the two are already the same
                                ; operation and a second body would be a
                                ; second thing to keep in step for nothing
    dw nsk_noprof               ; NETV_PROF - **THE CABLE HAS NO PROFILER**
                                ; (SPEC.md 72.15). It shares every line of the
                                ; stack and none of the instrument: the stages
                                ; are bracketed in ETHER.DRV's own wrappers,
                                ; and the DOS partner that runs the other half
                                ; of this transport carries none of them. A
                                ; refusal is the only honest answer - a table
                                ; of zeroes would read as a stack doing no
                                ; work at all
    dw nsk_everb                ; NETV_SKTAB - **THE CABLE HAS NO SOCKET
                                ; TABLE** (SPEC.md 72.20). Its handles are the
                                ; partner's file cursors, not TCP records:
                                ; there is no receive ring to report a depth
                                ; for and no TIME-WAIT to catch stranded, so
                                ; every column the verb exists to show would
                                ; be a zero that means "not applicable" and
                                ; reads as "healthy". A refusal is the honest
                                ; answer, and NETV_PROF above refuses for the
                                ; same reason
    dw nsk_ok                   ; NETV_BULK - **THE CABLE HAS ONE BUFFER SIZE**
                                ; (SPEC.md 72.21.1). There is no ring pool to
                                ; take a big ring out of, so the wish is
                                ; granted by being ignored - and that is a
                                ; SUCCESS rather than a refusal, because
                                ; netpkg.inc says a caller may always ask and
                                ; a package that treated NETE_VERB as fatal
                                ; would stop working over the cable for
                                ; asking a question about performance
%if ($ - nsk_tab) / 2 != NETV_MAX + 1
  %error "net: nsk_tab does not cover NETV_0..NETV_MAX"
%endif

; -----------------------------------------------------------------------------
; nsk_claim / net_fclaim - the one wire, from the two tasks that want it
;
; Both take [net_busy] under one `cli`, because this is the first time two
; tasks can be inside the driver at once. The difference is entirely in what
; they do when it is taken - see the file header.
; clobbers: flags (CF=1 = it is somebody else's)
; -----------------------------------------------------------------------------
nsk_claim:
    pushf
    cli
    cmp byte [net_busy], 0
    jne .no
    mov byte [net_busy], 1
    popf                        ; ...AND THE ANSWER AFTER THE popf, always:
    clc                         ; popf restores the CALLER'S carry along with
    ret                         ; its interrupt flag, so a `clc` before it is
.no:                            ; a result the caller never sees
    popf
    stc
    ret

NET_FSPIN   equ 4096            ; yields a FILE verb will wait. It has to cover
                                ; the longest socket exchange, which is
                                ; NET_SKMAX at the cable's 3,741 bytes/second
                                ; = ~274 ms (PERFORMANCE.md Set 39); a yield
                                ; is a full task switch at 693 us, so this is
                                ; several seconds of ceiling for a wait that
                                ; is normally one or two switches.
                                ;
                                ; IT CANNOT DEADLOCK, and the reason is worth
                                ; stating: the holder is a package's worker
                                ; INSIDE a driver call, where it is not
                                ; drawing and wants no gfx lock - so yielding
                                ; while holding that lock lets it finish. A
                                ; socket verb that took the gfx lock would
                                ; turn this into a deadlock immediately, which
                                ; is why netpkg.inc says none of them does.

net_fclaim:
    push cx
    mov cx, NET_FSPIN
.spin:
    call nsk_claim
    jnc .got
    call OSAPI_TASK_YIELD
    loop .spin
    pop cx
    stc
    ret
.got:
    pop cx
    clc
    ret

; -----------------------------------------------------------------------------
; net_fgate - every FSV_* verb, wrapped so the wire is ours for its whole run
; in:  BP = the verb's body; everything else is the verb's own
; out: the verb's, or CF=1 with AX = FERR_IO when the wire never came free
;
; THE GATE IS AT THE TABLE and not inside the verbs, which is what makes it
; auditable: net_fsv's fifteen cells point at fifteen six-byte thunks, so a
; verb added later CANNOT be added ungated - it has no cell to be reached
; through until one is written, and writing one is writing a thunk. Putting
; the release inside each verb instead would have meant fifteen exits to get
; right, three of which belong to helpers that run in the MIDDLE of a verb
; (net_rdrun, net_rdsink, net_wrrun) and would have released the wire
; half-way through a transfer.
;
; BP is free by the time we are here: drv_fs_call passes the verb in it and
; has already spent it, and the two verbs that use BP themselves (net_read,
; net_readat) push it.
; -----------------------------------------------------------------------------
net_fgate:
    call net_fclaim
    jc  .busy
    call bp
    mov byte [net_busy], 0      ; no flags: the verb's CF is the answer
    ret
.busy:
    mov ax, FERR_IO
    stc
    ret

%macro NFSGATE 1                ; one six-byte thunk per FSV_* cell
nfs_%1:
    mov bp, net_%1
    jmp net_fgate
%endmacro

    NFSGATE list
    NFSGATE chdir
    NFSGATE stat
    NFSGATE read
    NFSGATE write
    NFSGATE append
    NFSGATE readat
    NFSGATE delete
    NFSGATE rename
    NFSGATE mkdir
    NFSGATE rmdir
    NFSGATE dfree
    NFSGATE enum
    NFSGATE copy
    NFSGATE rmtree

; -----------------------------------------------------------------------------
; nsk_ident - NETV_IDENT: WHICH DRIVER IS THIS? (SPEC.md 20.11.1)
; out: AX = NET_SIG, CF=0. Always, whatever the cable is doing.
;
; It exists because BH is a CLASS and a class is a publication SLOT: NET.DRV
; and RAMDISK.DRV are both DRVC_FILE, so a package naming that class reaches
; whichever is attached, and their verb spaces are different things at the
; same numbers. Without this, `open a socket` handed to the RAM disk is
; `upper-case this string` and the length comes back as a handle.
; -----------------------------------------------------------------------------
nsk_ident:
    mov ax, NET_SIG
    clc
    ret

; -----------------------------------------------------------------------------
; nsk_state - NETV_STATE: what a package can find out with no wire at all
; out: AL = NSTF_* bits, AH = free handles, CF=0 always
; -----------------------------------------------------------------------------
nsk_state:
    push bx
    push cx
    xor al, al
    cmp byte [net_state], NS_NOPORT
    je  .count
    or  al, NSTF_PORT
    cmp byte [net_state], NS_LINKED
    jne .count
    or  al, NSTF_LINK
    cmp byte [lp_ver], NET_VER_SOCK
    jb  .count
    or  al, NSTF_SOCK
.count:
    xor ah, ah
    mov cx, NET_SOCKS
    xor bx, bx
.each:
    cmp byte [net_sk+bx], NSK_FREE
    jne .next
    inc ah
.next:
    inc bx
    loop .each
    pop cx
    pop bx
    clc
    ret

; -----------------------------------------------------------------------------
; nsk_hchk - AL = a handle a package handed us: is it one of ours?
; out: CF=0 and BX = its row index; CF=1 and AX = NETE_HANDLE
; clobbers: BX, flags (and AX on the failure path)
; -----------------------------------------------------------------------------
nsk_hchk:
    cmp al, 1
    jb  .no
    cmp al, NET_SOCKS
    ja  .no
    mov bl, al
    dec bl
    xor bh, bh
    cmp byte [net_sk+bx], NSK_FREE
    je  .no
    clc
    ret
.no:
    mov ax, NETE_HANDLE
    stc
    ret

; -----------------------------------------------------------------------------
; nsk_take - the far side answered with a handle: record it, or refuse
; in:  AL = the handle byte off the wire, AH = the state to record
; out: CF=0 and AX = the handle; CF=1 and AX = NETE_IO for one out of range
;
; EVERYTHING OFF THE WIRE IS HOSTILE (docs/NET-STACK-PLAN.md 8.5), and a
; handle is the cheapest thing to get wrong: this array is NET_SOCKS bytes
; and an unchecked index writes somewhere else in the driver's own segment.
; -----------------------------------------------------------------------------
nsk_take:
    push bx
    cmp al, 1
    jb  .bad
    cmp al, NET_SOCKS
    ja  .bad
    mov bl, al
    dec bl
    xor bh, bh
    mov [net_sk+bx], ah
    xor ah, ah
    pop bx
    clc
    ret
.bad:
    pop bx
    call nsk_io                 ; a far side inventing handles is one we can
    ret                         ; no longer follow. CALL and not jmp: every
                                ; caller of this has a stack frame of its own
                                ; to unwind, and a tail jump out of a helper
                                ; would return past their pops

; --- nsk_drop - forget every handle. A fresh link has none and a dead one's
; are gone with it, so both edges come through here.
nsk_drop:
    push bx
    push cx
    mov cx, NET_SOCKS
    xor bx, bx
.z:
    mov byte [net_sk+bx], NSK_FREE
    inc bx
    loop .z
    pop cx
    pop bx
    ret

; --- nsk_io - the wire went away mid-command. ONE exit, so every verb's
; failure path is the same three things: drop the link, forget the handles,
; and say so.
nsk_io:
    call net_lost
    call nsk_drop
    mov ax, NETE_IO
    stc
    ret

; --- nsk_cmd - the letter plus one handle byte, which is five verbs' opener
; in:  AL = the letter, AH = the handle. out CF=1 = the wire died
nsk_cmd:
    mov [net_cmdip], al
    call lp_sbyte
    jc  .bad
    mov al, ah
    call lp_sbyte
.bad:
    ret                         ; CF is lp_sbyte's, which is the answer

; --- nsk_cap - CX capped at NET_SKMAX, because a package may ask for anything
nsk_cap:
    cmp cx, NET_SKMAX
    jbe .ok
    mov cx, NET_SKMAX
.ok:
    ret

; =============================================================================
; THE VERBS
; =============================================================================

; -----------------------------------------------------------------------------
; NETV_OPEN - in ES:SI = a NUL host, CX = the port; out AX = a handle
;
; The host is sent as a FIXED NET_HOSTMAX-byte field, NUL and padding
; included. RESOLUTION IS THE FAR SIDE'S: a name goes across as a name and the
; lookup happens inside NSK_CONNECT, which is why there is no NETV_RESOLVE.
; -----------------------------------------------------------------------------
nsk_open:
    push bx
    push dx
    push si
    push di
    mov [net_sport], cx
    mov al, NW_OPEN
    mov [net_cmdip], al
    call lp_sbyte
    jc  .io
    mov ax, [net_sport]
    call lp_sword
    jc  .io
    mov cx, NET_HOSTMAX
    xor dl, dl                  ; 1 once the NUL has gone by: everything after
.name:                          ; it is padding, and the NUL itself is sent
    xor al, al
    or  dl, dl
    jnz .put
    mov al, [es:si]             ; ...THROUGH ES, which is the PACKAGE'S here
    inc si
    or  al, al
    jnz .put
    mov dl, 1
.put:
    call lp_sbyte
    jc  .io
    loop .name
    mov ah, NSK_CONNECT         ; NOT connected: the far side has STARTED a
    call nsk_reply_h            ; connection, and NETV_STATUS is how it ends
    jmp short .out
.io:
    call nsk_io
.out:
    pop di
    pop si
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; NETV_LISTEN - in CX = a port; out AX = a listening handle
; -----------------------------------------------------------------------------
nsk_listen:
    push bx
    push dx
    push si
    push di
    mov [net_sport], cx
    mov al, NW_LISTEN
    mov [net_cmdip], al
    call lp_sbyte
    jc  .io
    mov ax, [net_sport]
    call lp_sword
    jc  .io
    mov ah, NSK_LISTEN
    call nsk_reply_h
    jmp short .out
.io:
    call nsk_io
.out:
    pop di
    pop si
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; NETV_ACCEPT - in AL = a listening handle; out AX = a NEW handle, or 0
;
; **0 WITH CF=0 IS THE ORDINARY ANSWER**, not an error: nobody has connected
; yet. A server polls this the way a client polls NETV_STATUS.
; -----------------------------------------------------------------------------
nsk_accept:
    push bx
    push dx
    push si
    push di
    call nsk_hchk
    jc  .out
    cmp byte [net_sk+bx], NSK_LISTEN
    jne .nothand
    mov ah, al
    mov al, NW_ACCEPT
    call nsk_cmd
    jc  .io
    mov ah, NSK_UP              ; an accepted socket is connected already -
    call nsk_reply_h            ; there is no handshake left to wait out
    jmp short .out
.nothand:
    mov ax, NETE_HANDLE         ; send/recv verbs on a listener, and accept on
    stc                         ; anything else, are the same mistake
    jmp short .out
.io:
    call nsk_io
.out:
    pop di
    pop si
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; NETV_ADDR - out ES:DI = four IP bytes; CF=1 / NETE_NOLINK if there is none
;
; **THE ADDRESS IS THE PARTNER'S AND THAT IS THE TRUE ANSWER**, not a stand-in
; for one we do not have. The DOS box owns the IP, an inbound connection
; arrives there and is forwarded down the cable, so the address an FTP client
; must dial for a passive transfer really is the far end's
; (docs/NET-STACK-PLAN.md 1.5). Reporting anything else would put an
; unreachable quad in a 227 reply.
;
; Gated on NET_VER_ADDR and not on nsk_do's NET_VER_SOCK: a version-2 partner
; serves every other verb perfectly and only this letter is new to it, and an
; unknown letter is a hung wire rather than a refusal (nwire.inc).
; -----------------------------------------------------------------------------
nsk_addr:
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [lp_ver], NET_VER_ADDR
    jb  .old
    mov al, NW_ADDR
    mov [net_cmdip], al
    call lp_sbyte
    jc  .io
    call lp_rbyte               ; the status...
    jc  .io
    mov [net_sst], al
    mov cx, 4                   ; ...and FOUR BYTES WHATEVER IT SAID, because
    mov bx, 0                   ; the frame is fixed (SPEC.md 62.10.1)
.rd:
    call lp_rbyte
    jc  .io
    mov [net_addr+bx], al
    inc bx
    loop .rd
    cmp byte [net_sst], 0
    jne .refused
    mov si, net_addr            ; staged, then handed over: the four bytes are
    mov cx, 4                   ; read one at a time off the wire and only a
    cld                         ; whole answer is worth copying out
    rep movsb
    xor ax, ax
    clc
    jmp short .out
.refused:
    mov al, [net_sst]
    xor ah, ah
    stc
    jmp short .out
.old:
    mov ax, NETE_NOLINK
    stc
    jmp short .out
.io:
    call nsk_io
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; nsk_reply_h - the status-plus-handle reply that OPEN, LISTEN and ACCEPT share
; in:  AH = the state to record for a handle that comes back non-zero
; out: CF=0 and AX = the handle (0 = ACCEPT's "nobody yet");
;      CF=1 and AX = the far side's own NETE_*
;
; BOTH BYTES ARE READ WHATEVER THE STATUS SAYS - the fixed frame again. A
; refusal that short-changed the frame would leave the far side driving a
; nibble at an end that had stopped listening, which ends the SESSION and not
; the command (SPEC.md 62.10.1).
; -----------------------------------------------------------------------------
nsk_reply_h:
    mov [net_sstate], ah        ; the state to record, banked: AH is about to
                                ; carry a reply. IN MEMORY AND NOT IN AH -
                                ; lp_rbyte's contract is about AL, and nothing
                                ; says the high half survives a nibble pair
    call lp_rbyte
    jc  .io
    mov [net_sst], al           ; the far side's status
    call lp_rbyte               ; ...and its handle, ALWAYS: the frame is fixed
    jc  .io                     ; whatever the status said
    cmp byte [net_sst], 0
    jne .refused
    or  al, al
    jz  .none                   ; ACCEPT with nobody waiting
    mov ah, [net_sstate]
    call nsk_take
    ret
.none:
    xor ax, ax
    clc
    ret
.refused:
    mov al, [net_sst]
    xor ah, ah
    stc
    ret
.io:
    call nsk_io
    ret

; -----------------------------------------------------------------------------
; NETV_STATUS - in AL = a handle; out AH = an NSK_*, CX = bytes readable now
; -----------------------------------------------------------------------------
nsk_status:
    push bx
    push dx
    push si
    push di
    call nsk_hchk
    jc  .out
    mov [net_shnd], al
    mov ah, al
    mov al, NW_STAT
    call nsk_cmd
    jc  .io
    call lp_rbyte               ; status
    jc  .io
    mov [net_sst], al
    call lp_rbyte               ; the state
    jc  .io
    mov [net_sstate], al
    call lp_rword               ; ...and how much is readable
    jc  .io
    mov cx, ax
    cmp byte [net_sst], 0
    jne .refused
    mov al, [net_sstate]
    mov [net_sk+bx], al         ; keep our own row current, so NETV_STATE's
    mov ah, al                  ; free count and nsk_hchk agree with the far
    mov al, [net_shnd]          ; side without a second opinion
    clc
    jmp short .out
.refused:
    mov al, [net_sst]
    xor ah, ah
    stc
    jmp short .out
.io:
    call nsk_io
.out:
    pop di
    pop si
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; NETV_SEND - in AL = handle, ES:SI = bytes, CX = how many; out CX = taken
;
; A ZERO-LENGTH SEND COSTS NO WIRE, which matters more than it looks: a
; polling loop that has nothing to send yet would otherwise spend a whole
; round trip per tick saying so.
; -----------------------------------------------------------------------------
nsk_send:
    push bx
    push dx
    push si
    push di
    call nsk_hchk
    jc  .out
    cmp byte [net_sk+bx], NSK_LISTEN
    je  .nothand
    call nsk_cap
    or  cx, cx
    jnz .go                     ; a NEAR jump, deliberately: `jcxz` is short
    jmp .none                   ; only and .none is past the whole body loop
.go:
    mov [net_slen], cx
    mov ah, al
    mov al, NW_SEND
    call nsk_cmd
    jc  .io
    mov ax, [net_slen]
    call lp_sword
    jc  .io
    mov cx, [net_slen]
.body:
    mov al, [es:si]             ; ES is the PACKAGE'S
    inc si
    call lp_sbyte
    jc  .io
    loop .body
    call lp_rbyte               ; status
    jc  .io
    mov [net_sst], al
    call lp_rword               ; ...and how many it actually took
    jc  .io
    mov cx, ax
    cmp byte [net_sst], 0
    jne .refused
    cmp cx, [net_slen]
    jbe .ok                     ; a far side claiming to have taken more than
    call nsk_io                 ; it was given is one we cannot follow
    jmp short .out
.ok:
    xor ax, ax
    clc
    jmp short .out
.none:
    xor cx, cx
    xor ax, ax
    clc
    jmp short .out
.nothand:
    mov ax, NETE_HANDLE
    stc
    jmp short .out
.refused:
    mov al, [net_sst]
    xor ah, ah
    stc
    jmp short .out
.io:
    call nsk_io
.out:
    pop di
    pop si
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; NETV_RECV - in AL = handle, ES:DI = a buffer, CX = its capacity
;             out CX = bytes delivered
;
; **CX = 0 IS NOT END-OF-STREAM** and netpkg.inc says so twice: it means
; nothing has arrived yet. The end is NSK_CLOSING with nothing readable, and
; a caller that reads a 0 as an end truncates every page that arrives in more
; than one piece.
; -----------------------------------------------------------------------------
nsk_recv:
    push bx
    push dx
    push si
    push di
    call nsk_hchk
    jc  .out
    cmp byte [net_sk+bx], NSK_LISTEN
    je  .nothand
    call nsk_cap
    or  cx, cx
    jnz .go                     ; a NEAR jump, deliberately: `jcxz` is short
    jmp .none                   ; only and .none is past the whole body loop
.go:
    mov [net_scap], cx
    mov ah, al
    mov al, NW_RECV
    call nsk_cmd
    jc  .io
    mov ax, [net_scap]
    call lp_sword
    jc  .io
    call lp_rbyte               ; status
    jc  .io
    mov [net_sst], al
    call lp_rword               ; ...and how much is coming
    jc  .io
    cmp ax, [net_scap]
    jbe .len                    ; MORE THAN WE ASKED FOR is the one thing that
    call nsk_io                 ; would write past the package's buffer, so it
    jmp short .out              ; is refused here and not trusted anywhere
.len:
    mov [net_slen], ax
    mov cx, ax
    jcxz .rdone
.body:
    call lp_rbyte
    jc  .io
    mov [es:di], al             ; ES is the PACKAGE'S
    inc di
    loop .body
.rdone:
    mov cx, [net_slen]
    cmp byte [net_sst], 0
    jne .refused
    xor ax, ax
    clc
    jmp short .out
.none:
    xor cx, cx
    xor ax, ax
    clc
    jmp short .out
.nothand:
    mov ax, NETE_HANDLE
    stc
    jmp short .out
.refused:
    mov al, [net_sst]
    xor ah, ah
    xor cx, cx
    stc
    jmp short .out
.io:
    call nsk_io
.out:
    pop di
    pop si
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; NETV_CLOSE - in AL = a handle
;
; THE ROW IS FREED WHATEVER HAPPENS, which is the contract netpkg.inc states:
; the handle is the package's no more, and a close that could fail would leave
; it with something it cannot use and cannot give back.
; -----------------------------------------------------------------------------
nsk_close:
    push bx
    push dx
    push si
    push di
    call nsk_hchk
    jc  .out
    mov byte [net_sk+bx], NSK_FREE
    mov ah, al
    mov al, NW_CLOSE
    call nsk_cmd
    jc  .io
    call lp_rbyte
    jc  .io
    xor ax, ax
    clc
    jmp short .out
.io:
    call nsk_io
.out:
    pop di
    pop si
    pop dx
    pop bx
    ret

; --- nsk_noprof - NETV_PROF over the cable ----------------------------------
nsk_noprof:
    mov ax, NETE_VERB
    stc
    ret
