; =============================================================================
; os8088 - SOUND.DRV
;
; The loadable sound driver (SPEC.md 34/51.4): the AdLib/OPL2 tier, and the
; two API slots the resident kernel holds empty without it - OSAPI_SND_FM
; (0x00F8) and, when the Sound Blaster half lands, OSAPI_SND_STREAM (0x0100).
;
; WHAT STAYS IN THE KERNEL AND WHY. The PC speaker is resident: it is on
; every machine this OS boots, it is what `snd_beep` and Arkanoid use, and
; its whole driver is a handful of port writes. What is NOT on every machine
; - an OPL2, a Sound Blaster - is what costs kernel bytes to carry and what
; a 128KB machine with neither should not be paying for. So the speaker's
; tone and exclusive-clip tiers are kernel code (SPEC.md 34.2/34.4) and the
; card tiers are this file, loaded from the system disk only if the user
; asked for it (SPEC.md 51).
;
; FM is the real background-music tier: a note-on is two register writes,
; ~0.2-0.6ms on the floor machine, and then ZERO CPU while it sounds. That
; is why publishing DSV_TONE moves the tone tier here as well - with an
; AdLib present, tones leave the speaker for free and the speaker is left
; for the exclusive-clip tier that has nowhere else to go.
;
; Port ownership (SPEC.md 34.1, binding): ALL access to 388h/389h goes
; through opl_wr, whose atomicity is sized honestly - pushf/cli covers only
; the address select at 388h, its 6 counted status reads and the data write
; at 389h; the 35-read post-data delay runs at the caller's IF. The address
; register is stable across it, and same-tier interleaving is excluded by
; the kernel router's ownership, not by cli.
;
; The FM channel allocator (SPEC.md 34.3): a 9-bit claim bitmap plus a
; per-channel owner stamp. A channel is claimed by its first note-on or
; patch-load (the raw 0-8 index is the handle); a verb on a channel another
; requester holds is refused CF=1; verb 3 (all-off) and the kernel's
; instance teardown release every channel a requester holds. CHANNEL 8 IS
; RESERVED out of the bitmap, always, because this driver publishes DSV_TONE
; and channel 8 is the voice that tier sounds on - so a package grabbing
; every FM channel can never silence the system beep.
;
; There is no .bss and no far code: a driver's data ships zeroed inside its
; image (see drivers/os88drv.inc), and .fartext was retired (SPEC.md 33).
; The probe and the ~245-write init are ordinary code that happens to run
; once.
; =============================================================================

%include "os88drv.inc"

OS88_DRIVER 'Sound', DRVC_SOUND, snd_entry

; =============================================================================
; The entry proc: the only thing the kernel calls by offset. Everything else
; it reaches through the service table this returns.
; =============================================================================

; -----------------------------------------------------------------------------
; snd_entry - attach / detach (SPEC.md 51.2)
; in:  AL = DRVV_*; DS = CS = ours, ES = KERNEL_SEG
; out: attach: CF = 0 and SI = the service table, or CF = 1 = no hardware
;      detach: nothing
;
; EVERY VERB IS NAMED AND ANYTHING ELSE REFUSES, which is hd_entry's shape and
; is not a style point. This tested DETACH and TIER and let everything else
; fall into the attach body - so DRVV_READY, which drv_load sends right after
; DRVV_ATTACH (SPEC.md 51.2.2), RAN A SECOND WHOLE ATTACH on every load: the
; OPL re-probed and re-initialised, the DSP reset again, and sbl_dma_map
; claiming a second 12KB page-safe ring while [sbl_seg] was overwritten with
; the new one. The first was then unreachable and unfreeable, charged to the
; driver's segment until detach swept it by owner - a silent 12KB of the claim
; heap, on every machine with a Sound Blaster in it, for the life of the
; session. Found by breakpointing OSAPI_MEM_CLAIM_DMA: two hits per load, both
; from this routine's attach body, one per drv_load call site.
;
; The fallthrough was invisible because a re-attach SUCCEEDS - the hardware is
; still there, so the second probe answers yes and the driver comes up looking
; perfectly correct. Only the heap knew.
; -----------------------------------------------------------------------------
snd_entry:
    cmp al, DRVV_DETACH
    je snd_detach
    cmp al, DRVV_TIER
    je snd_tier
    cmp al, DRVV_READY
    je .nosb                    ; nothing to do: this driver keeps no settings
                                ; blob (SPEC.md 51.9) and the probe already
                                ; ran at ATTACH, so READY just re-answers with
                                ; the table it built then
    cmp al, DRVV_ATTACH
    jne .nohw                   ; an unknown verb REFUSES rather than probing
                                ; hardware nobody asked about
    ; --- attach ---------------------------------------------------------------
    ; TWO tiers, independently present. Either one alone is worth attaching
    ; for, so the refusal is only when NEITHER answers - and the service
    ; table is built from what actually replied, so a machine with an AdLib
    ; and no Sound Blaster publishes FM and no stream verb at all.
    mov byte [drv_up], 0
    call opl_probe              ; the timer-flag dance; a present chip is
    jc .nofm                    ; FULLY initialised before this returns
    mov byte [drv_up], 1
    mov word [snd_services+DSV_FM], opl_fm_op
    mov word [snd_services+DSV_TONE], opl_tone
    mov word [snd_services+DSV_RELINST], snd_release_both
    or word [snd_services+DSV_CAPS], SND_CAP_FM
    or word [snd_services+DSV_TIERS], 1 << SND_RT_FM
    mov word [snd_services+DSV_NAME], snd_s_opl
