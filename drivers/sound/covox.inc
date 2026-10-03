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
;   WHICH PORTS COULD BE ONE. The BIOS's own table at 0040:0008 (LPT1, LPT2,
;   LPT3 - three words, never the fourth: drivers/net/lplink.inc's lpl_scan
;   has why), each verified with lp_latch's two-value read-back before it is
;   believed.
;
;   WHETHER A COVOX IS ON IT AT ALL - which no probe can answer, because a
;   resistor ladder reads back exactly like a bare port or a printer. So it is
;   ANNOUNCED, never detected: docs/history/SOUND-PLAN.md planned it as "a row
;   with presence set from a Control Panel checkbox", and the row it became is
;   the Sound page's fourth tier, SND_RT_LPT + n for a Covox on LPTn+1. That
;   byte already persists in SYSTEM.CFG ('SR'), so the announcement - and
;   WHICH port - costs no kernel byte and no settings blob: DSV_TIERS bit
;   SND_RT_LPT + n says LPTn+1 answered (so that choice is live), and
;   SND_CAP_LPTDAC is published only while the user's tier IS one of them (so
;   a package plays the DAC only when somebody said it is there).
;
; NOTHING HERE WRITES THE PORT after the probe has put back what it found. A
; printer sharing the port (the original Covox passed the cable through) sees
; its data lines change only while a package plays, and never a strobe.
;
; Labels cvx_; state in the image (a driver has no bss: drivers/os88drv.inc).
; =============================================================================

; -----------------------------------------------------------------------------
; cvx_probe - which of LPT1..LPT3 answer (attach, once)
; in:  nothing
; out: CF = 0 at least one did: cvx_ports[n] = LPTn+1's data port where it
;      answered, 0 where it did not, and DSV_TIERS has bit SND_RT_LPT + n for
;      each one that did; CF = 1 none
; clobbers: nothing else (flags)
;
; THE BIOS'S OWN NUMBERING AND NOTHING ELSE, because it is the user's: a Covox
; is on "LPT2" the way DOS and every period tracker's setup screen said it, and
; on a mono machine LPT1 IS the adapter's own port at 3BCh - so a Covox on the
; add-on card at 378h is LPT2, and "the first port that answers" would have
; been the wrong one on every Hercules machine with a Covox (SPEC.md 34.14 has
; the run that found it). Three words and never the fourth (lplink.inc's
; lpl_scan: 0040:000E is the EBDA's segment since the PS/2), each believed
; only after the latch test.
; -----------------------------------------------------------------------------
cvx_probe:
    push ax
    push bx
    push cx
    push es
    mov ax, 0x0040
    mov es, ax
    xor bx, bx
    mov cx, 1 << SND_RT_LPT         ; this port's DSV_TIERS bit
.port:
    mov ax, [es:bx+8]
    mov word [cvx_ports+bx], 0
    or ax, ax
    jz .next
    cmp ax, 0x0400                  ; not an I/O address (the EBDA lesson)
    jae .next
    call cvx_latch
    jc .next
    mov [cvx_ports+bx], ax
    or [snd_services+DSV_TIERS], cx
.next:
    shl cx, 1
    add bx, 2
    cmp bx, 6
    jb .port
    test word [snd_services+DSV_TIERS], 7 << SND_RT_LPT
    jnz .out                        ; CF = 0 from the test
    stc
.out:
    pop es
    pop cx
    pop bx
    pop ax
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
; cvx_tier - the Covox half of DRVV_TIER (sound.asm's snd_tier): AH =
; SND_RT_LPT + n names LPTn+1, and when that port answered at attach it is the
; DAC - [cvx_port] - and SND_CAP_LPTDAC is published. Any other tier, or a
; port that did not answer, withdraws both. Cannot fail: a Covox tier whose
; port has gone is the AdLib tier, which is what the page's dot then shows
; (ctrl.inc's cp_snd_row).
; in:  AH = SND_RT_*
; out: nothing; clobbers nothing (flags)
; -----------------------------------------------------------------------------
cvx_tier:
    push bx
    and word [snd_services+DSV_CAPS], ~SND_CAP_LPTDAC
    mov word [cvx_port], 0
    mov bl, ah
    sub bl, SND_RT_LPT
    cmp bl, 2
    ja .out                         ; not a Covox tier (below wraps to big)
    xor bh, bh
    shl bx, 1
    mov bx, [cvx_ports+bx]
    or bx, bx
    jz .out                         ; ...or one whose port did not answer
    mov [cvx_port], bx
    or word [snd_services+DSV_CAPS], SND_CAP_LPTDAC
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; cvx_v_info - SNDV_DACINFO (sndpkg.inc): AX = the port while a Covox tier is
; live, else CF = 1 AX = SNDE_NODAC. Reads two words: worker-safe.
; -----------------------------------------------------------------------------
cvx_v_info:
    mov ax, [cvx_port]
    or ax, ax
    jz .no
    clc
    ret
.no:
    mov ax, SNDE_NODAC
    stc
    ret

cvx_ports:  dw 0, 0, 0              ; LPT1..LPT3's data ports, 0 = did not
                                    ; answer the latch test
cvx_port:   dw 0                    ; the DAC's, while a Covox tier is live
