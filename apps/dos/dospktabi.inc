; =============================================================================
; os8088 - apps/dos/dospktabi.inc
;
; THE PACKET DRIVER'S NUMBERS (SPEC.md 96.23) and the NETWORK PART's ABI
; (SPEC.md 96.23.11), in a file of their own for dosnetabi.inc's reason: the
; host's bss table is a %assign chain that cannot see an equ from a file
; included after it - and since 96.23.11 there are TWO assemblies that need
; them, apps/dos/dos.asm (which claims and sizes) and apps/dos/dosnetp.asm
; (the part, which runs). One file, so the two cannot disagree.
; =============================================================================

PKT_NHAND   equ 4                   ; handles. mTCP opens ONE (IP) and ARP
                                    ; rides the same one; four is room for a
                                    ; client that separates them and one more
PKT_HUSED   equ 0                   ; --- a handle row ---
PKT_HTYPE   equ 1                   ; word: the ethertype, big-endian as it
                                    ; sits on the wire. 0 = every frame
PKT_HRCVO   equ 3                   ; word: the client's receiver...
PKT_HRCVS   equ 5                   ; word: ...far
PKT_HSIZE   equ 7

PKT_VEC_LO  equ 0x60                ; the range the spec reserves, and which a
PKT_VEC_HI  equ 0x80                ; client walks looking for the signature

; --- what CF=1 means, in DH (Crynwr) -----------------------------------------
PKE_BADHAND equ 1
PKE_NOCLASS equ 2
PKE_NOTYPE  equ 3
PKE_NONUM   equ 4
PKE_BADTYPE equ 5
PKE_NOSPACE equ 9
PKE_TYPEUSED equ 10
PKE_BADCMD  equ 11
PKE_CANTSEND equ 12

; --- get_statistics' record: six DWORDS, in the Crynwr order -----------------
; These are the numbers mTCP's PKTTOOL prints, so they are kept for real
; rather than left at zero - and they used to be two DIAGNOSTIC word counters
; written over bytes_out and errors_in, which made a debugging aid into a
; wrong answer to a published call.
PKS_PIN     equ 0                   ; packets in  - frames handed to a client
PKS_POUT    equ 4                   ; packets out - frames a client sent
PKS_BIN     equ 8                   ; ...and their lengths, as the client
PKS_BOUT    equ 12                  ; asked for them rather than as padded
PKS_ERRIN   equ 16                  ; errors in: nothing here can report one -
                                    ; a frame the driver could not read never
                                    ; reaches us at all
PKS_DROP    equ 20                  ; dropped: no handle matched it, or the
                                    ; client refused the buffer

PKT_ETYPE   equ 12                  ; where the ethertype sits in a frame. The
                                    ; ABI publishes the header's SIZE and this
                                    ; is the one offset inside it a demux needs
PKT_STK     equ 1024                ; the tick poll's own stack
                                    ; (SPEC.md 96.23.4.1). The chain under it
                                    ; is OSAPI_DRV_CALL into the kernel, the
                                    ; driver's verb, ne_rx's DMA loop and then
                                    ; the CLIENT's receiver - and the last of
                                    ; those is the one we cannot measure, so
                                    ; this is cut generously rather than to a
                                    ; walked depth

