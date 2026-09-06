; =============================================================================
; os8088 - ETHER.DRV
;
; An NE1000/NE2000 card and a TCP/IP stack, answering the SAME socket verbs
; NET.DRV answers over a parallel cable (SPEC.md 62.11, drivers/net/netpkg.inc).
; That is the whole claim of docs/NET-STACK-PLAN.md stage E and it is the test
; of whether stage A's boundary was drawn in the right place: the browser and
; Telnet are not rebuilt against a new interface here, they ask for a socket
; and get one from a card instead of a cable.
;
; What DID have to change is one line of that question, and it is worth saying
; plainly rather than claiming a clean pass. A package names a publication
; CLASS, and this driver is in DRVC_NET where NET.DRV is in DRVC_FILE - it
; serves no volume, so it has no business in the file redirector's slot, and
; a machine may have both attached at once. So `NET_CLASS` stopped being a
; constant and became `[net_cls]`, a byte apps/os88sock.inc's `net_find` sets
; by asking each candidate class for NETV_IDENT. Three lines in each package,
; none of them about Ethernet.
;
; --- WHAT IS IN HERE --------------------------------------------------------
;   ne2000.inc  the 8390: probe, ring, transmit         (SPEC.md 72.2)
;   inet.inc    ARP, IP, ICMP echo, UDP, checksums      (SPEC.md 72.3)
;   tcp.inc     TCP, stop-and-wait, four connections    (SPEC.md 72.4)
;   dns.inc     DHCP for our address, DNS for theirs    (SPEC.md 72.5)
;   etherui.inc the Control Panel page                  (SPEC.md 72.6)
;
; --- IT POLLS, AND HOOKS NOTHING --------------------------------------------
; No interrupt vector, no IRQ line, no DMA channel. The card's receive ring
; holds about ten frames and is drained from inside the socket verbs a package
; already calls once a tick (SPEC.md 72.2.1). DRVV_ATTACH is therefore
; all-or-nothing for free: there is nothing to unhook, so the refusal path has
; nothing to undo.
;
; --- THE ONE BLOCKING THING IS DHCP, AT DRVV_READY --------------------------
; Every socket verb is non-blocking because netpkg.inc says so. Acquiring an
; address is not a socket verb: it happens once, at attach, where the machine
; is already waiting for a driver to load off a floppy - so it spins the pump
; for up to DHCP_WAIT ticks and gives up rather than making every later call
; carry a "not configured yet" state nothing would poll.
; =============================================================================

%define NETPKG_DRIVER           ; netpkg.inc's constants, not its client half
%include "os88drv.inc"
%include "netpkg.inc"

    OS88_DRIVER 'ether', DRVC_NET, eth_entry

DHCP_WAIT   equ 110             ; ticks - six seconds for the whole exchange,
                                ; which is four DISCOVERs at DHCP_TMO

; -----------------------------------------------------------------------------
; The service table (SPEC.md 51.2). Only four cells: the page, the name, and
; the package door - this driver serves no volume and no sector.
; -----------------------------------------------------------------------------
eth_svc:
    dw 0                        ; DSV_CAPS
    dw 0                        ; DSV_FM
    dw 0                        ; DSV_STREAM
    dw 0                        ; DSV_TICK
    dw 0                        ; DSV_RELINST
    dw eth_name                 ; DSV_NAME
    dw 0                        ; DSV_TONE
    dw 0                        ; DSV_TIERS
    dw 0                        ; DSV_BLK
    dw eth_cpname               ; DSV_CPNAME
    dw eth_cp_paint             ; DSV_CPPAINT
    dw eth_cp_click             ; DSV_CPCLICK
    dw eth_cp_close             ; DSV_CPCLOSE - the panel has gone, so the
                                ; Setup window goes with it and its setting
                                ; reaches the disk (SPEC.md 31.8, 72.7)
    dw 0                        ; DSV_FS      - no volume: this driver serves
                                ;               neither files nor sectors
    dw 0                        ; DSV_CPKEY   - the page takes no keys; the
                                ;               typing is SPEC.md 72.7's own
                                ;               window, which has a W_ONKEY
    dw eth_cp_up                ; DSV_CPUP    - both buttons fire on the
    dw eth_cp_drag              ; DSV_CPDRAG    RELEASE, and the pressed one
                                ;               follows the pointer between
                                ;               the two edges (SPEC.md 13.8.4)
    dw eth_pkg                  ; DSV_PKGCALL - the sockets (SPEC.md 20.11)
    times DSV_SIZE - ($ - eth_svc) db 0
                                ; ...AND THE PAD. drv_publish copies DSV_SIZE
                                ; bytes whatever this table's length is, so a
                                ; table that stops short publishes whatever
                                ; FOLLOWS it as a cell - which is how
                                ; ramdisk.asm once handed the kernel its file
                                ; lister as a mouse handler. An exact table is
                                ; exact until DSV_SIZE next grows; the pad is
                                ; what makes it stay exact.

