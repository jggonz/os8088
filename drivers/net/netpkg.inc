; =============================================================================
; NET.DRV's package verbs - sockets, behind OSAPI_DRV_CALL (SPEC.md 20.11)
;
; Included by BOTH ends, exactly as drivers/ramdisk/rdpkg.inc is: the driver
; that answers these and every package that asks. Nothing here is a kernel
; slot and nothing here is in SPEC.md's API table - which is the whole point
; of the door (docs/plans/completed/NET-STACK-PLAN.md 1.1). A sockets ABI in the kernel would
; be a contract os8088 owes for ever on a subject it has no code for; here it
; is the DRIVER's, it lives beside the driver, and it can be got wrong once
; and fixed.
;
;       %include "netpkg.inc"
;       ...
;       mov  bh, NET_CLASS              ; DRVC_FILE - see NET_CLASS below
;       mov  bl, NETV_OPEN
;       mov  si, my_host                ; ES:SI - ES is YOUR segment here
;       mov  cx, 80
;       call OSAPI_DRV_CALL
;       jc   .no                        ; AX = a NETE_* below
;
; **ES IS YOUR OWN SEGMENT** on the way in - the one driver entry point in the
; machine where it is not KERNEL_SEG - so a buffer you hand a verb is ES:SI or
; ES:DI and needs no staging.
;
; -----------------------------------------------------------------------------
; EVERY VERB IS NON-BLOCKING, AND THAT IS THE LOAD-BEARING RULE
; -----------------------------------------------------------------------------
; A file operation may freeze the machine for tens of milliseconds because
; that is what the floppy already does and SPEC.md 12.8's widget reports it.
; A connect is SECONDS and a recv may never complete at all, so a blocking
; socket call is a hang with the gfx lock held.
;
; So NETV_OPEN starts a connection and returns; NETV_STATUS says how it is
; getting on and how many bytes are readable NOW; NETV_RECV delivers what has
; already arrived and answers 0 when nothing has. **Waiting is the caller's**,
; and the ordinary shape is a worker task (SPEC.md 20.6) polling NETV_STATUS.
;
; What is bounded is the WIRE exchange, not the connection: every verb here is
; one request-and-reply over the cable, and the far side answers it out of
; state it already has. It never goes and waits for the network on our behalf.
;
; -----------------------------------------------------------------------------
; WORKER SAFETY
; -----------------------------------------------------------------------------
; **Every verb here is worker-safe** (SPEC.md 20.6 rule 7's amendment): none
; takes the gfx lock, raises sch_lock or touches a disk. That is deliberate
; and it is what the polling design needs - a verb a worker could not call
; would leave nothing to poll with.
;
; The wire is shared with the Link VOLUME's file verbs, which run on the UI
; task, so the driver arbitrates: a file verb waits briefly for a socket
; exchange to finish, and a socket verb that finds the wire busy answers
; **NETE_BUSY immediately** rather than waiting. NETE_BUSY is an ordinary
; answer and not a failure - poll again next tick.
; =============================================================================

; --- the verbs ---------------------------------------------------------------
NETV_IDENT   equ PKGV_IDENT     ; = 0. -> AX = 'NL', CF=0. **ASK IT FIRST.**
                                ; BH is a CLASS and a class is a publication
                                ; SLOT, not a driver: NET.DRV and RAMDISK.DRV
                                ; are BOTH DRVC_FILE, so NETV_OPEN and
                                ; RDPV_UPCASE are the same number at the same
                                ; address. A package that skipped this would
                                ; hand a hostname to the RAM disk and read the
                                ; length back as a socket handle - it
                                ; assembles, it runs, and it says nothing.
                                ; SPEC.md 20.11.1
NETV_STATE   equ 1              ; -> AL = NSTF_* bits, AH = free sockets.
                                ; CF=1 only when there is no driver; a machine
                                ; with a cable and no partner answers CF=0
                                ; with NSTF_LINK clear, which is the state a
                                ; package should SAY rather than refuse on
NETV_OPEN    equ 2              ; in  ES:SI = a NUL host - a name or a dotted
                                ;     quad, up to NET_HOSTMAX-1 bytes
                                ;     CX = the port
                                ; out AX = a handle, 1..NET_SOCKS
                                ;
                                ; RESOLUTION IS PART OF CONNECTING. A name is
                                ; passed through and the far side looks it up,
                                ; so a lookup is inside NSK_CONNECTING and
                                ; needs no verb of its own - see NETV_RESOLVE
NETV_STATUS  equ 3              ; in  AL = a handle
                                ; out AH = an NSK_* state, CX = bytes readable
                                ;     RIGHT NOW (0 is ordinary), AL = the
                                ;     handle back
NETV_SEND    equ 4              ; in  AL = handle, ES:SI = bytes, CX = how many
                                ; out CX = HOW MANY WERE TAKEN, which may be
                                ;     fewer than asked and may be 0. Advance
                                ;     your own pointer by it and come back
NETV_RECV    equ 5              ; in  AL = handle, ES:DI = a buffer, CX = its
                                ;     capacity
                                ; out CX = bytes delivered, 0 = nothing has
                                ;     arrived yet. **0 IS NOT END-OF-STREAM**:
                                ;     that is NSK_CLOSING with CX = 0 from
                                ;     NETV_STATUS, and reading a 0 as an end
                                ;     truncates every page that arrives in
                                ;     more than one piece
NETV_CLOSE   equ 6              ; in  AL = a handle. Always succeeds from the
                                ; caller's point of view: the handle is yours
                                ; no more whatever the far side says
NETV_LISTEN  equ 7              ; in  CX = a port -> AX = a LISTENING handle
NETV_ACCEPT  equ 8              ; in  AL = a listening handle
                                ; out AX = a NEW connected handle, or 0 with
                                ;     CF=0 for "nobody yet" - which is the
                                ;     ordinary answer and not an error.
                                ;
                                ; SEPARATE FROM LISTEN because a server must
                                ; stay reachable while it is serving: the
                                ; listener outlives the connection it accepts
NETV_RESOLVE equ 9              ; RESERVED, AND NOT IMPLEMENTED. It is in the
                                ; numbering so that adding it later does not
                                ; move anything below it. NETV_OPEN taking a
                                ; NAME subsumes it for everything in
                                ; docs/plans/completed/NET-STACK-PLAN.md's staging - a browser
                                ; and an FTP client both want the connection,
                                ; never the address - and a verb whose only
                                ; caller is hypothetical is a contract to get
                                ; wrong for free
NETV_ADDR    equ 10             ; out ES:DI = filled with FOUR bytes, this
                                ; machine's own IPv4 in wire order, and AX =
                                ; 0. CF=1 with AX = NETE_NOLINK when there is
                                ; no address yet (DHCP has not bound, or the
                                ; cable has no partner) - which is an ORDINARY
                                ; answer a server should SAY rather than a
                                ; failure, exactly as NETV_STATE's cleared
                                ; NSTF_LINK is.
                                ;
                                ; **THIS IS PASV'S VERB AND IT EXISTS FOR
                                ; NOTHING ELSE.** RFC 959's passive reply
                                ; carries the address the client should come
                                ; back to - `227 ... (h1,h2,h3,h4,p1,p2)` -
                                ; so a server that cannot name its own address
                                ; cannot answer PASV at all, and PASV is what
                                ; every FTP client made this century tries
                                ; first. Nothing else in this tree has ever
                                ; needed it: a client connecting OUT is told
                                ; where to go and never has to say where it
                                ; is, which is why five verbs' worth of
                                ; networking got built without one.
                                ;
                                ; docs/plans/completed/NET-STACK-PLAN.md 1.3 always said
                                ; NETV_STATE "reports whether there is a stack
                                ; and what its address is", and the half after
                                ; the `and` was never built - AL is flags and
                                ; AH is a free-handle count, with no room left
                                ; in either. So this is that promise kept at a
                                ; verb of its own rather than a contract
                                ; change at a live number (SPEC.md 20.8 rule
                                ; 4): appending cannot invalidate a caller,
                                ; and every package built before it keeps
                                ; working untouched.
                                ;
                                ; **ON THE CABLE THE ANSWER IS THE DOS BOX'S**
                                ; and that is the honest answer rather than a
                                ; convenient one (docs/plans/completed/NET-STACK-PLAN.md 1.5):
                                ; mTCP on the far end owns the IP, an inbound
                                ; connection arrives THERE and is forwarded,
                                ; so the address a client must dial really is
                                ; the partner's. A server reporting 0.0.0.0 or
                                ; its own nonexistent address would be telling
                                ; the client to connect somewhere nothing is
                                ; listening
NETV_ABORT   equ 11             ; AL = a handle: DROP IT NOW and give the slot
                                ; back, where NETV_CLOSE hands the connection
                                ; to TCP and keeps the slot until the FIN
                                ; handshake finishes (SPEC.md 77.25).
                                ;
                                ; **IT IS FOR A PEER THAT HAS ALREADY GONE.**
                                ; A close against one that never answers holds
                                ; its slot for the retransmit timer's whole
                                ; run - about a minute - and with NET_SOCKS
                                ; four, one of those is the difference between
                                ; a server that works and one that cannot open
                                ; a data connection until it expires. That is
                                ; a real failure the FTP server hit in the
                                ; field, not a tidiness.
                                ;
                                ; NO RST IS SENT, and the omission is
                                ; deliberate: building one needs the socket's
                                ; own sequence state where tcp_rst answers a
                                ; RECEIVED segment, and the peer this is used
                                ; against is not listening anyway. It learns
                                ; the connection is gone the next time it
                                ; sends, which tcp_rst then answers properly.
                                ;
                                ; NEVER on a transfer that SUCCEEDED: closing
                                ; the data connection is how FTP signals
                                ; end-of-file, and an abort there truncates
                                ; whatever the peer had not yet read.
                                ; out CF=0 always - aborting a handle that is
                                ; already gone is not an error
NETV_PROF    equ 12             ; THE STAGE PROFILER (SPEC.md 72.15), and it is
                                ; a verb rather than a build knob because the
                                ; only machine that can answer the question is
                                ; the 4.77MHz one somebody is holding.
                                ;
                                ;   AL = 0  stop, and bank the wall clock
                                ;   AL = 1  zero everything and start
                                ;   AL = 2  read: ES:DI = a buffer of at least
                                ;           PF_BLK bytes -> CX = how many were
                                ;           written
                                ;
                                ; CF=1 with AX = NETE_VERB from a driver that
                                ; has no profiler, which is the CABLE: it
                                ; shares every line of the stack and none of
                                ; the instrument (SPEC.md 62.11.1), and a
                                ; reporter must SAY that rather than draw a
                                ; table of zeroes.
; -----------------------------------------------------------------------------
; NETV_SKTAB - THE WHOLE SOCKET TABLE, for a human to look at (SPEC.md 72.20)
;
; in  ES:DI = a buffer, CX = its capacity
; out CX = bytes written (a whole number of SKI_SZ records), AL = NET_SOCKS
;
; **NETV_STATUS ANSWERS ABOUT A HANDLE YOU ALREADY HOLD**, which is exactly
; the wrong question when the complaint is "the server went quiet". A stranded
; socket belongs to nobody, so nobody can ask about it, and the three times
; this table has run dry (SPEC.md 72.4.2) it was invisible from inside every
; package on the machine. This answers about the TABLE.
;
; Read-only and side-effect free apart from the pump every verb does, so it is
; safe to call from a UI pass in the middle of a transfer - which is the only
; moment worth looking.
NETV_SKTAB   equ 13

; --- one record per slot, and it is FIXED so a reader can stride it ----------
SKI_ST      equ 0               ; byte: NSK_*, what a package would be told
SKI_TS      equ 1               ; byte: TS_*, what TCP is actually doing
SKI_FL      equ 2               ; byte: SKF_* - ORPH and ACPT are what say a
                                ;       slot is stranded rather than working
SKI_TRY     equ 3               ; byte: retransmits on the segment in flight
SKI_LP      equ 4               ; word: local port, host order
SKI_RP      equ 6               ; word: remote port, host order
SKI_RXN     equ 8               ; word: bytes sitting in the receive ring -
                                ;       "are we filling sockets and keeping
                                ;       them filled" is this number over time
SKI_TXN     equ 10              ; word: bytes waiting to go out
SKI_RXC     equ 12              ; word: the CAPACITY of the ring it holds, 0 =
                                ;       none. This is the tier (SPEC.md 72.21):
                                ;       a listener shows 0, a control
                                ;       connection SK_LEANRX and a transfer the
                                ;       bulk rung - so "did the data connection
                                ;       actually get the big ring" is a
                                ;       question the screen answers
SKI_SZ      equ 14

; The SKI_TS values a reader will want to test by name. They are tcp.inc's
; TS_* and this is a MIRROR rather than a second definition - tests/suite.py's
; `mirror` row is what keeps the two honest.
TS_CLOSED   equ 0
TS_CLOSEW   equ 10              ; ...and the TOP of the range, published so a
                                ; package with a name PER state can assert its
                                ; table is still the right length (apps/ftpd's
                                ; FD_NTS). That catches a state INSERTED or
                                ; REMOVED; a pure reorder renames rows and
                                ; nothing cheap catches it

; -----------------------------------------------------------------------------
; NETV_BULK - "this handle is going to move a FILE" (SPEC.md 72.21.1)
;
; in  AL = a handle. Always succeeds; CF=1 only for a handle that is not yours.
;
; The rings are a POOL now (72.21) with ONE big ring in it, because only a
; transfer needs a window worth the name - a listener needs no ring at all and
; a control connection carries short command lines. **The driver cannot tell
; those apart**, and first-come is actively wrong: an FTP control connection
; arrives before the data connection and would take the big ring.
;
; So the package says. Call it on a LISTENER and every connection that
; listener accepts inherits the wish - which is what lets a PASV listener hand
; the bulk ring to a data connection that does not exist yet. Call it on a
; freshly opened socket and it swaps, while both rings are still empty.
;
; **A REFUSAL IS SILENT AND NORMAL** (PERFORMANCE.md rule 6): if the bulk ring
; is already out, the caller keeps the lean one and the transfer is slower.
; Asking twice, or asking for something already held, is a no-op.
NETV_BULK    equ 14

NETV_MAX     equ 14             ; ...and anything above it is refused. A verb
                                ; table indexed by an unchecked verb is a wild
                                ; near call inside the driver

; --- NETV_STATE's bits -------------------------------------------------------
NSTF_PORT    equ 0x01           ; a parallel port was found
NSTF_LINK    equ 0x02           ; ...and a partner answered on it
NSTF_SOCK    equ 0x04           ; ...and that partner speaks the socket verbs.
                                ; A far side built before them answers the
                                ; file verbs perfectly well and refuses these,
                                ; so this is a fact about the PARTNER and not
                                ; about the cable

; --- a socket's state, as NETV_STATUS answers it -----------------------------
NSK_FREE     equ 0              ; not a handle of yours
NSK_CONNECT  equ 1              ; connecting - or still resolving the name
NSK_UP       equ 2              ; connected: send and recv both work
NSK_CLOSING  equ 3              ; THE PEER HAS CLOSED ITS END. Readable bytes
                                ; may still be waiting, so drain to CX = 0
                                ; before you believe the transfer is over -
                                ; this state is what "end of stream" means and
                                ; a 0 from NETV_RECV is not
NSK_CLOSED   equ 4              ; both ends done
NSK_FAILED   equ 5              ; it never connected, or it broke
NSK_LISTEN   equ 6              ; a listening handle: NETV_ACCEPT is what this
                                ; one is for and send/recv are refused on it

; --- what CF=1 means, in AX --------------------------------------------------
NETE_NODRV   equ 0              ; the kernel's own refusal: no DRVC_NET driver
                                ; is published, or it publishes no package
                                ; door. AX = 0 is the kernel's answer and not
                                ; the driver's, which is why it is the zero
NETE_NOLINK  equ 1              ; no partner on the cable
NETE_BUSY    equ 2              ; the wire is in use by the Link volume. NOT A
                                ; FAILURE - come back next tick
NETE_HANDLE  equ 3              ; not a handle of yours, or wrong kind for the
                                ; verb (send on a listener)
NETE_FULL    equ 4              ; all NET_SOCKS handles are in use
NETE_IO      equ 5              ; the link died mid-command. The driver has
                                ; dropped it; every handle is gone with it
NETE_REFUSED equ 6              ; the far side said no: a name that does not
                                ; resolve, a port nothing is listening on, a
                                ; partner too old for the socket verbs
NETE_VERB    equ 7              ; a verb this driver does not have

; --- who to ask, and how to know you reached them ----------------------------
; **THE CLASS IS DRVC_FILE AND NOT DRVC_NET**, which is the one thing here
; most likely to be got wrong from the name. NET.DRV became the FILE
; REDIRECTOR at SPEC.md 62.10 - it serves a volume by answering questions
; about files, so it occupies the DRVC_FILE publication slot and DRVC_NET is
; block mode's, which is retired. A driver publishes in ONE class; sockets are
; a second service of the same driver, not a second driver.
;
; And a class is a SLOT, so DRVC_FILE names whichever DRVC_FILE driver is
; attached - RAMDISK.DRV is the other one, and it publishes a package door
; too. NETV_IDENT is what tells them apart, and it is not optional.
; **AND SINCE SPEC.md 72 THERE ARE TWO CLASSES**, because ETHER.DRV answers
; these same verbs off a card and serves no volume, so it publishes in
; DRVC_NET. `NET_CLASS` is therefore no longer a constant a package can bake
; in: apps/os88sock.inc's `net_find` asks each candidate for NETV_IDENT and
; stores the winner, and NET_CLASS names that byte. Every `mov bh, NET_CLASS`
; written against the old constant assembles unchanged - the operand went from
; an immediate to a memory byte and nothing else moved.
;
; A DRIVER includes this file for the verb numbers and has no use for the
; question, so it defines NETPKG_DRIVER and gets the constant it publishes in.
%ifdef NETPKG_DRIVER
NET_CLASS    equ DRVC_FILE       ; the redirector's own slot, for net.asm
%else
%define NET_CLASS [net_cls]      ; ...and os88sock.inc's answer for a package
%endif
NET_SIG      equ 'NL'            ; what NETV_IDENT answers. **IT NAMES THE ABI
                                ; AND NOT THE DRIVER**: the card answers it too,
                                ; because what a package is asking is "do you
                                ; speak sockets", and the thing it has to be
                                ; told apart from is the RAM disk sharing a
                                ; publication slot (SPEC.md 20.11.1)

; --- limits ------------------------------------------------------------------
NET_SOCKS    equ 8              ; simultaneous handles. **A SLOT AND A RING
                                ; ARE DIFFERENT THINGS NOW** (SPEC.md 72.21),
                                ; and that is what lets this be eight: a slot
                                ; is SK_SZ = 128 bytes of bss, where a ring
                                ; pair is 9,216 of the claim - seventy-two to
                                ; one - and the two used to be welded together
                                ; by the ring's address being the slot NUMBER.
                                ;
                                ; Four was the FLOOR an FTP server needs
                                ; (docs/plans/completed/NET-STACK-PLAN.md 1.2) - control, data
                                ; and a listener that stays open across both -
                                ; and a floor is what you cannot build on: a
                                ; listener, a TIME-WAIT and a connection the
                                ; listener made that nobody has accepted yet
                                ; all competed for the same four. Eight costs
                                ; 512 bytes of bss over four and NOTHING of
                                ; the claim, because listeners now hold no
                                ; rings at all
NET_HOSTMAX  equ 64             ; the host field on the wire, INCLUDING the
                                ; NUL, and a fixed frame like every other one
                                ; here (SPEC.md 62.10.1)
NET_SKMAX    equ 1024           ; the most bytes one NETV_SEND or NETV_RECV
                                ; moves. Ask for more and you get this much
                                ; and the count says so.
                                ;
                                ; It is a LATENCY bound, not a memory one: the
                                ; cable runs at 3,741 bytes/second
                                ; (PERFORMANCE.md Set 39), so a full chunk is
                                ; ~274 ms of wire during which nothing else on
                                ; the machine moves. A page arrives in pieces
                                ; either way, and a caller that cannot cope
                                ; with a partial read is broken on a real
                                ; network as well
