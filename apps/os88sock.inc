; =============================================================================
; os88sock.inc - finding whichever driver is answering the socket verbs
;
; **A PACKAGE ASKS FOR A SERVICE AND THE KERNEL ADDRESSES A CLASS.** Those are
; not the same thing, and until SPEC.md 72 there was only one socket driver so
; nothing had to notice. NET.DRV serves a volume, so it publishes in
; DRVC_FILE; ETHER.DRV serves no volume at all, so it publishes in DRVC_NET;
; both answer netpkg.inc's verbs and a machine may have both attached.
;
;       %include "os88sock.inc"      ; at the END, beside os88ui.inc
;       ...
;       call net_find                ; once, before the first socket call
;       jc   .no_networking
;       mov  bh, NET_CLASS           ; = [net_cls], set by net_find
;       mov  bl, NETV_OPEN
;
; NET_CLASS used to be an `equ` and is a memory reference now, which is why
; every `mov bh, NET_CLASS` in every package here still assembles unchanged:
; the operand went from an immediate to a byte in the image, and nothing else
; about the call moved. That is the whole of what stage E cost the software
; above the driver.
;
; **THE CARD IS TRIED FIRST**, and that is a policy rather than an accident.
; A machine with both has an Ethernet card and a parallel cable; the card is
; three orders of magnitude faster (PERFORMANCE.md Set 39 puts the cable at
; 3,741 bytes/second), so a browser that found the cable first on such a
; machine would be slow for a reason the user cannot see.
;
; **AND NETV_IDENT IS WHAT MAKES IT SAFE** (SPEC.md 20.11.1). A class is a
; publication SLOT: DRVC_FILE is RAMDISK.DRV's as well as NET.DRV's, and the
; RAM disk's own package door has a verb 1 of its own. Asking each candidate
; for its signature is what stops a hostname being handed to a RAM disk.
;
; This file is code and data, so it goes where a package's code goes - at the
; end of the source, before OS88_BSS - and it carries its own one byte of
; state in the image rather than asking its includer for a bss slot.
;
; **OS88SOCK_STATE: AND IS THE STACK UP?** Almost every package asks NETV_STATE
; next and refuses unless NSTF_SOCK is set - the same five instructions after
; every net_find. A package whose EVERY net_find wants that question answered
; defines OS88SOCK_STATE before including this file, and net_find asks it too:
;
;       %define OS88SOCK_STATE
;       ...
;       call net_find
;       jc   .no_driver              ; no socket driver, or NETV_STATE refused
;       jz   .no_link                ; one answered, and NSTF_SOCK is clear
;
; with AX = NETV_STATE's answer on the CF=0 path (AL the NSTF_* bits, AH the
; free handles). It costs net_find 10 bytes and saves each caller 15, so it
; is opt-in: a package that never asks (the DOS box, NETBENCH) assembles the
; plain net_find and pays nothing, and one with a net_find that must NOT ask
; - FTPD's address read at PASV, where NETV_STATE would pump ETHER.DRV's ring
; a second time - leaves it undefined and asks inline.
; =============================================================================

net_cls:    db 0                ; the class that answered, 0 = none yet

; -----------------------------------------------------------------------------
; net_find - which class answers the socket verbs?
; out: CF=0 and [net_cls] set; CF=1 = no socket driver at all
;      ...and with OS88SOCK_STATE (see the header): CF=1 also when NETV_STATE
;      refuses, and on CF=0 AX = its answer and ZF=1 = NSTF_SOCK clear
; clobbers: flags (AX is NOT preserved: the probe's answer is not interesting
;           and a caller that wanted AX kept would be the only one)
; -----------------------------------------------------------------------------
net_find:
    push bx
    mov bh, DRVC_NET            ; the CARD first - see the header
    call net_try
    jnc .got
    mov bh, DRVC_FILE           ; ...then the cable
    call net_try
    jnc .got
    mov byte [net_cls], 0
.no:
    pop bx
    stc
    ret
.got:
    mov [net_cls], bh
%ifdef OS88SOCK_STATE
    mov bl, NETV_STATE          ; BH is still the class that answered
    call OSAPI_DRV_CALL         ; both drivers keep BX, CX, DX and SI here
    jc .no
    test al, NSTF_SOCK          ; CF=0, and ZF is the caller's second answer
    pop bx
    ret
%else
    pop bx
    clc
    ret
%endif

; --- net_try - does the class in BH answer NETV_IDENT with NET_SIG? ----------
net_try:
    push cx
    push si
    push di
    mov bl, NETV_IDENT
    call OSAPI_DRV_CALL
    pop di
    pop si
    pop cx
    jc  .no
    cmp ax, NET_SIG
    jne .no
    clc
    ret
.no:
    stc
    ret