eth_name:   db 'Ethernet', 0
eth_cpname: db 'Ethernet', 0

; --- the I/O bases a card is jumpered to, in the order they are tried --------
; **0x360 IS DELIBERATELY LAST AND 0x378 IS NOT HERE AT ALL.** The probe WRITES
; to the reset port of whatever answers, and 0x378 is a parallel port - which
; on this machine is NET.DRV's cable (SPEC.md 62). 0x360 overlaps LPT2 on some
; boards, so it is tried only after everything else has said no.
eth_bases:  dw 0x300, 0x320, 0x340, 0x280, 0x2C0, 0x240, 0x360, 0
%ifdef ETH_BASE
eth_fixed:  dw ETH_BASE
%endif

%define WZ_CLASS DRVC_NET       ; ...and the desktop icon the Wire is reached
%include "wirezone.inc"         ; by (SPEC.md 26.7). EVERY BYTE OF IT IS OURS:
                                ; the kernel's zone is generic and carries no
                                ; glyph, so a machine with no card pays for
                                ; none of this

%include "ethprof.inc"          ; the stage timers (SPEC.md 72.15), and HERE
                                ; rather than beside the other includes below:
                                ; it defines the PROF_ macros, and eth_pkg uses
                                ; them a hundred lines before that block

; =============================================================================
; ENTRY
; =============================================================================
eth_entry:
    cmp al, DRVV_ATTACH
    je  eth_attach
    cmp al, DRVV_DETACH
    je  eth_detach
    cmp al, DRVV_READY
    je  eth_ready
    clc
    ret

; -----------------------------------------------------------------------------
; DRVV_ATTACH - find a card, and hook NOTHING
; out: CF=0 and SI = the service table; CF=1 = no hardware
; -----------------------------------------------------------------------------
eth_attach:
    push ax
    push bx
    push cx
    push di
%ifdef ETH_BASE
    mov ax, [eth_fixed]         ; `make ETHBASE=0x320`: one address and no
    mov [eth_base], ax          ; sweep, for a machine where the sweep would
    call ne_probe               ; poke something that objects
    jnc .found
    jmp .none
%endif
    mov bx, eth_bases
.try:
    mov ax, [bx]
    or  ax, ax
    jz  .none
    mov [eth_base], ax
    add bx, 2
    call ne_probe
    jc  .try
.found:
    call sk_claim               ; THE RINGS, before the card can deliver into
    jc  .noring                 ; them (SPEC.md 72.13). A card with no buffers
    call ne_init                ; cannot serve a socket, so this is the one
    mov byte [eth_up], 1        ; memory failure that refuses the attach
    mov di, eth_ip              ; a fresh attach starts with no address: a
    mov cx, 16                  ; second one must not inherit the first's
    call mem_zero
%ifdef ETH_IP
    mov si, eth_static          ; ...unless one was baked in (SPEC.md 72.5)
    mov di, eth_ip
    mov cx, 16
    call mem_copy
    mov byte [dhcp_st], DH_BOUND
    mov byte [eth_mode], ECM_MAN
%endif
    mov si, eth_svc
    pop di
    pop cx
    pop bx
    pop ax
    clc
    ret
.noring:
    mov word [eth_base], 0      ; the card is real and the memory is not: the
    mov byte [eth_up], 0        ; Drivers page reports a refusal either way,
    pop di                      ; and a stack with no window is not a stack
    pop cx
    pop bx
    pop ax
    stc
    ret
.none:
    mov word [eth_base], 0
    mov byte [eth_up], 0
    pop di
    pop cx
    pop bx
    pop ax
    stc                         ; DRVE_NOHW: the Drivers page reports it
    ret

