; =============================================================================
; os8088 - PCIETHER.DRV
;
; The PCI half of SPEC.md 72: the SAME TCP/IP stack and socket verbs as
; ETHER.DRV, in front of a card found on the PCI bus. SPEC.md 125 is the
; contract. kern_mega only (125.1): the PCI BIOS is reached by `int 1Ah`, which
; kern_mega reflects to real mode (pci.inc), and no machine below a 386 has PCI.
;
; --- WHAT IS IN HERE --------------------------------------------------------
;   pci.inc      the PCI BIOS: find a device, its I/O BAR, enable it
;   rtl8139.inc  the Realtek RTL8139 backend: polled, one ring, one buffer
;   ethdrv.inc   the driver's body, SHARED AS SOURCE with ETHER.DRV (125.2):
;                service table, attach/ready/detach, DHCP wait, the package
;                door - and through it the stack (inet, tcp, dns, ethsock,
;                etherui, ethcfg, ethstate), none of it copied
;
; --- THE NIC SEAM -----------------------------------------------------------
; The stack calls nic_rx / nic_tx / nic_init / nic_stop. Here they are one
; `jmp` each through the vectors of the backend the PCI probe chose, so the
; next card (a row in pce_tab and a <card>.inc beside rtl8139.inc) is not a new
; driver. ETHER.DRV's are the 8390's (drivers/ether/nicne2k.inc).
;
; --- IT POLLS, AND HOOKS NOTHING (SPEC.md 72.2.1, 125.4) --------------------
; IMR = 0, no IRQ, no vector. The receive ring is a heap claim the card writes
; by bus mastering and the socket verbs drain.
;
; OS88_DRV_HIGHOK, as ETHER.DRV and HDD.DRV are: kern_mega may run this image
; above 1MB. Its BIOS calls are int 1Ah B101h-B10Ch at attach, and none of
; them takes a pointer; mega_svc hands a DS above 1MB to the BIOS as FFFFh
; (mega_s2segc) and gives the selector back after, which is HDD.DRV's int 13h
; from a high image every day. The receive ring is MEMP_CONV (rt_claim): a
; bus master's memory never goes to the pool. OS88_DRV_HICLAIM, so the socket
; pool - movable, and only ever loaded - is claimed high too.
; =============================================================================

%define NETPKG_DRIVER           ; netpkg.inc's constants, not its client half
%define OS88_DRV_HICLAIM        ; the socket pool above 1MB (item 83)...
%define OS88_DRV_HIGHOK         ; ...and the image (KERN-MEGA-PLAN item 82)

%ifdef ETHPROF
  %error "pciether: the stage timers (ethprof.inc) are the 8390's"
%endif

; --- the NIC seam, as the PCI family ----------------------------------------
%define NIC_BACKEND "pcifamily.inc"
%define nic_init    pce_init
%define nic_rx      pce_rx
%define nic_tx      pce_tx
%define nic_stop    pce_stop
PCE_ROW     equ 8               ; a pce_tab row: vendor, device, name, vectors
PCEV_CLAIM  equ 0               ; ...the vectors, in order
PCEV_INIT   equ 2
PCEV_RX     equ 4
PCEV_TX     equ 6
PCEV_STOP   equ 8

%macro NIC_DATA 0
pce_tab:                        ; the backends, in the order they are tried
    dw 0x10EC, 0x8139, rt_name, rt_vtab
    dw 0
%endmacro

%macro NIC_ATTACH_FIND 2
    call pce_find               ; CF=1 = no PCI card of ours
    jc  %2
%endmacro

%macro NIC_STATE 0
pce_bdf:    dw 0                ; the card's bus / device-function
pce_name:   dw 0                ; its name, for the Control Panel page
pce_vt:     times 5 dw 0        ; the chosen backend's vectors (PCEV_*)
rt_seg:     dw 0                ; the ring's claim, as OSAPI_MEM_P named it
rt_lin:     dw 0, 0             ; ...its linear (bus) base
rt_par:     dw 0                ; ...that as a paragraph (below 1MB)
rt_win:     dw 0                ; ...and rt_es's window over it, 0 = none
rt_off:     dw 0                ; the read offset in the ring
rt_txn:     db 0                ; the transmit slot to use next
%endmacro

; the Control Panel page names the card and offers no I/O base (125.2)
%macro NIC_UI_CARD 0
    mov si, [pce_name]
    call eu_val
%endmacro
%macro NIC_UI_STRINGS 0
%endmacro

%macro NIC_RING_CONSTS 0
NIC_RINGSZ  equ RT_RING
NIC1_RINGSZ equ RT_RING
SK_RINGSLK  equ 1536            ; one frame, rounded up
%if SK_RXMID > NIC_RINGSZ - SK_RINGSLK
    %error "SK_RXMID no longer fits the RTL8139's receive ring"
%endif
%if SK_RXCAP0 > NIC_RINGSZ - SK_RINGSLK
    %error "even the floor rung no longer fits the RTL8139's receive ring"
%endif
%endmacro

%include "os88drv.inc"
%include "netpkg.inc"

    OS88_DRIVER 'pciether', DRVC_NET, eth_entry
%include "ethdrv.inc"

