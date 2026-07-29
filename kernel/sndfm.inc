; =============================================================================
; os8088 - sndfm.inc
;
; The AdLib/OPL2 driver (SPEC.md 34, Phase 3 of docs/SOUND-PLAN.md): the
; snd_drv row 1 ops. FM is the real background-music tier - a note-on is two
; register writes ~0.2-0.6ms on the floor, then ZERO CPU while it sounds -
; which is why snd_pref_tone lists DRV_OPL first: with an AdLib present the
; tone tier moves off the speaker for free.
;
; Port ownership (SPEC.md 34.1, binding): ALL access to 388h/389h goes
; through opl_wr, whose atomicity is sized honestly - pushf/cli covers only
; the address select at 388h, its 6 counted status reads and the data write
; at 389h; the 35-read post-data delay runs at the caller's IF (the address
; register is stable, and same-tier interleaving is excluded by the
; router's ownership, not by cli).
;
; The FM channel allocator (SPEC.md 34.3): a 9-bit claim bitmap plus a
; per-channel owner stamp. A channel is claimed by its first note-on or
; patch-load (the raw 0-8 index is the handle); a verb on a channel another
; requester holds is refused CF=1; verb 3 (all-off) and snd_release_inst
; release every channel a requester holds. CHANNEL 8 IS RESERVED out of the
; bitmap whenever OPL2 is in the tone route or the tone preference list -
; which, with snd_pref_tone as shipped, is always - so a package grabbing
; all FM channels can never steal the tone tier's voice.
;
; Sections (SPEC.md 33/34.7): every op the kernel dispatches to by near
; pointer - and opl_wr, which snd_tick's sanctioned key-off reaches through
; snd_town_off - stays in .text. The probe (the timer-flag dance) and the
; one-time init (~245 register writes, 25-70ms on the floor - fine cold at
; boot, never from ISR context) are far, behind the SDRV_PROBE FARSHIM;
; they KCALL back to opl_wr/opl_patch through their FARK wrappers. Far code
; keeps DS = KERNEL_SEG, so all data below stays in .text.
; =============================================================================

; --- OPL2 geometry and data (.text - far code reads it through DS) -----------

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

; The default patch, loaded onto all 9 channels by the far init and the
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
; status reads at the caller's IF (IF=1 from task context; snd_tick's
; sanctioned key-off and the 34.3 tone-grant window run whole writes at
; IF=0, bounded and rare - SPEC.md 8.2/34.1). ~430-1,300 cycles ~= 90-280us
; per write on the floor, of which <= ~100us is this routine's own IF=0
; window when entered at IF=1.
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
; opl_b0[ch] so key-off is the single-write path (SPEC.md 8.2/34.3).
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
;
; in:       CL = channel 0..8
; out:      nothing
; clobbers: nothing (flags)
;
; Rewrites the channel's stamped B0h value with KEY-ON cleared - one opl_wr,
; which is exactly the worst case SPEC.md 8.2 sanctions for snd_tick's
; routed-tone expiry (reached via snd_town_off -> opl_tone AX=0).
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
; in:       CL = channel 0..8, DS:SI -> 11 bytes: 5 modulator operator regs
;           (20h/40h/60h/80h/E0h values), 5 carrier, then C0h
; out:      nothing
; clobbers: nothing (flags)
;
; The FM op's verb-2 body and the far init's KCALL target (it stamps the
; default patch onto all 9 channels at probe time). 11 opl_wr calls.
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
    lodsb                       ; the patch byte
    call opl_wr
    inc bx
    cmp bx, 5
    jb .reg
    add dl, 3                   ; carrier = modulator slot + 3
    dec dh
    jnz .op
    mov ah, 0xC0
    add ah, cl                  ; C0h+ch: feedback/connection
    lodsb
    call opl_wr
    popf
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; opl_tone - the OPL2 row's tone op (SDRV_TONE, SPEC.md 34.2)
;
; in:       AX = frequency Hz (19..6208), or 0 = off; DL = voice (0 only)
; out:      CF = 0 done, CF = 1 unsupported voice/frequency
; clobbers: nothing else (flags)
;
; The routed tone tier's voice: channel 8, reserved out of the FM allocator
; (SPEC.md 34.3), sounding the default square-ish patch the far init loaded.
; Ownership, priority and expiry all live in snd_tone_req above this; off is
; the single-write key-off - snd_tick's sanctioned OPL2 touch (SPEC.md 8.2).
; A frequency above block 7's ceiling is CF=1 and the router leaves the
; incumbent owner sounding, exactly like a bad speaker divisor.
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
; opl_ch8_rsv - is channel 8 reserved for the tone tier? (internal)
; out: CF = 1 reserved (OPL2 is the tone route override, or appears in
;      snd_pref_tone - with the shipped list, always); CF = 0 free
; clobbers: nothing (flags)
; -----------------------------------------------------------------------------
opl_ch8_rsv:
    push ax
    push si
    cmp byte [snd_route+SND_TIER_TONE], DRV_OPL
    je .rsv
    mov si, snd_pref_tone