; -----------------------------------------------------------------------------
; DRVV_READY - our services are published, so the API will take our calls
;
; This is where the address is acquired, and it is the ONE place in this
; driver that waits for the network. The alternative is every socket verb
; carrying a "no address yet" answer that nothing would poll for, on a machine
; that is already sitting through a floppy read (see the file header).
; -----------------------------------------------------------------------------
eth_ready:
    push ax
    push cx
    call wz_register            ; the desktop icon, NOW: our services are
                                ; published, so the API will take the call -
                                ; and above the [eth_up] test, because a card
                                ; that found no address is still a card and
                                ; the Wire's own window is what says so
                                ; (SPEC.md 26.7, docs/WIRE-PLAN.md §7)
    cmp byte [eth_up], 0
    je  .out
    call eth_cfg_load           ; a MANUAL address the user set and the panel
    pushf                       ; remembered (SPEC.md 72.7) needs no server
    call eth_buf_apply          ; ...and the socket buffer rung it remembered
    popf                        ; too, which is a setting and not an address:
    jnc .out                    ; it is wanted on every path out of the load
    cmp byte [dhcp_st], DH_BOUND
    je  .out                    ; ...and neither does a baked-in one
    call eth_dhcp_wait
.out:
    pop cx
    pop ax
    clc                         ; NEVER a failure: a card with no address is a
    ret                         ; card, and the page says which (SPEC.md 47)

; -----------------------------------------------------------------------------
; eth_dhcp_wait - ask for an address and spin the pump until there is one
;
; **NOTHING ELSE PUMPS.** The ring is drained from inside the socket verbs
; (SPEC.md 72.2.1), so a DHCP exchange started with no package polling would
; never finish - and the two places that start one are exactly the two where
; nobody is: DRVV_READY, and a user pressing Renew or choosing Automatic on
; the Control Panel. Both are moments the machine is already waiting at, so
; this is the one thing in this driver that blocks, and DHCP_WAIT bounds it.
;
; It was NOT here at first and the shape of the bug is worth keeping: the
; switch to Automatic worked, dhcp_begin sent a DISCOVER, and the page then
; sat on `none` for ever. It self-healed the moment any package called a
; socket verb, because every verb pumps - so it looked like the Control Panel
; being broken and the browser being fine.
; -----------------------------------------------------------------------------
eth_dhcp_wait:
    push ax
    call OSAPI_GET_TICKS
    add ax, DHCP_WAIT
    mov [eth_dl], ax
.begin:
    call eth_claim              ; the DISCOVER is a frame like any other, so it
    jnc .go                     ; goes out under the claim too - and a worker
    call OSAPI_GET_TICKS        ; that holds it is milliseconds away, with the
    sub ax, [eth_dl]            ; same deadline bounding even that
    js  .begin
    mov byte [dhcp_st], DH_FAIL
    jmp short .done
.go:
    call dhcp_begin
    mov byte [eth_busy], 0
.spin:
    call eth_claim              ; ONE PUMP AT A TIME and never held across the
    jc  .look                   ; spin: this is the UI task sitting here for up
    call eth_pump               ; to DHCP_WAIT ticks, and a package's worker
    mov byte [eth_busy], 0      ; would get NETE_BUSY for every one of them
.look:
    cmp byte [dhcp_st], DH_BOUND
    je  .done
    cmp byte [dhcp_st], DH_FAIL
    je  .done
    call OSAPI_GET_TICKS
    sub ax, [eth_dl]
    js  .spin
    mov byte [dhcp_st], DH_FAIL
.done:
    pop ax
    ret


; -----------------------------------------------------------------------------
; DRVV_DETACH - cannot fail (SPEC.md 51.6 rule 1)
; -----------------------------------------------------------------------------
eth_detach:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call wz_withdraw            ; **THE DESKTOP ICON FIRST** (SPEC.md 26.7):
                                ; its paint verb lives in the image the kernel
                                ; is about to free, and the zone is redrawn
                                ; out of ui_task's next pass
    mov byte [eth_busy], 1      ; **TAKEN AND NOT ASKED FOR** (SPEC.md 72.2.2):
                                ; a detach cannot fail and cannot wait, so it
                                ; takes the card outright and every package
                                ; verb from here answers NETE_BUSY - which is
                                ; the honest thing for a driver on its way out
    call eth_cfg_reap           ; **BEFORE anything else.** The window's
                                ; dispatcher is ours and the kernel is about to
                                ; free the image it lives in (SPEC.md 72.7)
    cmp byte [eth_up], 0
    je  .out
    call eth_dropall
    call ne_stop
    mov byte [eth_up], 0
    call sk_release             ; ...and the rings go back: a driver's claims
                                ; are not an instance's, so nothing frees them
                                ; for us (SPEC.md 72.13)