.nofm:
    call sbl_attach             ; the reset scan, then the DMA buffer
    jnc .sbok
    cmp al, DRVE_TWICE
    je .bug                     ; NOT .nosb. Every other refusal here is a fact
                                ; about the machine and degrades to FM-only,
                                ; which is a real configuration; this one says
                                ; we attached twice, and swallowing it would
                                ; leave the driver up and looking fine with the
                                ; report thrown away - the exact silence the
                                ; code exists to break. It is a FUTURE
                                ; regression's guard: the DRVV_READY
                                ; fallthrough that used to reach here is fixed
                                ; above, so nothing should ever take this jump
    jmp short .nosb
.sbok:
    mov byte [drv_up], 1
    mov word [snd_services+DSV_STREAM], sbl_stream_op
    mov word [snd_services+DSV_TICK], sbl_tick
    mov word [snd_services+DSV_RELINST], snd_release_both
                                ; ...and the release verb, which the OPL leg
                                ; above may already have published. It must be
                                ; set by EITHER half attaching, because it is
                                ; now the one entry that releases BOTH - a
                                ; card with no OPL still has grants and a
                                ; staging pool to give back
    or word [snd_services+DSV_CAPS], SND_CAP_PCM_BG | SND_CAP_PCM_IN
    or word [snd_services+DSV_TIERS], 1 << SND_RT_SB
                                ; ...and THIS is the only place that bit is
                                ; ever set: snd_tier may take the DSP tier
                                ; away and give it back all session, but
                                ; whether a card is in the machine was
                                ; settled here and cannot change until the
                                ; driver is reloaded. That is what lets the
                                ; Control Panel grey the row instead of
                                ; running the six-base reset scan on a click
    mov word [snd_services+DSV_NAME], snd_s_sb
                                ; DSV_NAME is what the Control Panel's Sound
                                ; page calls the CARD ROW, and the SOUND
                                ; BLASTER TAKES IT WHENEVER IT ATTACHES -
                                ; unconditionally, overwriting the OPL2's.
                                ;
                                ; It used to defer to whatever named itself
                                ; first, on the reasoning that the row is the
                                ; TONE sink and the tone comes out of the
                                ; OPL2. That is true and it is still the wrong
                                ; name, because EVERY Sound Blaster carries an
                                ; OPL2 of its own and that chip is probed
                                ; FIRST: the row therefore read 'AdLib' on
                                ; every real Sound Blaster ever made, and the
                                ; page's one statement about the machine's
                                ; hardware named the smaller half of it. The
                                ; tone still comes out of an OPL2 either way -
                                ; the SB's - so the new name is no less true
                                ; of the sink, and strictly more true of the
                                ; card. It doubles as the answer to "did the
                                ; DSP tier attach?", since nothing else can
                                ; put that string on the page.
.nosb:
    cmp byte [drv_up], 0
    je .nohw
    mov si, snd_services
    clc
    ret
.nohw:
    mov al, DRVE_HW             ; the reason, explicitly: attach's refusal may
.bug:                           ; carry a DRVE_* now (SPEC.md 51.2) and the
    stc                         ; kernel reads whatever AL holds - so .bug
    ret                         ; joins here with DRVE_TWICE already in AL