; =============================================================================
; THE NETWORK CLAIM (SPEC.md 96.23.7) - every byte the wire needs, and NONE
; of it in this package's bss
; =============================================================================
; **A BSS BYTE IS A BYTE THE DOS PROGRAM CANNOT HAVE.** §96.3 hands the
; program OSAPI_MEM_AVAIL's whole answer, and this package's image + bss is
; claimed off the same heap first - so every byte declared below the DBSS
; macro comes straight out of the arena, on every machine, whether or not
; there is a wire to use it. The frames, the staging copy and the poll's
; private stack were 4,052 bytes of exactly that: on the 128KB floor machine
; that is 8% of the arena spent on a driver kern_small does not even ship
; (SPEC.md 24.5).
;
; So they live in a claim `dos_pkt_bufs` takes only when net_find answers -
; and the shape that makes it nearly free is that **DS IS THIS CLAIM inside
; every dn_* routine** (SPEC.md 96.26.6). dosnet.inc's premise was already
; "one segment, no segment override", so pointing that one segment at the
; claim instead of at ourselves leaves all 78 of its frame references
; untouched: what changes is the VALUE of the symbols, not the code.
;
; It also collapses two branches that were only ever about which buffer:
; dos_pkt_deliver and dos_pkt_poll each had a `[dos_pkt_xl]` arm choosing
; between the claim and our bss, and both arms are now the same claim.
; **THE ORDER IS THE CARD'S NEEDS FIRST**, so that the two routes' claims
; nest: a card wants the received frame and the stack and nothing else, and
; putting the cable-only parts above them means the card claims 3KB rather
; than paying for a hole.
PKB_RX      equ 0                   ; NET_FRAME: the frame FOR the client -
                                    ; the card's received one, or the one the
                                    ; translation built
PKB_STK     equ 1536                ; PKT_STK bytes of private stack
PKB_STKTOP  equ PKB_STK + PKT_STK
PKB_TX      equ PKB_STKTOP          ; NET_FRAME: the CLIENT's own frame,
                                    ; staged - the CABLE PATH ALONE, because
                                    ; the card hands the client's buffer to
                                    ; the driver where it lies (96.23.8)
PKB_STATE   equ PKB_TX + 1536       ; ...and the translation's own state, laid
                                    ; out by dosnetabi.inc's DNB_*
PKT_RXOFF   equ PKB_RX              ; the packet driver's own name for it,
                                    ; kept because SPEC.md 96.23.7 publishes
                                    ; that one

; **THE TWO ROUTES CLAIM DIFFERENT AMOUNTS**, which is the other half of not
; spending a byte that cannot be used: a card needs the received frame and the
; stack and nothing else, because send_pkt hands the client's own buffer to the
; driver where it lies (SPEC.md 96.23.8). The cable needs the staging frame and
; the whole translation state on top.
PKB_CARDKB  equ (PKB_STK + PKT_STK + 1023) / 1024
PKB_XLKB    equ (PKB_STATE + DNB_SIZE + 1023) / 1024

PKT_CLASS   equ 1                   ; DIX Ethernet (Blue Book), which is what
                                    ; an NE2000 is and what mTCP expects
PKT_TYPE    equ 1
PKT_FUNC    equ 2                   ; basic plus extended: set/get_rcv_mode
                                    ; and get_statistics are answered
PKT_VERSION equ 9

; =============================================================================
; THE NETWORK PART'S ABI (SPEC.md 96.23.11)
; =============================================================================
; --- the doors: the host far-calls claim:0 with AL = one of these ----------
PN_D_INIT   equ 0                   ; AH = the route (1 = the translation),
                                    ; BL = [net_cls], DX:SI = the environment
                                    ; rows (dn_lsn_init's OS88LISTEN=)
PN_D_START  equ 1                   ; dos_pkt_start
PN_D_POLL   equ 2                   ; dos_pkt_poll, for DHK_POLL
PN_D_SHUT   equ 3                   ; dn_shut, then dos_pkt_rawdrop
PN_D_N      equ 4

; --- the part's own cells, at PN_STATE_AT (= the image's length) ------------
; They were the host's HBSS rows; the host keeps only the far door and
; [dos_pkt_bseg] (the claim) and [dos_pkt_xl] (which route it sized for).
PNC_VEC     equ 0                   ; byte: the vector we took, 0 = none
PNC_RAW     equ 1                   ; byte: 1 = we hold NETV_RAW
PNC_BSY     equ 2                   ; byte: the poll's re-entrancy guard
PNC_MODE    equ 3                   ; byte: what set_rcv_mode was told
PNC_XL      equ 4                   ; byte: the route, handed in at PN_D_INIT
PNC_CLS     equ 5                   ; byte: [net_cls], handed in likewise
PNC_HTAB    equ 6                   ; PKT_NHAND * PKT_HSIZE
PNC_MAC     equ PNC_HTAB + PKT_NHAND * PKT_HSIZE
PNC_CVEC    equ PNC_MAC + 6         ; dword: the client receiver being called
PNC_CHAND   equ PNC_CVEC + 4        ; word: ...and the handle both calls carry
PNC_OLD08   equ PNC_CHAND + 2       ; dword: the tick we chain
PNC_SSS     equ PNC_OLD08 + 4       ; word: the program's stack across a poll
PNC_SSP     equ PNC_SSS + 2
PNC_CAN     equ PNC_SSP + 2         ; word: the canary under the private stack
PNC_CDS     equ PNC_CAN + 2         ; word: the client's DS (SPEC.md 96.23.9)
PNC_BSEG    equ PNC_CDS + 2         ; word: the claim's OWN segment, written at
                                    ; every door (the claim may have moved
                                    ; between two of them)