.out:
    mov byte [eth_busy], 0      ; ...and the flag does not outlive the driver:
                                ; a claim left standing would refuse every
                                ; verb of the next attach
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; eth_dropall - every connection goes, and the peers are told
;
; A RESET rather than a FIN, because detach cannot wait for anything: a FIN
; needs an acknowledgement and there will be no driver here to receive it.
; -----------------------------------------------------------------------------
eth_dropall:
    push ax
    push bx
    push cx
    push dx
    xor dl, dl
    mov cx, NET_SOCKS
.l:
    mov al, dl
    call sk_ptr
    cmp byte [bx+SKO_TS], TS_ESTAB
    jb  .kill
    mov al, TF_RST | TF_ACK
    push cx
    xor cx, cx
    call tcp_out
    pop cx
.kill:
    mov byte [bx+SKO_TS], TS_CLOSED
    mov byte [bx+SKO_ST], NSK_FREE
    inc dl
    loop .l
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE PACKAGE DOOR - the socket verbs (netpkg.inc)
; =============================================================================
; in:  BL = the verb, ES = the CALLING PACKAGE's segment
; out: per verb; CF=1 with AX = a NETE_*
;
; **ES IS BANKED AND THEN REPLACED.** Every address in this driver is a DS one
; and several routines here would otherwise be the only code in the file that
; had to care which of three things ES meant. usr_read/usr_write are the two
; places the caller's segment is reached, and they take it out of [eth_useg].
; ES is restored on the way out, because a package's own ES is its own.
; -----------------------------------------------------------------------------
eth_pkg:
    push es
    push bx
    push si
    push di
    inc word [eth_ncall]        ; how many times a package has asked us
                                ; anything - the counter that tells "the
                                ; driver answered wrongly" from "nobody is
                                ; calling it", which from inside the guest
                                ; look identical
    cmp bl, WZV_PAINT
    je  .wzpaint                ; ...AND SO IS THE DESKTOP ICON (SPEC.md 26.7),
                                ; above the claim and for the same reason: the
                                ; kernel calls it from a desktop repaint under
                                ; the gfx lock, which can land while another
                                ; task is inside a socket verb, and an icon
                                ; that answered NETE_BUSY would simply not be
                                ; drawn. It touches neither the card nor
                                ; [eth_useg]
    cmp bl, NETV_IDENT
    je  .ident                  ; ...ANSWERED FIRST AND WITH NO CLAIM: it is a
                                ; question about which DRIVER this is and it
                                ; touches neither the card nor [eth_useg]
                                ; (SPEC.md 20.11.1). NETV_STATE is NOT in that
                                ; company - eth_v_state pumps the ring
    call eth_claim              ; **THE CARD IS ONE CARD** (SPEC.md 72.2.2)
    jc  .busy
    mov [eth_useg], es
    push ds
    pop es
    cmp bl, NETV_MAX
    ja  .verb
    cmp byte [eth_up], 0
    jne .up
    cmp bl, NETV_STATE          ; a driver with no card still IDENTIFIES (it
    je  .up                     ; is answered above) and still answers
                                ; NETV_STATE, or a package cannot tell "no
                                ; card" from "wrong class" - and NETV_STATE is
                                ; the verb that exists to say so (netpkg.inc)
    mov ax, NETE_NOLINK
    jmp short .err
.up:
    xor bh, bh
    shl bx, 1
    PROF_B PF_VERB              ; the whole verb, so a profile can say how much
                                ; of a transfer was inside the driver at all
                                ; and how much was the package and the disk
                                ; above it (SPEC.md 72.15)
    call [eth_vtab+bx]          ; **THROUGH MEMORY, NOT THROUGH A REGISTER.**
                                ; `mov di, [eth_vtab+bx] / call di` is the
                                ; obvious form and it destroys DI - which is
                                ; NETV_RECV's DESTINATION OFFSET, so the reply
                                ; was written into the caller's segment at the
                                ; address of the verb that was about to run.
                                ; The browser's own code, five hundred bytes of
                                ; it, and everything after was a plausible
                                ; failure somewhere else. The kernel's own
                                ; drv_pkg_call_x had this bug in stage C
                                ; (SPEC.md 20.11) and it is the same one twice
    PROF_E PF_VERB, 0           ; ...and it preserves CF and AX, which are the
                                ; verb's answer on their way out
    jmp short .out
.verb:
    mov ax, NETE_VERB
.err:
    stc
.out:
    mov byte [eth_busy], 0      ; ...a `mov` touches no flags, so the verb's
                                ; own CF and AX reach the package unchanged
.pop:
    pop di
    pop si
    pop bx
    pop es
    ret
.ident:
    call eth_v_ident
    jmp short .pop
