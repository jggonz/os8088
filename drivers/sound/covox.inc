; =============================================================================
; os8088 - drivers/sound/covox.inc
;
; THE COVOX SPEECH THING (SPEC.md 34.14): eight resistors on a parallel port's
; data lines - an R-2R ladder, the "LPT DAC" - so a byte written to the port's
; DATA register (base + 0) is a voltage at the jack. Unsigned, 80h the middle.
; No FIFO, no IRQ, no DMA and no clock: the CPU writes every sample at the
; sample rate, which on this machine means the speaker's own sample ISR
; (SPEC.md 34.11) with `out dx, al` where the speaker has `out 42h, al`. So
; the player is apps/os88spk.inc and NOT this driver; what the driver owns is
; the two things a package cannot know for itself:
;
;   WHICH PORT. The BIOS's own table at 0040:0008 (LPT1, LPT2, LPT3 - three
;   words, never the fourth: drivers/net/lplink.inc's lpl_scan has why), then
;   the three addresses the POST scans, each verified with lp_latch's
;   two-value read-back before it is believed. The first that answers is the
;   port, LPT1 first.
;
;   WHETHER A COVOX IS ON IT AT ALL - which no probe can answer, because a
;   resistor ladder reads back exactly like a bare port or a printer. So it is
;   ANNOUNCED, never detected: docs/history/SOUND-PLAN.md planned it as "a row
;   with presence set from a Control Panel checkbox", and the row it became is
;   the Sound page's fourth tier, SND_RT_LPT. That byte already persists in
;   SYSTEM.CFG ('SR'), so the announcement costs no kernel byte and no
;   settings blob: DSV_TIERS bit SND_RT_LPT says a port answered (so the row
;   is live), and SND_CAP_LPTDAC is published only while the user's tier IS
;   that one (so a package plays the DAC only when somebody said it is there).
;
; NOTHING HERE WRITES THE PORT after the probe has put back what it found. A
; printer sharing the port (the original Covox passed the cable through) sees
; its data lines change only while a package plays, and never a strobe.
;
; Labels cvx_; state in the image (a driver has no bss: drivers/os88drv.inc).
; =============================================================================

; -----------------------------------------------------------------------------
; cvx_probe - find the first parallel port that answers (attach, once)
; in:  nothing
; out: CF = 0, [cvx_port] = its data port; CF = 1 none, [cvx_port] = 0
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
cvx_probe:
    push ax
    push bx
    push es
    mov word [cvx_port], 0
    mov ax, 0x0040
    mov es, ax
    xor bx, bx
.bios:                          ; LPT1..LPT3 as the POST found them
    mov ax, [es:bx+8]
    call cvx_try
    jnc .out
    add bx, 2
    cmp bx, 6
    jb .bios
    xor bx, bx
.std:                           ; ...and where a clone POST may not look
    mov ax, [cs:cvx_std+bx]
    call cvx_try
    jnc .out
    add bx, 2
    cmp bx, 6
    jb .std
    stc
.out:
    pop es
    pop bx
    pop ax
    ret

; cvx_try - AX = a candidate base: CF = 0 and [cvx_port] = AX when it is an
; I/O address (not 0, below 400h - lpl_scan's EBDA lesson) and its data latch
; reads back both AAh and 55h. clobbers: nothing else (flags)
cvx_try:
    or ax, ax
    jz .no
    cmp ax, 0x0400
    jae .no
    call cvx_latch
    jc .no
    mov [cvx_port], ax
    clc
    ret
.no:
    stc
    ret

; cvx_latch - drivers/net/lplink.inc's lp_latch, the same test to the
; instruction: the CONTROL register's bit 5 cleared first (a bidirectional
; port left in input mode reads its pins), AAh and then 55h through the data
; latch, and both registers put back as found. Copied rather than shared
; because the two drivers are separate images and neither can reach the
; other's code.
; in: AX = the base. out: CF = 0 it answered. clobbers: nothing else
cvx_latch:
    push ax
    push bx
    push cx
    push dx
    mov dx, ax
    add dx, 2
    in al, dx
    mov cl, al
    and al, 0xDF
    out dx, al
    sub dx, 2
    in al, dx
    mov bh, al                  ; what the data lines held, put back on both
    mov al, 0xAA                ; paths below
    out dx, al
    jmp short $+2
    jmp short $+2
    in al, dx
    cmp al, 0xAA
    jne .no
    mov al, 0x55
    out dx, al
    jmp short $+2
    jmp short $+2
    in al, dx
    cmp al, 0x55
    jne .no
    mov al, bh
    out dx, al
    clc
    jmp short .ctl
.no:
    mov al, bh
    out dx, al
    stc
.ctl:
    pushf                       ; the verdict is CF, and the restore below
    add dx, 2                   ; must not be what decides it
    mov al, cl
    out dx, al
    popf
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cvx_tier - the Covox half of DRVV_TIER (sound.asm's snd_tier): publish
; SND_CAP_LPTDAC when AH = SND_RT_LPT and a port answered, withdraw it for any
; other tier. Cannot fail: a Covox tier on a machine whose port has gone is the
; AdLib tier, which is what the page's dot then shows (ctrl.inc's cp_snd_row).
; in:  AH = SND_RT_*
; out: nothing; clobbers nothing (flags)
; -----------------------------------------------------------------------------
cvx_tier:
    and word [snd_services+DSV_CAPS], ~SND_CAP_LPTDAC
    cmp ah, SND_RT_LPT
    jne .out
    cmp word [cvx_port], 0
    je .out
    or word [snd_services+DSV_CAPS], SND_CAP_LPTDAC
.out:
    ret

; -----------------------------------------------------------------------------
; cvx_v_info - SNDV_DACINFO (sndpkg.inc): AX = the port while the Covox tier
; is live, else CF = 1 AX = SNDE_NODAC. Reads two words: worker-safe.
; -----------------------------------------------------------------------------
cvx_v_info:
    test word [snd_services+DSV_CAPS], SND_CAP_LPTDAC
    jz .no
    mov ax, [cvx_port]
    clc
    ret
.no:
    mov ax, SNDE_NODAC
    stc
    ret

cvx_std:    dw 0x3BC, 0x378, 0x278  ; the POST's own three (lplink.inc's
                                    ; std_base, and its reason for the order)
cvx_port:   dw 0                    ; the data port, 0 = none answered
