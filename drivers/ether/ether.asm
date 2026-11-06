; =============================================================================
; os8088 - ETHER.DRV
;
; An NE1000/NE2000 card and a TCP/IP stack, answering the SAME socket verbs
; NET.DRV answers over a parallel cable (SPEC.md 62.11, drivers/net/netpkg.inc).
; That is the whole claim of docs/plans/completed/NET-STACK-PLAN.md stage E and it is the test
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
%define OS88_DRV_HICLAIM         ; ...and its socket pool there too (item 83:
                                ; sk_seg is only ever loaded, and sk_reloc
                                ; is the one store a move needs)
%define OS88_DRV_HIGHOK          ; kern_mega may run this image above 1MB
                                ; (KERN-MEGA-PLAN item 82: no paragraph
                                ; arithmetic on its own segment, no BIOS or
                                ; DMA buffer in it)
%include "nicne2k.inc"           ; ...the NIC seam, as the 8390 (SPEC.md 125.2)

%include "os88drv.inc"
%include "netpkg.inc"

    OS88_DRIVER 'ether', DRVC_NET, eth_entry
%include "ethdrv.inc"            ; ...the rest of the driver, SHARED with
                                ; PCIETHER.DRV (SPEC.md 125.2)