; -----------------------------------------------------------------------------
; snd_tier - DRVV_TIER: how much of ourselves the user wants (SPEC.md 34.8)
;
; in:  AH = SND_RT_* - anything below SND_RT_SB means "no Sound Blaster"
; out: CF = 0 and SI = the service table, which the kernel re-copies because
;      this changes it; CF = 1 and AL = DRVE_* - the SB tier was wanted and
;      could not be had
;
; The FM half is never touched. It costs no memory beyond the driver image,
; and an AdLib is what remains when the DSP tier is off - so the two tiers
; are not alternatives to probe between, they are a subset relationship the
; user picks a point on.
;
; TURNING IT OFF CANNOT FAIL, which is why that leg does not test anything:
; sbl_detach halts the DSP, waits the worker out, unhooks the vector and
; frees both claims. TURNING IT ON IS A CLAIM AND SO CAN, and we simply try -
; there is nothing to pre-check with. mem_avail reports the largest free run,
; which says nothing about whether a 64KB-page-safe base exists inside it;
; only mem_claim_dma's own scan knows, so asking it IS the test. That is also
; why the Control Panel does not grey the Sound Blaster row on a full heap:
; the only honest test is the claim itself, so the page makes it and reports
; what came back (SPEC.md 34.8).
; -----------------------------------------------------------------------------
snd_tier:
    cmp byte [drv_up], 0
    je .nohw                    ; nothing attached: no tier to move
    cmp ah, SND_RT_SB
    jae .want
                                ; --- off ----------------------------------
    cmp word [snd_services+DSV_STREAM], 0
    je .table                   ; already off
    call sbl_detach             ; cannot fail (SPEC.md 51.2)
    mov word [snd_services+DSV_STREAM], 0
    and word [snd_services+DSV_CAPS], ~(SND_CAP_PCM_BG | SND_CAP_PCM_IN)
    cmp word [snd_services+DSV_TONE], 0
    je .table                   ; no OPL2 either: the name stays as it was
    mov word [snd_services+DSV_NAME], snd_s_opl   ; the card is an AdLib now
    jmp short .table
.want:                          ; --- on -----------------------------------
    cmp word [snd_services+DSV_STREAM], 0
    jne .table                  ; already on
    call sbl_attach             ; probe + the 12KB page-safe claim; AL is the
    jc .no                      ; DRVE_* saying which of the two failed
    mov word [snd_services+DSV_STREAM], sbl_stream_op
    mov word [snd_services+DSV_TICK], sbl_tick
    or word [snd_services+DSV_CAPS], SND_CAP_PCM_BG | SND_CAP_PCM_IN
    mov word [snd_services+DSV_NAME], snd_s_sb
.table:
    mov si, snd_services
    clc
    ret
.nohw:
    mov al, DRVE_HW             ; nothing attached at all, so there is no
.no:                            ; Sound Blaster here either
    stc
    ret

; -----------------------------------------------------------------------------
; snd_detach - silence everything and give the hardware back
; in:  nothing
; out: nothing. Cannot fail (SPEC.md 51.2).
; -----------------------------------------------------------------------------
snd_detach:
    push ax
    push cx
    cmp byte [drv_up], 0
    je .out
    call sbl_detach             ; the Sound Blaster FIRST: it is the tier
                                ; with an interrupt vector and a worker task
                                ; in it, and neither may outlive this call -
                                ; the kernel frees our image the moment we
                                ; return (SPEC.md 51.7)
    cmp word [snd_services+DSV_TONE], 0
    je .done                    ; no OPL2 was ever found: nothing below is
                                ; ours to silence
    mov cl, 0
.off:
    call opl_keyoff             ; every voice down, claimed or not
    inc cl
    cmp cl, 9
    jb .off
    mov ax, 0x0460              ; mask both timers, reset the flags: the
    call opl_wr                 ; state a cold AdLib is in
    mov ax, 0x0480
    call opl_wr
    mov ax, 0xBD00
    call opl_wr
    call opl_state_init         ; forget every claim: a reload starts clean
