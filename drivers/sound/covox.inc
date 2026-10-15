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
; in:  SI = snd_services (attach's)
; out: CF = 0 at least one did: cvx_ports[n] = LPTn+1's data port where it
;      answered, 0 where it did not, and DSV_TIERS has bit SND_RT_LPT + n for
;      each one that did; CF = 1 none
; clobbers: AX, ES (attach spends neither after this), flags - and it ENDS
;      in snd_upnm, so the card row may be named 'Covox' on the way out
;
; THE BIOS'S OWN NUMBERING AND NOTHING ELSE, because it is the user's: a Covox
; is on "LPT2" the way DOS and every period tracker's setup screen said it, and
; on a mono machine LPT1 IS the adapter's own port at 3BCh - so a Covox on the
; add-on card at 378h is LPT2, and "the first port that answers" would have
; been the wrong one on every Hercules machine with a Covox (SPEC.md 34.14 has
; the run that found it). Three words and never the fourth (lplink.inc's
; lpl_scan: 0040:000E is the EBDA's segment since the PS/2), each believed
; only after the latch test.
;
; THE LATCH TEST is drivers/net/lplink.inc's lp_latch, the same test to the
; port access: the CONTROL register's bit 5 cleared first (a bidirectional
; port left in input mode reads its pins), AAh and then 55h through the data
; latch, and both registers put back as found. Copied rather than shared
; because the two drivers are separate images and neither can reach the
; other's code - and written in line here, its one caller, as a loop over
; the two values (AAh XOR FFh is 55h, whose sign bit ends it). The verdict
; is CF, set where the loop ends, and `inc`, `dec`, `mov`, `out` and `pop`
; on the way back all leave it standing (kernel size pass 10).
; -----------------------------------------------------------------------------
cvx_probe:
    push bx
    push cx
    push dx
    mov ax, 0x0040                  ; ES is not put back: the kernel restores
                                    ; its own round every driver call
                                    ; (drv_stamped), and attach reads nothing
                                    ; through it after this
    mov es, ax
    xor bx, bx
    mov cl, 1 << SND_RT_LPT         ; this port's DSV_TIERS bit
.port:
    mov dx, [es:bx+8]
    dec dx                          ; 0, or not an I/O address (the EBDA
    cmp dh, 0x04                    ; lesson): 1..3FFh is the only range that
    jae .ans                        ; is a port at all, and less one it is
                                    ; exactly the DH of 0..3 - and JAE is
                                    ; taken with CF = 0, which .ans reads as
                                    ; "did not answer"
    inc dx                          ; (INC keeps CF)
    inc dx
    inc dx                          ; DX = the CONTROL register
    in al, dx
    push ax                         ; what it held, put back below
    and al, 0xDF
    out dx, al
    dec dx
    dec dx                          ; DX = the DATA register
    in al, dx
    mov ah, al                      ; what the data lines held, likewise
    mov ch, 0xAA
.val:
    mov al, ch
    out dx, al
    jmp short $+2
    jmp short $+2
    in al, dx
    cmp al, ch
    clc                             ; (CLC keeps ZF)
    jne .back                       ; a bit that did not latch: not a port
    xor ch, 0xFF                    ; AAh -> 55h, then 55h -> AAh: SF = 1
    jns .val                        ; after the second, and both latched
    stc
.back:
    mov al, ah                      ; the data lines as they were...
    out dx, al
    inc dx
    inc dx
    pop ax                          ; ...and the control register
    out dx, al
    dec dx
    dec dx                          ; DX = the base again, CF the verdict
.ans:
    sbb ax, ax                      ; AX = FFFFh answered, 0 not...
    and ax, dx                      ; ...so the base or 0
    jz .put
    or [si+DSV_TIERS], cl
.put:
    mov [cvx_ports+bx], ax
    inc bx
    inc bx
    shl cl, 1                       ; 10h, 20h, 40h: SF = 1 when it reaches
    jns .port                       ; 80h, after LPT3
%if SND_RT_LPT != 4
  %error "cvx_probe ends its loop on the bit after SND_RT_LPT + 2 being 80h"
%endif
    cmp byte [si+DSV_TIERS], 1 << SND_RT_LPT
                                    ; CF = 1 below SND_RT_LPT's bit: no port
                                    ; answered, since every bit DSV_TIERS ever
                                    ; carries is a SND_RT_* of 2..6, all in
                                    ; its low byte and none above the three
    pop dx
    pop cx
    pop bx
    mov ax, snd_s_cvx               ; ...and FALLS INTO snd_upnm: the probe
                                    ; that answered names the card row when
                                    ; nothing before it brought the driver up
                                    ; - on a machine with no card that is the
                                    ; whole reason it is up

; snd_upnm - the MPU-401's and the Covox's shared tail of attach: a probe that
; answered (CF = 0) brings the driver up when nothing before it did, and then
; names the card row after itself. in: CF = the probe's verdict, AX = its
; name, SI = snd_services. out: nothing; clobbers flags (kernel size pass 10)
snd_upnm:
    jc .out
    cmp byte [drv_up], 0
    jne .out
    inc byte [drv_up]
    mov [si+DSV_NAME], ax
.out:
    ret

; THE TIER (which port is the DAC, and SND_CAP_LPTDAC) is sound.asm's
; snd_tier's tail: it runs once per DRVV_TIER, from there and nowhere else.

; -----------------------------------------------------------------------------
; cvx_v_info - SNDV_DACINFO (sndpkg.inc): AX = the port while a Covox tier is
; live, else CF = 1 AX = SNDE_NODAC. Reads one word: worker-safe.
; -----------------------------------------------------------------------------
cvx_v_info:
    mov ax, [cvx_port]
    cmp ax, 1                       ; CF = 1 exactly when it is 0...
    jnc .out
    mov al, SNDE_NODAC              ; ...and then AH is 0 already
.out:
    ret

            dw 0                    ; NEVER WRITTEN: cvx_ports[-1], what
                                    ; snd_tier reads for a tier that is not a
                                    ; Covox's (tests/covox.py finds the three
                                    ; ports and cvx_port by their adjacency,
                                    ; so this goes in front of them)
cvx_ports:  dw 0, 0, 0              ; LPT1..LPT3's data ports, 0 = did not
                                    ; answer the latch test
cvx_port:   dw 0                    ; the DAC's, while a Covox tier is live