.wzpaint:
    call wz_paint               ; ...and it cannot fail: CF=0, and `pop` writes
    clc                         ; no flags
    jmp short .pop
.busy:
    mov ax, NETE_BUSY           ; NOT A FAILURE (netpkg.inc): the other task is
    stc                         ; in the driver, come back next tick
    jmp short .pop

; -----------------------------------------------------------------------------
; eth_claim - the card, from the two tasks that want it
;
; Test-and-set under one `cli`, because this is the first thing in the driver
; that two tasks can be inside at once (SPEC.md 72.2.2). The shape is
; drivers/net/netsock.inc's nsk_claim and deliberately so - it answers the
; same socket ABI, and a mutex invented twice is a mutex fixed once.
; clobbers: flags (CF=1 = it is somebody else's)
; -----------------------------------------------------------------------------
eth_claim:
    pushf
    cli
    cmp byte [eth_busy], 0
    jne .no
    mov byte [eth_busy], 1
    popf                        ; ...AND THE ANSWER AFTER THE popf, always:
    clc                         ; popf restores the CALLER'S carry along with
    ret                         ; its interrupt flag, so a `clc` before it is
.no:                            ; a result the caller never sees
    popf
    stc
    ret

eth_vtab:
    dw eth_v_ident              ; 0 NETV_IDENT
    dw eth_v_state              ; 1 NETV_STATE
    dw eth_v_open               ; 2 NETV_OPEN
    dw eth_v_status             ; 3 NETV_STATUS
    dw eth_v_send               ; 4 NETV_SEND
    dw eth_v_recv               ; 5 NETV_RECV
    dw eth_v_close              ; 6 NETV_CLOSE
    dw eth_v_listen             ; 7 NETV_LISTEN
    dw eth_v_accept             ; 8 NETV_ACCEPT
    dw eth_v_none               ; 9 NETV_RESOLVE - RESERVED (netpkg.inc)
    dw eth_v_addr               ; 10 NETV_ADDR
    dw eth_v_abort              ; 11 NETV_ABORT
    dw eth_v_prof               ; 12 NETV_PROF - the stage timers (72.15)
    dw eth_v_sktab              ; 13 NETV_SKTAB - the socket table (72.20)
    dw eth_v_bulk               ; 14 NETV_BULK  - give me the big ring (72.21.1)
eth_vtab_end:
%if (eth_vtab_end - eth_vtab) / 2 != NETV_MAX + 1
  %error "ether: the verb table and NETV_MAX disagree"
%endif

%include "ethusr.inc"           ; ...SHARED with the cable (62.11.1)

; --- NETV_IDENT --------------------------------------------------------------
; NET_SIG names the SOCKET ABI and not the driver, which is what lets a
; package find whichever of them is attached (netpkg.inc, apps/os88sock.inc).
eth_v_ident:
    mov ax, NET_SIG
    clc
    ret

eth_v_none:
    mov ax, NETE_VERB
    stc
    ret

; --- NETV_STATE --------------------------------------------------------------
; out AL = NSTF_*, AH = free handles
eth_v_state:
    push bx
    push cx
    push dx
    push si
    call eth_pump
    mov dl, NSTF_PORT           ; a card was found, or we would not be here
    mov si, eth_ip
    call ip_zero
    jz  .noip
    or  dl, NSTF_LINK | NSTF_SOCK
.noip:
    xor dh, dh
    xor al, al
    mov cx, NET_SOCKS
.l:
    call sk_ptr
    cmp byte [bx+SKO_ST], NSK_FREE
    jne .next
    inc dh
.next:
    inc al
    loop .l
    mov ax, dx                  ; AL = DL, the flags; AH = DH, the count
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret

%include "ethsock.inc"          ; NETV_OPEN..ACCEPT, SHARED with the
                                ; DOS end of the cable (62.11.1)
%include "ne2000.inc"
%include "inet.inc"
%include "tcp.inc"
%include "dns.inc"
; The two shared includes come FIRST here, where a package puts them last: a
; package must keep its header and icon block at fixed image offsets (SPEC.md
; 20.2) and a driver has neither, while ethcfg.inc's four line blocks are sized
; by OS88LINE_SZ and a `times` needs its constant already defined.
%include "os88ui.inc"           ; the standard control (SPEC.md 20.5.1)...
%include "os88line.inc"         ; ...and the one-line field the Setup window is
%include "etherui.inc"
%include "ethcfg.inc"           ; ...and the Setup window behind it (72.7)

%include "ethstate.inc"         ; ...and the state it all runs on
    OS88_DRV_END
