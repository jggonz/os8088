; =============================================================================
; os8088 - nicne2k.inc: the NE1000/NE2000 as ETHER.DRV's NIC (SPEC.md 125.2)
;
; Everything the shared stack (inet/tcp/dns/ethsock/ethstate/etherui/ethdrv)
; asks of "the card" is a NIC_ / nic_ name; this file is where they mean the
; 8390. ne2000.inc is the driver proper and is included in place by ethdrv.inc
; as NIC_BACKEND. Nothing here emits a byte by itself: every macro is invoked
; at the place the old inline text stood, so ETHER.DRV is byte-identical.
; =============================================================================
%define NIC_BACKEND "ne2000.inc"
%define nic_init    ne_init
%define nic_stop    ne_stop
%define nic_rx      ne_rx
%define nic_tx      ne_tx           ; (ethprof.inc re-aims ne_tx at ne_tx_i)
%define NIC_FRAME   NE_FRAME

; --- NIC_DATA: the I/O bases a card is jumpered to ----------------------------
%macro NIC_DATA 0
; --- the I/O bases a card is jumpered to, in the order they are tried --------
; **0x360 IS DELIBERATELY LAST AND 0x378 IS NOT HERE AT ALL.** The probe WRITES
; to the reset port of whatever answers, and 0x378 is a parallel port - which
; on this machine is NET.DRV's cable (SPEC.md 62). 0x360 overlaps LPT2 on some
; boards, so it is tried only after everything else has said no.
eth_bases:  dw 0x300, 0x320, 0x340, 0x280, 0x2C0, 0x240, 0x360, 0
%ifdef ETH_BASE
eth_fixed:  dw ETH_BASE
%endif

%endmacro

; --- NIC_ATTACH_FIND: pin, fixed base or sweep; falls into .found, else .none ---
%macro NIC_ATTACH_FIND 0
    call os88_pin               ; THE PORTS PAGE FIRST (SPEC.md 51.9.2): a
    or  al, al                  ; pinned card is probed at its one address and
    jz  %%auto                   ; the sweep never runs, so nothing else on the
    cmp al, DRVPIN_OFF          ; bus is so much as read. Off - which the page
    je  .none                   ; does not offer for a card - looks nowhere
    xor ah, ah
    shl ax, 1
    add ax, DRVPIN_BASE
    mov [eth_base], ax
    call ne_probe
    jnc .found
    jmp .none
%%auto:
%ifdef ETH_BASE
    mov ax, [eth_fixed]         ; `make ETHBASE=0x320`: one address and no
    mov [eth_base], ax          ; sweep, for a machine where the sweep would
    call ne_probe               ; poke something that objects
    jnc .found
    jmp .none
%endif
    mov bx, eth_bases
%%try:
    mov ax, [bx]
    or  ax, ax
    jz  .none
    mov [eth_base], ax
    add bx, 2
    call ne_probe
    jc  %%try
%endmacro

; --- NIC_STATE: the 8390's ring, as ne_init picked it -------------------------
%macro NIC_STATE 0
eth_txpg:   db NE_TXPAGE        ; the ring, as ne_init picked it
eth_pstart: db NE_PSTART
eth_pstop:  db NE_PSTOP
eth_next:   db NE_PSTART + 1    ; the page WE will read next
%endmacro

; --- NIC_UI_CARD: the Control Panel's card row (BX = the row's y) -------------
%macro NIC_UI_CARD 0
    mov si, eu_s_ne1
    cmp byte [eth_word], 0
    je  %%kind
    mov si, eu_s_ne2
%%kind:
    call eu_val
    mov ax, [eth_base]
    mov cx, EU_VX + 64
    call eu_hex
%endmacro
%macro NIC_UI_STRINGS 0
eu_s_ne1:   db 'Ne1000', 0
eu_s_ne2:   db 'Ne2000', 0
%endmacro

; --- NIC_RING_CONSTS: what the receive ring can absorb (tcp.inc's sk_cardcap) ---
%macro NIC_RING_CONSTS 0
NIC_RINGSZ  equ (NE_PSTOP - NE_PSTART) * 256
NIC1_RINGSZ equ (NE1_PSTOP - NE1_PSTART) * 256
SK_RINGSLK  equ 1536            ; one 1514-byte frame, rounded up to 6 pages

; --- and the rungs have to LAND somewhere, which is a build-time fact --------
; Without these the clamp is silent: a card whose ring stopped fitting even
; the middle rung would fall to the floor and read as "the network got slow".
%if SK_RXMAX > NIC_RINGSZ - SK_RINGSLK
    %error "SK_RXMAX no longer fits an NE2000's receive ring"
%endif
%if SK_RXMID > NIC1_RINGSZ - SK_RINGSLK
    %error "SK_RXMID no longer fits an NE1000's receive ring"
%endif
%if SK_RXCAP0 > NIC1_RINGSZ - SK_RINGSLK
    %error "even the floor rung no longer fits an NE1000's receive ring"
%endif
%endmacro