PNC_ESEG    equ PNC_BSEG + 2        ; word: the environment rows' segment...
PNC_EOFF    equ PNC_ESEG + 2        ; word: ...and offset
PNC_STAT    equ PNC_EOFF + 2        ; 24: get_statistics' six dwords
PN_STATE    equ (PNC_STAT + 24 + 15) & ~15  ; ...and the buffers start past
                                    ; it on a paragraph, so PKB_* stay as
                                    ; aligned as they were at offset 0

; --- the buffers' two sizes in BYTES, which the host adds to the row's len --
PKB_CARDB   equ PKB_STK + PKT_STK
PKB_XLB     equ PKB_STATE + DNB_SIZE

; --- the network claim's offsets (SPEC.md 96.26.6), at a base ---------------
; 0 in a build with the family inline (the claim holds the buffers alone) and
; PN_BUF in the part (the claim holds the code first, SPEC.md 96.23.11) - one
; list, so the two layouts cannot drift.
%macro PN_BUFSYMS 1
; --- the cable translation's, IN THE NETWORK CLAIM (SPEC.md 96.26.6) --------
; These are offsets into [dos_pkt_bseg] and NOT into our own segment, which is
; the whole of what made the move cheap: dosnet.inc already addressed all of
; them DS-relative with no override, so pointing DS at the claim leaves its
; seventy-eight frame references untouched and only these fourteen lines move.
dn_flows    equ %1 + PKB_STATE + DNB_FLOWS
dn_names    equ %1 + PKB_STATE + DNB_NAMES
dn_qname    equ %1 + PKB_STATE + DNB_QNAME
dn_pend     equ %1 + PKB_STATE + DNB_PEND
dn_lastip   equ %1 + PKB_STATE + DNB_LASTIP
dn_pseudo   equ %1 + PKB_STATE + DNB_PSEUDO
dn_rr       equ %1 + PKB_STATE + DNB_RR
dn_gwmac    equ %1 + PKB_STATE + DNB_GWMAC       ; copied out of the image by
dn_ourmac   equ %1 + PKB_STATE + DNB_OURMAC      ; dn_init (dn_mac_c below)
dn_lsn      equ %1 + PKB_STATE + DNB_LSN         ; the inbound listeners (96.26.8)
dn_lrr      equ %1 + PKB_STATE + DNB_LRR
dn_cip      equ %1 + PKB_STATE + DNB_CIP         ; ...and the client's own address
dn_frame    equ %1 + PKB_RX                      ; the frame we build FOR the
                                            ; client, which is also...
dos_pkt_rxs equ %1 + PKB_RX                      ; ...the one dos_pkt_deliver hands
                                            ; over, on EITHER route: the card
                                            ; writes the same offset through
                                            ; NETV_RAWRX, which is why both
                                            ; of that routine's arms collapsed
dos_pkt_txs equ %1 + PKB_TX                      ; and the client's own, staged
dos_pkt_stk_top equ %1 + PKB_STKTOP
%endmacro

; --- the environment rows' shape, which dn_lsn_init walks in the HOST's
; segment (SPEC.md 96.23.11). dos.asm owns DOS_ENVN and DOS_ENVBUF and fails
; its build if these stop agreeing with them; the part names its copies.
PN_ENVN     equ 4
PN_ENVBUF   equ 49