.scan:
    mov al, [si]
    inc si
    cmp al, 0FFh
    je .free
    cmp al, DRV_OPL
    jne .scan
.rsv:
    stc
    jmp .out
.free:
    clc
.out:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; opl_claim - claim a channel for a requester, first touch wins (internal)
;
; in:       CL = channel, DH = requester instance slot (0xFF = kernel)
; out:      CF = 0 claimed or already this requester's; CF = 1 refused -
;           channel out of range, reserved, or another requester's
; clobbers: nothing else (flags)
;
; The test-and-set and the owner stamp are one pushf/cli..popf unit
; (SPEC.md 34.3): two tasks claiming at once cannot both win.
; -----------------------------------------------------------------------------
opl_claim:
    push ax
    push bx
    cmp cl, 8
    ja .bad
    jb .try
    call opl_ch8_rsv            ; channel 8: only when the tone tier does
    jc .bad                     ; not hold the reservation (SPEC.md 34.3)
.try:
    mov ax, 1
    push cx
    shl ax, cl
    pop cx
    mov bl, cl
    xor bh, bh
    pushf
    cli
    test [opl_bits], ax
    jnz .held
    or [opl_bits], ax           ; free: claim + stamp, one IF=0 unit
    mov [opl_own+bx], dh
    popf
    clc
    jmp .out
.held:
    mov al, [opl_own+bx]        ; already claimed: only by this requester?
    popf                        ; (the compare runs AFTER popf - popf
    cmp al, dh                  ; restores flags, so a compare before it
    jne .bad                    ; would be judged on stale ones)
    clc
    jmp .out
.bad:
    stc
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; opl_owned - does this requester hold this channel? (internal)
; in:  CL = channel, DH = requester instance slot
; out: CF = 0 yes; CF = 1 out of range, unclaimed, or another's
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
opl_owned:
    push ax
    push bx
    cmp cl, 8
    ja .bad
    mov ax, 1
    push cx
    shl ax, cl
    pop cx
    test [opl_bits], ax
    jz .bad
    mov bl, cl
    xor bh, bh
    cmp [opl_own+bx], dh
    jne .bad
    clc
    jmp .out
.bad:
    stc
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; opl_free - release one claimed channel (internal)
; in:  CL = channel 0..8
; out: nothing
; clobbers: nothing (flags)
; -----------------------------------------------------------------------------
opl_free:
    push ax
    push bx
    mov ax, 1
    push cx
    shl ax, cl
    pop cx
    not ax
    mov bl, cl
    xor bh, bh
    pushf
    cli
    and [opl_bits], ax          ; bit + stamp cleared as one unit (34.3)
    mov byte [opl_own+bx], 0xFF
    popf
    pop bx
    pop ax
    ret

; =============================================================================
; opl_fm_op - the OPL2 row's FM op (SDRV_NOTE) and slot 0x0084's live body
;
; in:       AL = verb: 0 note-on (CL = channel 0..8, BX = frequency Hz),
;           1 note-off (CL), 2 patch-load (CL, DS:SI -> 11 bytes),
;           3 all-off
; out:      CF = 0 done; CF = 1 refused - bad verb/channel/frequency, the
;           channel is reserved or another requester's
; clobbers: nothing else (flags)
;
; The requester is the dispatched callback's instance, else the running
; task's (snd_req_inst - the same rule as every other sound grant,
; SPEC.md 34.3). Note-on and patch-load claim the channel on first touch;
; note-off leaves the claim standing (a melody keeps its channels between
; notes); all-off keys off and releases everything this requester holds.
; A note-on refused for its frequency ROLLS BACK a claim it just created
; (a requester probing frequencies must not accumulate silent claims), but
; leaves a pre-existing claim - including patch-load's - untouched.
; The port writes run OUTSIDE the claim's IF=0 window: once the channel is
; claimed, same-tier interleaving is excluded by ownership, not by cli
; (SPEC.md 34.1).
; =============================================================================
opl_fm_op:
    push ax
    push bx
    push cx
    push dx
    call snd_req_inst           ; DH = requester instance slot (0xFF kernel)
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
    call opl_free               ; never one that predates it (SPEC.md 34.2)
    jmp .bad
.off:                           ; --- verb 1: note-off -------------------------
    call opl_owned              ; only the owner silences a channel
    jc .bad
    call opl_keyoff
    jmp .ok
.patch:                         ; --- verb 2: patch-load -----------------------
    call opl_claim              ; a patch claims the channel too (34.3)
    jc .bad
    call opl_patch
.ok:
    clc
    jmp .out