.done:
    mov byte [drv_up], 0
    mov word [snd_services+DSV_CAPS], 0     ; a reload rebuilds the table
    mov word [snd_services+DSV_FM], 0       ; from what answers THEN
    mov word [snd_services+DSV_STREAM], 0
    mov word [snd_services+DSV_TICK], 0
    mov word [snd_services+DSV_RELINST], 0
    mov word [snd_services+DSV_TONE], 0
    mov word [snd_services+DSV_NAME], 0
    mov word [snd_services+DSV_TIERS], 0    ; detach is the ONE thing besides
                                            ; attach that may clear this - a
                                            ; tier change must not
.out:
    pop cx
    pop ax
    ret

; --- the service table (drivers/os88drv.inc) ---------------------------------
; BUILT AT ATTACH, not authored: the two tiers are independently present, so
; what this publishes is what actually answered. A machine with only an
; AdLib gets FM and a tone sink and no stream verb; one with only a Sound
; Blaster gets streams and leaves tones on the PC speaker, which is right -
; an SB's tone tier would be a DAC loop, and the speaker is free.
snd_services:
    dw 0                        ; DSV_CAPS
    dw 0                        ; DSV_FM
    dw 0                        ; DSV_STREAM
    dw 0                        ; DSV_TICK
    dw 0                        ; DSV_RELINST
    dw 0                        ; DSV_NAME
    dw 0                        ; DSV_TONE
    dw 0                        ; DSV_TIERS
    dw 0                        ; DSV_BLK    - a disk driver's, not ours
    dw 0                        ; DSV_CPNAME - no Control Panel page: the
    dw 0                        ; DSV_CPPAINT  Sound and Drivers pages the
    dw 0                        ; DSV_CPCLICK  kernel already has are this
                                ; driver's whole interface (SPEC.md 31.7)
    times DSV_SIZE - ($ - snd_services) db 0

snd_s_opl:  db 'AdLib', 0
snd_s_sb:   db 'Sound Blaster', 0

; =============================================================================
; OPL2 geometry and data
; =============================================================================

opl_slotoff:                    ; channel n -> modulator operator offset;
    db 0x00, 0x01, 0x02         ; carrier = modulator + 3 (SPEC.md 34.2)
    db 0x08, 0x09, 0x0A
    db 0x10, 0x11, 0x12

opl_opregs:                     ; the 5 per-operator register bases, in
    db 0x20, 0x40, 0x60         ; patch-byte order (SPEC.md 34.2):
    db 0x80, 0xE0               ; char/level/attack-decay/sustain-release/wave

opl_fmax:                       ; highest Hz whose F-Number fits 10 bits at
    dw 48, 97, 194, 388         ; block 0..7: floor(1023*49716 / 2^(20-b)).
    dw 776, 1552, 3104, 6208    ; The keyon scan picks the SMALLEST block
                                ; that fits - maximum F-Number precision

; The default patch, loaded onto all 9 channels by the init and the
; square-ish voice the routed tone tier sounds on channel 8 (SPEC.md 34.3):
; sustaining envelopes (instant attack, hold at peak, release 7 so key-off
; is prompt but unclicky), a lightly-driven 1:1 modulator for edge, and a
; half-sine carrier. Layout = opl_patch's contract: 5 modulator bytes, 5
; carrier bytes, then C0h.
opl_defpatch:
    db 0x21, 0x28, 0xF0, 0x07, 0x00     ; modulator: EGT|MULT1, -21dB, A=15
                                        ; D=0, SL=0 RR=7, sine
    db 0x21, 0x00, 0xF0, 0x07, 0x01     ; carrier: EGT|MULT1, full level,
                                        ; A=15 D=0, SL=0 RR=7, half-sine
    db 0x00                             ; C0h: FM connection, no feedback

