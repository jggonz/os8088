; =============================================================================
; os8088 - drivers/ether/ethstate.inc
;
; THE STACK'S STATE - the card, the addresses, the sockets and the frames.
;
; Cut out of ether.asm so os88net.com can host the same stack (ethsock.inc's
; header). In a DRIVER all of it ships inside the image, because a driver's
; bss does (SPEC.md 51); in a .COM it is ordinary uninitialised data.
;
; The ne2000 words at the top are the CARD's and a packet-driver host leaves
; them alone - they cost 46 bytes there and keep this file one file.
; =============================================================================
; =============================================================================
; STATE - all of it in the image, because a driver's bss ships inside it
; =============================================================================
; --- where the addresses below CAME FROM ------------------------------------
; With [eth_mode], because they are one fact: it was ethcfg.inc's, and a
; program with no Control Panel page still has a mode.
ECM_DHCP    equ 0
ECM_MAN     equ 1

eth_base:   dw 0                ; the card's I/O base, 0 = none
eth_up:     db 0                ; ne_init has run
eth_word:   db 0                ; 1 = a 16-bit card (NE2000), read 8-bit anyway
eth_txpg:   db NE_TXPAGE        ; the ring, as ne_init picked it
eth_pstart: db NE_PSTART
eth_pstop:  db NE_PSTOP
eth_next:   db NE_PSTART + 1    ; the page WE will read next
eth_mac:    times 6 db 0
eth_bcmac:  times 6 db 0xFF     ; ...and the one every broadcast goes to
eth_prom:   times 32 db 0

; --- the addresses, four bytes each and always in WIRE order -----------------
; They are contiguous and eth_attach zeroes all sixteen in one call, so a
; second attach cannot inherit the first machine's network.
eth_ip:     times 4 db 0
eth_mask:   times 4 db 0
eth_gw:     times 4 db 0
eth_dns:    times 4 db 0
eth_mode:   db ECM_DHCP         ; ECM_*: where the four above came from
%ifdef ETH_IP
eth_static: db ETH_IP
            db ETH_MASK
            db ETH_GW
            db ETH_DNS
%endif
ip_bcast:   times 4 db 0xFF

; --- scratch -----------------------------------------------------------------
eth_pr:     db 0                ; the protocol of the frame being built
eth_dip:    times 4 db 0        ; ...and where it is really going
eth_ipid:   dw 1
eth_ph:     times 4 db 0        ; the pseudo-header's tail (ip_pseudo)
eth_ph2:    times 4 db 0        ; a 32-bit scratch for sequence arithmetic
eth_oflg:   db 0                ; the flags tcp_out is sending...
eth_flg:    db 0                ; ...and the ones tcp_seg received. TWO WORDS
                                ; AND NOT ONE: tcp_seg reads the received
                                ; flags after calling tcp_ack, and a shared
                                ; byte made a FIN look like whatever we last
                                ; sent
eth_adv:    db 0                ; did the last segment advance RCV.NXT?
eth_dlen:   dw 0
eth_tlen:   dw 0                ; the TCP segment, header included
eth_thl:    dw 0                ; ...its header length, options included
eth_seglen: dw 0                ; ...and its data
eth_udplen: dw 0                ; a datagram's payload, from the UDP header
eth_lportr: dw 0                ; the ports the frame in eth_rxb names
eth_rport:  dw 0
eth_now:    dw 0
eth_dl:     dw 0
eth_hnd:    db 0
eth_useg:   dw 0                ; the calling package's segment (eth_pkg)
eth_usi:    dw 0                ; ...and the offset it handed NETV_OPEN
eth_busy:   db 0                ; THE CARD'S MUTEX (SPEC.md 72.2.2). One byte,
                                ; and the first thing in this driver that two
                                ; tasks touch: eth_useg above is one word for
                                ; whichever package is inside, eth_rxb is one
                                ; frame, and the 8390's register file is PAGED
eth_lasterr: db 0
eth_lport:  dw 49152            ; the next ephemeral port

; --- counters, for the Control Panel page ------------------------------------
eth_nrx:    dw 0
eth_ntx:    dw 0
eth_ndrop:  dw 0
eth_novw:   dw 0
eth_nping:  dw 0
eth_nseg:   dw 0
eth_nretx:  dw 0
eth_ncall:  dw 0                ; package-door calls (eth_pkg)

; --- ARP ---------------------------------------------------------------------
arp_used:   times ARP_N db 0
arp_rr:     db 0
arp_ip:     times ARP_N * 4 db 0
arp_ha:     times ARP_N * 6 db 0

; --- DHCP and DNS ------------------------------------------------------------
dhcp_st:    db DH_IDLE
dhcp_try:   db 0
dhcp_tmo:   dw 0
dhcp_xid:   times 4 db 0
dhcp_off:   times 4 db 0
dhcp_srv:   times 4 db 0
dns_sk:     db 0xFF
dns_try:    db 0
dns_id:     dw 0
dns_lport:  dw 0
dns_tmo:    dw 0

; --- the sockets and the frames ---------------------------------------------
; **A DRIVER EMITS THESE AND A .COM RESERVES THEM.** A driver's bss ships
; inside its image (SPEC.md 51), so here they are 16,776 bytes of literal
; zeros and have to be; in a .COM they are 16,776 bytes of literal zeros ON A
; FLOPPY, which is 33 clusters of a 360KB disk spent saying nothing.
; ETH_NOEMIT is os88net.asm's, and it declares the same labels past the end of
; its image - where DOS has already given it the memory (SPEC.md 62.11.1).
;
; THE COST OF NOT EMITTING THEM IS THAT THEY ARRIVE DIRTY, and that is the
; caller's to pay: DOS zeroes nothing, which is PERFORMANCE.md's DIRTYRAM
; lesson in another place - a socket table full of rubbish is a table of
; sockets that are already open.
%ifndef ETH_NOEMIT
sk_tab:     times NET_SOCKS * SK_SZ db 0
sk_rxb:     times NET_SOCKS * SK_RXCAP db 0
sk_txb:     times NET_SOCKS * SK_TXCAP db 0
eth_rxh:    times 4 db 0        ; the card's own four-byte receive header
eth_rxb:    times NE_FRAME + 4 db 0
eth_txb:    times NE_FRAME + 4 db 0
%endif