.bad:
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; opl_release_inst - force-release every FM channel an instance holds
;
; in:       AL = instance slot
; out:      nothing
; clobbers: nothing (flags)
;
; snd_release_inst's FM leg (SPEC.md 34.3): key-off + free for each claimed
; channel stamped with the dying instance. At most 9 single-write key-offs -
; bounded teardown work, and the dying instance can make no new claims.
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
; opl_state_init - store the allocator's .bss before snd_live publishes
;
; in:       nothing (called by snd_init ahead of the probe loop)
; out:      nothing; bitmap clear, owners 0xFF, key-off images 0
; clobbers: nothing (flags)
; -----------------------------------------------------------------------------
opl_state_init:
    push ax
    push bx
    push cx
    mov word [opl_bits], 0
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
    pop ax
    ret

; =============================================================================
; The probe and the one-time init: cold far code (SPEC.md 33/34.7) behind
; the SDRV_PROBE FARSHIM. Register writes go through KCALL opl_wr /
; KCALL opl_patch; the status reads are direct port reads.
; =============================================================================
    FARSHIM opl_probe, opl_f_probe

section .fartext

; -----------------------------------------------------------------------------
; opl_f_probe - the OPL2 timer-flag dance (far body; SPEC.md 34.2, binding)
;
; in:       nothing (DS = KERNEL_SEG)
; out:      AL = 1 present - and the chip is FULLY initialised (registers
;           zeroed, waveform select on, default patch on all 9 channels)
;           before the 1 returns, the 29.2 publish-last idiom; AL = 0 absent
; clobbers: nothing else (flags)
;
; Mask/reset via 04h<-60h/80h, read status s1; timer-1 count FFh + start
; 21h; wait >= 80us by counted status reads; read s2; present iff
; (s1 & E0h) == 00h and (s2 & E0h) == C0h; clean up 04h<-60h/80h.
; -----------------------------------------------------------------------------
opl_f_probe:
    push bx
    push cx
    push dx
    push ax                     ; AH is restored on exit - only AL is the
                                ; documented output (SPEC.md 34.2)
    mov ax, 0x0460              ; 04h <- 60h: mask both timers
    KCALL opl_wr
    mov ax, 0x0480              ; 04h <- 80h: reset the IRQ flags
    KCALL opl_wr
    mov dx, 0x388
    in al, dx
    mov bl, al                  ; s1: flags must read clear
    mov ax, 0x02FF              ; timer 1 count FFh (overflows in 80us)
    KCALL opl_wr
    mov ax, 0x0421              ; start timer 1, timer 2 masked
    KCALL opl_wr
    mov cx, 200                 ; >= 80us of counted status reads on any
.wait:                          ; bus (an ISA in is ~1-6us; QEMU: instant)
    in al, dx
    loop .wait
    in al, dx
    mov bh, al                  ; s2: overflow + timer-1 flags must be set
    mov ax, 0x0460              ; clean up: mask + reset, leaving the
    KCALL opl_wr                ; flags clear for the next reader
    mov ax, 0x0480
    KCALL opl_wr
    and bl, 0xE0
    jnz .absent
    and bh, 0xE0
    cmp bh, 0xC0
    jne .absent
    call opl_f_init             ; present: configure FULLY, then say so
    mov al, 1
    jmp .out
.absent:
    xor al, al
.out:
    pop bx                      ; the entry AX (BX itself is restored below)
    mov ah, bh                  ; put the caller's AH back around the result
    pop dx
    pop cx
    pop bx
    retf                        ; far body: reached through the shim

; -----------------------------------------------------------------------------
; opl_f_init - one-time chip init (far, near-called by the probe)
;
; in:       nothing (DS = KERNEL_SEG)
; out:      nothing; registers 01h..F5h zeroed, 01h <- 20h (waveform select
;           enabled), BDh <- 0, the default patch on all 9 channels
; clobbers: nothing (flags)
;
; ~245 writes ~= 25-70ms on the floor - cold boot-time work, exactly why
; this body is far (SPEC.md 33/34.7).
; -----------------------------------------------------------------------------
opl_f_init:
    push ax
    push cx
    push si
    mov ah, 0x01
.zero:
    xor al, al
    KCALL opl_wr
    inc ah
    cmp ah, 0xF6                ; 01h..F5h inclusive
    jb .zero
    mov ax, 0x0120              ; waveform select enable
    KCALL opl_wr
    mov ax, 0xBD00              ; BDh <- 0: melodic mode, no drums
    KCALL opl_wr
    mov cl, 0
.patch:
    mov si, opl_defpatch        ; the default voice, channel by channel
    KCALL opl_patch
    inc cl
    cmp cl, 9
    jb .patch
    pop si
    pop cx
    pop ax
    ret                         ; near: called from opl_f_probe (same CS)

section .text

; =============================================================================
; state (SPEC.md 34.7) - stored by opl_state_init from snd_init, before
; snd_live publishes; unreadable until it does
; =============================================================================
section .bss

opl_bits:   resw 1              ; FM channel claim bitmap, bits 0..8 (34.3)
opl_own:    resb 9              ; per-channel owner instance (0xFF = none)
opl_b0:     resb 9              ; per-channel B0h image: the single-write
                                ; key-off's source (SPEC.md 8.2/34.3)

section .text