; =============================================================================
; opl_wr - write one OPL2 register (SPEC.md 34.1, binding)
;
; in:       AH = register index, AL = value
; out:      nothing
; clobbers: nothing (flags)
;
; Select via 388h, 6 counted status reads (the chip's post-address window),
; data write to 389h - that much under one pushf/cli..popf - then 35 counted
; status reads at the caller's IF. ~430-1,300 cycles ~= 90-280us per write on
; the floor machine, of which <= ~100us is this routine's own IF=0 window
; when it is entered at IF=1.
; =============================================================================
opl_wr:
    push ax
    push cx
    push dx
    pushf
    cli
    mov dx, 0x388
    xchg al, ah                 ; AL = register (value parked in AH)
    out dx, al
    mov cx, 6
.post_addr:                     ; counted status reads: the address window
    in al, dx
    loop .post_addr
    mov al, ah                  ; the value
    inc dx                      ; 389h
    out dx, al
    popf                        ; the long delay runs at the caller's IF
    dec dx                      ; 388h
    mov cx, 35
.post_data:                     ; counted status reads: the data window
    in al, dx
    loop .post_data
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; opl_keyon - sound a note on one channel (internal)
;
; in:       CL = channel 0..8 (validated by the caller), BX = frequency Hz
; out:      CF = 0 sounding; CF = 1 frequency unrepresentable (19..6208 -
;           block 7's 10-bit F-Number ceiling, SPEC.md 34.2)
; clobbers: nothing else (flags)
;
; F-Number = Hz * 2^(20-block) / 49716, block chosen smallest so F fits 10
; bits (the opl_fmax scan). The 32-bit numerator is built with a shl/rcl
; loop (Hz <= 6208 << up to 20 keeps DX < 49716, so the div cannot trap).
; Writes A0h+ch (F low) then B0h+ch (KEY-ON | block | F high), and stamps
; opl_b0[ch] so key-off is the single-write path.
; -----------------------------------------------------------------------------
opl_keyon:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp bx, 19
    jb .bad
    xor si, si                  ; SI = block scan index * 2
.blk:
    cmp bx, [opl_fmax+si]
    jbe .got
    add si, 2
    cmp si, 16
    jb .blk
    jmp .bad                    ; > 6208 Hz: no block fits (SPEC.md 34.2)
.got:
    mov ax, si
    shr ax, 1                   ; AL = block b (0..7)
    mov ch, al                  ; CH = block (CL still the channel)
    push cx
    mov cl, 20
    sub cl, ch                  ; CL = 20 - b, the shift count
    mov ax, bx
    xor dx, dx
.shift:
    shl ax, 1                   ; DX:AX = Hz << (20-b)
    rcl dx, 1
    dec cl
    jnz .shift
    mov cx, 49716
    div cx                      ; AX = F-Number, <= 1023 by the block choice
    pop cx                      ; CL = channel, CH = block
    push ax
    mov ah, 0xA0
    add ah, cl
    call opl_wr                 ; A0h+ch <- F-Number low byte (AL)
    pop ax
    mov al, ah                  ; F-Number bits 9..8
    and al, 0x03
    mov dl, ch
    shl dl, 1
    shl dl, 1
    or al, dl                   ; | block << 2
    or al, 0x20                 ; | KEY-ON
    mov bl, cl
    xor bh, bh
    mov [opl_b0+bx], al         ; the single-write key-off's source
    mov ah, 0xB0
    add ah, cl
    call opl_wr                 ; B0h+ch: the note starts
    clc
    jmp .out
.bad:
    stc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; opl_keyoff - silence one channel with a single register write (internal)
; in:       CL = channel 0..8
; out:      nothing
; clobbers: nothing (flags)
; -----------------------------------------------------------------------------
opl_keyoff:
    push ax
    push bx
    mov bl, cl
    xor bh, bh
    mov al, [opl_b0+bx]
    and al, 0xDF                ; clear KEY-ON
    mov [opl_b0+bx], al
    mov ah, 0xB0
    add ah, cl
    call opl_wr
    pop bx
    pop ax
    ret

; =============================================================================
; opl_patch - load an 11-byte patch onto one channel (SPEC.md 34.2)
;
; in:       CL = channel 0..8, ES:SI -> 11 bytes: 5 modulator operator regs
;           (20h/40h/60h/80h/E0h values), 5 carrier, then C0h
; out:      nothing
; clobbers: nothing (flags)
;
; **ES:SI, not DS:SI, and that is the whole reason this reads with an
; override.** The patch bytes are the CALLER'S - a package's, staged into the
; kernel's own buffer by the API slot - while opl_slotoff and opl_opregs are
; OURS. Pointing DS at the caller to reach the bytes would have taken the
; two tables with it, and the register bases would come out of whatever sat
; at those offsets in the kernel. It did, briefly; the note still sounded,
; which is exactly how a bug like this survives a listening test.
; =============================================================================
opl_patch:
    push ax
    push bx
    push cx
    push dx
    push si
    pushf                       ; DF restored on exit; lodsb needs it clear
    cld
    mov bl, cl
    xor bh, bh
    mov dl, [opl_slotoff+bx]    ; DL = this operator's slot offset
    mov dh, 2                   ; two operators: modulator, then carrier
.op:
    xor bx, bx                  ; BX = opl_opregs index
.reg:
    mov ah, [opl_opregs+bx]
    add ah, dl                  ; register = base + slot offset
    es lodsb                    ; the patch byte, from the CALLER's segment
    call opl_wr
    inc bx
    cmp bx, 5
    jb .reg
    add dl, 3                   ; carrier = modulator slot + 3
    dec dh
    jnz .op
    mov ah, 0xC0
    add ah, cl                  ; C0h+ch: feedback/connection
    es lodsb
    call opl_wr
    popf
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; opl_tone - the tone tier's sink while this driver is loaded (DSV_TONE)
;
; in:       AX = frequency Hz (19..6208), or 0 = off; DL = voice (0 only)
; out:      CF = 0 done, CF = 1 unsupported voice/frequency
; clobbers: nothing else (flags)
;
; Channel 8, reserved out of the FM allocator, sounding the default
; square-ish patch the init loaded. Ownership, priority and expiry all live
; in the kernel's snd_tone_req above this; off is the single-write key-off,
; which is what snd_tick reaches on a duration-limited tone's expiry - one
; register write inside IRQ0, which is exactly the worst case SPEC.md 8.2
; sanctions. A frequency above block 7's ceiling is CF=1 and the router
; leaves the incumbent owner sounding, exactly like a bad speaker divisor.
; =============================================================================
opl_tone:
    push bx
    push cx
    or dl, dl                   ; one voice: the reserved channel 8
    jnz .bad
    mov cl, 8
    or ax, ax
    jz .off
    mov bx, ax
    call opl_keyon              ; CF: 19..6208 or refused
    jmp .out
.off:
    call opl_keyoff
    clc
    jmp .out
.bad:
    stc
.out:
    pop cx
    pop bx
    ret

; =============================================================================
; The FM channel allocator (SPEC.md 34.3): 9-bit claim bitmap + owner stamps
; =============================================================================

; -----------------------------------------------------------------------------
; opl_claim - claim a channel for a requester, first touch wins (internal)
;
; in:       CL = channel, DH = requester instance slot (0xFF = kernel)
; out:      CF = 0 claimed or already this requester's; CF = 1 refused -
;           channel out of range, reserved, or another requester's
; clobbers: nothing else (flags)
;
; The test-and-set and the owner stamp are one pushf/cli..popf unit
; (SPEC.md 34.3): two tasks claiming at once cannot both win. Channel 8 is
; always refused - it is the tone tier's, and this driver always publishes
; DSV_TONE.
; -----------------------------------------------------------------------------
opl_claim:
    push ax
    push bx
    cmp cl, 8
    jae .bad                    ; 8 is the tone voice; 9+ does not exist
    mov bl, cl
    xor bh, bh
    pushf
    cli
    mov al, [opl_own+bx]
    cmp al, 0xFF
    je .take
    cmp al, dh
    je .mine
    popf
    jmp short .bad
.take:
    mov [opl_own+bx], dh
.mine:
    popf
    pop bx
    pop ax
    clc
    ret
.bad:
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; opl_owned - is this channel already this requester's? (internal)
; in:       CL = channel, DH = requester instance slot
; out:      CF = 0 yes; CF = 1 no (unclaimed, someone else's, out of range)
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
opl_owned:
    push ax
    push bx
    cmp cl, 9
    jae .no
    mov bl, cl
    xor bh, bh
    mov al, [opl_own+bx]
    cmp al, dh
    jne .no
    pop bx
    pop ax
    clc
    ret
.no:
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; opl_free - release one channel's claim (internal)
; in:       CL = channel
; out:      nothing
; clobbers: nothing (flags)
; -----------------------------------------------------------------------------
opl_free:
    push bx
    cmp cl, 9
    jae .out
    mov bl, cl
    xor bh, bh
    mov byte [opl_own+bx], 0xFF
.out:
    pop bx
    ret

; =============================================================================
; opl_fm_op - the body behind OSAPI_SND_FM (slot 0x00F8, SPEC.md 34.2)
;
; in:       AL = verb - 0 note-on (CL = channel, BX = Hz), 1 note-off (CL),
;           2 patch-load (CL, ES:SI -> 11 bytes ALREADY STAGED BY THE KERNEL
;           into its own segment), 3 all-off. DH = the requesting instance
;           slot, stamped by the kernel (SPEC.md 34.3).
; out:      CF = 1 refused / no FM sink
; clobbers: nothing else (flags)
;
; Note-on and patch-load claim the channel on first touch; note-off leaves
; the claim standing (a melody keeps its channels between notes); all-off
; keys off and releases everything this requester holds. A note-on refused
; for its FREQUENCY rolls back a claim it just created - a requester probing
; frequencies must not accumulate silent claims - but leaves a pre-existing
; claim, including patch-load's, untouched. The port writes run OUTSIDE the
; claim's IF=0 window: once the channel is claimed, same-tier interleaving is
; excluded by ownership, not by cli (SPEC.md 34.1).
; =============================================================================
opl_fm_op:
    push ax
    push bx
    push cx
    push dx
    push si
    push ds
    cmp al, 1
    jb .on
    je .off
    cmp al, 2
    je .patch
    cmp al, 3
    jne .bad
                                ; --- verb 3: all-off --------------------------
    mov cl, 0
.all:
    call opl_owned
    jc .next
    call opl_keyoff
    call opl_free
.next:
    inc cl
    cmp cl, 9
    jb .all
    jmp .ok
.on:                            ; --- verb 0: note-on --------------------------
    call opl_owned              ; CF = 1: not yet this requester's, so the
    mov al, 0                   ; claim below would be FRESH - AL remembers
    adc al, 0                   ; (AL is dead: the verb is decoded)
    call opl_claim
    jc .bad
    call opl_keyon              ; CF = frequency out of 19..6208
    jnc .ok
    or al, al                   ; a refused note-on leaves no side effect:
    jz .bad                     ; roll back a claim THIS call created, but
    call opl_free               ; never one that predates it
    jmp .bad
.off:                           ; --- verb 1: note-off -------------------------
    call opl_owned              ; only the owner silences a channel
    jc .bad
    call opl_keyoff
    jmp .ok
.patch:                         ; --- verb 2: patch-load -----------------------
    call opl_claim
    jc .bad
    call opl_patch              ; ES:SI is the kernel's staging buffer, and
                                ; DS stays OURS - opl_patch's own tables are
                                ; in this segment
.ok:
    clc
    jmp .out
.bad:
    stc
.out:
    pop ds
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; snd_release_both - the DSV_RELINST verb: give back EVERYTHING a dying
;                    instance holds, on BOTH halves of this driver
;
; in:       AL = instance slot
; out:      nothing
; clobbers: nothing (flags)
;
; The cell used to be `opl_release_inst` alone, which keys off FM channels and
; touches nothing of the Sound Blaster's - so a package that streamed and then
; closed left its staging grant behind, and with it the driver's 20KB staging
; POOL, which sbl_pool_put only releases once the last grant has gone. That is
; a 20KB claim stranded in the MIDDLE of the heap for the rest of the session
; (SPEC.md 50.3's warning about long-lived mid-heap claims), and the visible
; symptom was the next launch of the same app being refused its module buffer
; on a machine with room for it - reported from the field, reproduced here as
; System heap staying at 122K instead of returning to 102K after a close.
;
; Each half is gated on its own attach flag rather than on a published service
; word: snd_tier can take the DSP tier away and give it back all session
; (it clears DSV_STREAM to do it), and a teardown must still free memory for a
; card whose tier is currently parked.
; -----------------------------------------------------------------------------
snd_release_both:
    cmp byte [drv_up], 0        ; nothing attached at all
    je .out
    cmp byte [sbl_up], 0        ; the Sound Blaster's grants + staging pool
    je .fm
    call sbl_release_inst
.fm:
    cmp word [snd_services+DSV_FM], 0   ; the OPL's channels, if one attached
    je .out                             ; (DSV_FM is set once at attach and
    call opl_release_inst               ; never cleared, unlike DSV_STREAM)
.out:
    ret

; -----------------------------------------------------------------------------
; opl_release_inst - force-release every FM channel an instance holds
;                    (reached through snd_release_both, the DSV_RELINST cell)
; in:       AL = instance slot
; out:      nothing
; clobbers: nothing (flags)
;
; At most 9 single-write key-offs: bounded teardown work, and the dying
; instance can make no new claims.
; -----------------------------------------------------------------------------
opl_release_inst:
    push cx
    push dx
    mov dh, al
    mov cl, 0
.chan:
    call opl_owned
    jc .next
    call opl_keyoff
    call opl_free
.next:
    inc cl
    cmp cl, 9
    jb .chan
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; opl_state_init - the allocator's state, cleared
; in:       nothing
; out:      bitmap clear, owners 0xFF, key-off images 0
; clobbers: nothing (flags)
; -----------------------------------------------------------------------------
opl_state_init:
    push bx
    push cx
    mov bx, opl_own
    mov cx, 9
.own:
    mov byte [bx], 0xFF
    inc bx
    loop .own
    mov bx, opl_b0
    mov cx, 9
.b0:
    mov byte [bx], 0
    inc bx
    loop .b0
    pop cx
    pop bx
    ret

; =============================================================================
; opl_probe - the OPL2 timer-flag dance (SPEC.md 34.2, binding)
;
; in:       nothing
; out:      CF = 0 present - and the chip is FULLY initialised (registers
;           zeroed, waveform select on, default patch on all 9 channels)
;           before this returns, the publish-last idiom; CF = 1 absent
; clobbers: nothing else (flags)
;
; Mask/reset via 04h<-60h/80h, read status s1; timer-1 count FFh + start
; 21h; wait >= 80us by counted status reads; read s2; present iff
; (s1 & E0h) == 00h and (s2 & E0h) == C0h; clean up 04h<-60h/80h.
; =============================================================================
opl_probe:
    push ax
    push bx
    push cx
    push dx
    call opl_state_init         ; before any write: a reload starts clean
    mov ax, 0x0460              ; 04h <- 60h: mask both timers
    call opl_wr
    mov ax, 0x0480              ; 04h <- 80h: reset the IRQ flags
    call opl_wr
    mov dx, 0x388
    in al, dx
    mov bl, al                  ; s1: the flags must read clear
    mov ax, 0x02FF              ; timer 1 count FFh (overflows in 80us)
    call opl_wr
    mov ax, 0x0421              ; start timer 1, timer 2 masked
    call opl_wr
    mov cx, 200                 ; >= 80us of counted status reads on any
.wait:                          ; bus (an ISA `in` is ~1-6us; QEMU: instant)
    in al, dx
    loop .wait
    in al, dx
    mov bh, al                  ; s2: overflow + timer-1 flags must be set
    mov ax, 0x0460              ; clean up: mask + reset, leaving the flags
    call opl_wr                 ; clear for the next reader
    mov ax, 0x0480
    call opl_wr
    and bl, 0xE0
    jnz .absent
    and bh, 0xE0
    cmp bh, 0xC0
    jne .absent
    call opl_init               ; present: configure FULLY, then say so
    clc
    jmp short .out
.absent:
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; opl_init - one-time chip init
; in:       nothing
; out:      registers 01h..F5h zeroed, 01h <- 20h (waveform select enabled),
;           BDh <- 0, the default patch on all 9 channels
; clobbers: nothing (flags)
;
; ~245 writes ~= 25-70ms on the floor machine. Cold, once, at attach.
; -----------------------------------------------------------------------------
opl_init:
    push ax
    push cx
    push si
    mov ah, 0x01
.zero:
    xor al, al
    call opl_wr
    inc ah
    cmp ah, 0xF6                ; 01h..F5h inclusive
    jb .zero
    mov ax, 0x0120              ; waveform select enable
    call opl_wr
    mov ax, 0xBD00              ; BDh <- 0: melodic mode, no drums
    call opl_wr
    push es
    push ds
    pop es                      ; opl_patch reads the patch through ES, and
    mov cl, 0                   ; OUR default one is in our own segment
.patch:
    mov si, opl_defpatch        ; the default voice, channel by channel
    call opl_patch
    inc cl
    cmp cl, 9
    jb .patch
    pop es
    pop si
    pop cx
    pop ax
    ret

%include "sb.inc"               ; the Sound Blaster half (SPEC.md 34.5/34.6)

; =============================================================================
; State. A driver has no .bss: these ship zeroed inside the image
; (drivers/os88drv.inc).
; =============================================================================

drv_up:     db 0                ; 1 = attached, so detach knows there is work
opl_own:    times 9 db 0xFF     ; per-channel owner instance (0xFF = none)
opl_b0:     times 9 db 0        ; per-channel B0h image: the single-write
                                ; key-off's source (SPEC.md 8.2/34.3)

OS88_DRV_END
