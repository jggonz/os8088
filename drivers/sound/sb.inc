; =============================================================================
; os8088 - sndsb.inc
;
; The Sound Blaster driver (SPEC.md 34.5/34.6, Phase 4 of docs/history/SOUND-PLAN.md):
; snd_drv row 2. The only self-clocked PCM tier - DMA paces itself, one IRQ
; per 2KB block (256ms at 8kHz) - so this is the only tier where "samples
; play while the GUI runs" is promised (SND_CAP_PCM_BG).
;
; Detection (far, behind the SDRV_PROBE FARSHIM): reset scan over the base
; list with a bounded poll budget per base, DSP version via E1h, auto-init
; strategy gate at DSP >= 2.00. IRQ discovery is DEFERRED TO FIRST USE
; (SPEC.md 34.5), two tiers: a DSP >= 4.00 reads the line straight from
; CT1745 mixer register 80h (deterministic, nothing fired); older DSPs hook
; candidates {7,5,3,2} onto the .text stubs below, F2h triggers an 8-bit
; IRQ, and the candidate that fires is accepted (retried once; 2xEh bit 7
; is NOT a confirm - it means "response byte waiting" and F2h queues none
; outside QEMU); the losers are unhooked, and the winner is hooked
; mouse_init-style (save old
; vector, install while masked, unmask) onto sbl_isr, staying hooked until
; snd_unhook.
;
; sbl_isr (mou_isr discipline: IF=0 throughout, DS=KERNEL_SEG, no BIOS,
; never sti, own EOI): the spurious-IRQ-7 guard first - the interrupt is
; SB's only if sbl_expect is set AND 2xEh bit 7 confirms; an unexpected
; IRQ 7 asks the 8259's in-service register (OCW3 0Bh) whether it is real
; - ISR bit 7 set gets an EOI (the SB's factory default IS line 7), clear
; is truly spurious and gets none; a non-7 stray is always EOId so the
; line cannot wedge. The 2xEh confirm read IS the SB-side ACK. Then the
; half-swap: the finished half is flagged for the refill task; if the next
; half was never refilled the ISR pauses output (D0h) and marks the stream
; underrun-paused - bounded silence and a visible status, NEVER stale audio
; looping (SPEC.md 34.5, binding). Single-cycle DSPs (< 2.00) re-arm DMA +
; 14h for the other half here; auto-init DSPs have nothing to re-arm.
;
; DMA recipe (SPEC.md 34.5, binding): channel 1 only - never channel 2, the
; floppy's. Mask 0Ah=05h; each 16-bit value is flip-flop clear via 0Ch then
; two successive byte writes (lo, hi); page 3 -> 83h; mode 49h single /
; 59h auto-init (memory->card reads); unmask 0Ah=01h. NOTHING is multiplied
; by 2 - address doubling is a 16-bit-channel concept the XT does not have.
; The double buffer is 2 x 2KB at SND_SEG:0 - wholly inside 8237 physical
; page 3, page-crossing-safe by construction (SPEC.md 2.2).
;
; The kernel owns stream pacing (SPEC.md 34.5): verb 0 spawns a TRANSIENT
; kernel task from the 12-slot pool that copies grant -> double-buffer
; halves as the ISR flags them consumed, resumes an underrun-paused stream
; (D4h) once data reappears, and exits at close, teardown, watchdog stop or
; a superseding open. Packages never hold an ES pointer into SND_SEG: data
; crosses through the verb-6 staging copy (the dsk_get_dir idiom).
;
; ...UNLESS THE STREAM IS DIRECT (SPEC.md 34.5.2). A ring stream whose ring the
; 8237 can reach - auto-init, two halves or more, no 64KB page crossed - is
; played where it lies: the DMA cycle IS the ring, there is no double buffer,
; no refill task and no copy, the ISR asks the counters rather than a valid
; flag, verb 1 resumes an underrun, and verb 0 pins the pool for the stream's
; life. The double buffer is claimed per stream for everything else and freed
; with it, so an idle card holds its image and nothing more.
;
; Verbs 1/3/5/6/7 are ANY-TASK (SPEC.md 20.3's per-verb context rule): a
; package's worker may feed, poll, read, stage and hold grants. Every
; check-then-act those verbs run is one pushf/cli window (SPEC.md 34.3),
; the verb-5/6 caller segment arrives per-call in BX from the slot wrapper,
; and grant attribution rides the task-qualified [snd_inst] stamp
; (snd_req_inst). Verbs 0/2/4 stay UI-callback-only - the open busy check
; and the act=1 publish are many IF=1 instructions apart.
;
; RING MODE (SPEC.md 34.5; verb 0, AH bit 0 = SND_OPENF_RING): the same
; total/fed/consumed become free-running 16-bit counters, the physical
; offset of stream byte n is str_off + (n & sbl_rmask), fills are whole
; 2048-byte halves only and never pad, and the feed bounds are the two
; subtractions of SPEC.md 20.3 verb 1. What it buys: endless playback out
; of a small grant with no close+reopen seam - a worker mixes, verb-6
; stages at ringbase + (total & rmask), and verb-1 feeds, forever.
;
; Recording (SPEC.md 34.6, Phase 5) is the mirror image on the same stream
; record: verb 4 opens an input stream (DSP 24h single-cycle / 2Ch
; auto-init per the version gate, DMA modes 45h/55h) capturing into the
; 8KB record ring at SND_SEG:0x1000 (2 x 4KB halves); a transient kernel
; DRAIN task moves completed ring halves into the caller's grant - in
; act+gen-guarded IF=0 chunks, the teardown fence (SPEC.md 34.6) - until
; its stated capacity fills, then stops the DSP - the stream reads paused(1)
; with DX = capacity, the input mirror of data exhaustion. The ISR's input
; leg flags captured halves and, if the drain starved (the half it must
; capture into next is still un-drained), pauses input - bounded loss and
; a visible status, never silent overwrite. Half-duplex with playback is
; the ROUTER'S owner record: one stream record with a direction byte, so
; open-in while an out-stream is open (and vice versa) is AX = 1 busy by
; construction. Consumers read captured bytes via the verb-5 staging copy
; (grant -> caller DS:DI - the verb-6 idiom in reverse); verb 1 feed on an
; input stream is err 7 (a record stream has no valid length to extend).
; The snd_tick watchdog covers input identically: block IRQs that stop
; arriving halt the stream as ended(2).
;
; Known bound, recorded: on single-cycle DSPs the re-arm programs the 8237
; from the ISR, and the 8237's byte-pair flip-flop is shared with the BIOS's
; channel-2 programming inside int 13h - a floppy read concurrent with a
; single-cycle stream can interleave the pairs. Auto-init hardware (every
; DSP >= 2.00, the QEMU-verified path) never programs DMA from the ISR and
; has no such window. The single-cycle branch is 86Box/real-hardware work
; (SPEC.md 34.5's pinned fallback covers refusing < 2.00 if it fails there).
; =============================================================================

; --- geometry and states -----------------------------------------------------
SBL_HALF     equ 2048           ; double-buffer half size (SPEC.md 2.2)
SBL_RING     equ 0x1000         ; record ring base offset (SPEC.md 2.2)
SBL_RHALF    equ 2048           ; record ring half size (2 x 2KB = 4KB). It
                                ; was 4KB halves, and what pays for the cut is
                                ; the drain task's own pacing bound: one tick
                                ; of sleep against a half that is 2.49 ticks
                                ; even at the 15kHz input ceiling (it was
                                ; 4.97). Still over 2x the pacer, where the
                                ; PLAYBACK half has no such room - halving
                                ; SBL_HALF puts a block UNDER one tick above
                                ; ~18.6kHz and silently underruns the 22 and
                                ; 44kHz rates (SPEC.md 34.5)
SBL_DMASZ    equ 0x2000         ; the DMA-visible claim: double buffer +
                                ; record ring, 8KB - was 12KB. THE ONLY PART
                                ; THE 8237 ADDRESSES, and so the only part
                                ; that has to obey its 64KB page rule, which
                                ; a smaller claim is strictly easier to place
                                ; against. The wide-rate double buffer is
                                ; 2 x 4KB and so fills this exactly; it
                                ; overlaps the record ring, which is legal
                                ; because a stream is half-duplex by
                                ; construction (SPEC.md 34.6) and the guards
                                ; below hold the arithmetic
SBL_POOLKB   equ 20             ; the staging pool's CEILING, in KB: a
                                ; SEPARATE claim taken on the first grant and
                                ; released with the last (SPEC.md 51.4). Grant
                                ; offsets are relative to ITS base. It is sized
                                ; to what the first grant asks for (SPEC.md
                                ; 34.6.3) and grows toward this - it used to
                                ; take all 20KB whatever the ask, so a Tracker
                                ; playing an 8KB ring held 12KB for nothing
SBL_PLAYKB   equ 16             ; what the driver holds while it does its
                                ; JOB, playing - the 16KB ring Tracker and
                                ; Audio both ask for, played in place
                                ; (SPEC.md 34.5.2), so it is the pool and
                                ; nothing else. The Drivers page prices it
                                ; (DRVM_SND); recording adds the 8KB buffer
SBL_POOLMIN  equ 4              ; the smallest pool worth holding: a tiny
                                ; first grant still leaves room for a scratch
                                ; one beside it (sbtest's verb 5/6 round trip)
%if SBL_POOLMIN > SBL_POOLKB
  %error "SBL_POOLMIN above the pool's ceiling"
%endif
SBL_POOLMAX  equ 0xD000         ; the biggest grant the offset arithmetic can
                                ; express. The live bound is [sbl_poolsz],
                                ; which is 0 until the pool is claimed
SBL_GMAX     equ 8              ; staging-grant records
; SBL_MOVS - the copy every pool/ring move uses: WORDS, then the odd byte.
; `rep movsw` is 13.3 clocks a byte on an 8088 (PERFORMANCE.md Set 117.2)
; against `rep movsb`'s 17,
; and these copies are two passes over every byte a stream plays - 8.3% of a
; 4.77 MHz machine at 11 kHz by sample, and each is inside a cli window that
; gets shorter by the same factor. In: CX bytes, DS:SI -> ES:DI, DF = 0.
; Out: CX = 0, SI/DI advanced as `rep movsb` would. Flags: CF, not preserved.
%macro SBL_MOVS 0
    shr cx, 1
    rep movsw
    adc cx, cx                  ; the odd byte: CX is 0 here, so it is CF
    rep movsb
%endmacro

SBL_DCHUNK   equ 512            ; drain-copy chunk: one act+gen-guarded
                                ; IF=0 write unit (SPEC.md 34.6, ~2ms of
                                ; rep movsb on a 4.77MHz 8088)

; --- derived from the geometry above, and that is the whole point ------------
; The watchdog fires at 2 block periods + 3 ticks, so its dividend is
; 2 * block * 18.2 - three magic numbers (74,547 / 149,094 / 149,095) that
; were computed by hand and had no link to the constants they came from. A
; block size changed without them is a watchdog that fires at the wrong time
; and says nothing about it, which is the failure this whole block exists to
; make impossible. 182/10 keeps NASM's integer division on the value the
; original three were derived from.
SBL_WD_OUT   equ (2 * SBL_HALF * 182) / 10        ; legacy playback
SBL_WD_WIDE  equ (2 * (2*SBL_HALF) * 182) / 10    ; wide-rate playback
SBL_WD_IN    equ (2 * SBL_RHALF * 182) / 10       ; capture

; --- and the layout rules the comments used to state and nothing enforced ----
; Each of these held by accident at the old geometry: the record ring began
; exactly where the double buffer ended, and SBL_RING happened to equal
; SBL_RHALF. Both are coincidences a resize breaks silently, so they are
; assembly-time failures now rather than prose.
%if SBL_RING < 2 * SBL_HALF
  %error "SBL_RING overlaps the legacy double buffer"
%endif
%if SBL_RING + 2 * SBL_RHALF > SBL_DMASZ
  %error "the record ring does not fit SBL_DMASZ"
%endif
%if 2 * (2 * SBL_HALF) > SBL_DMASZ
  %error "a wide-rate double buffer does not fit SBL_DMASZ"
%endif
%if SBL_WD_WIDE != 2 * SBL_WD_OUT
  %error "the wide watchdog is no longer twice the legacy one - sbl_v_open's .wdcalc shifts"
%endif
%if ((SBL_HALF & 0xFF) != 0) || (((2*SBL_HALF) >> 8) == (SBL_HALF >> 8))
  %error "sbl_v_open's .wdcalc tells the two halves apart by their high byte"
%endif

SBL_ST_PLAY  equ 0              ; stream states (verb 3, SPEC.md 20.3):
                                ;   out = playing, in = recording
SBL_ST_UNDER equ 1              ;   out = underrun-paused (resumable by
                                ;   feed); in = paused (grant capacity
                                ;   full, or the drain task starved)
SBL_ST_END   equ 2              ;   stopped by the snd_tick watchdog
%if SBL_ST_PLAY != 0 || SBL_ST_UNDER != 1 || SND_OPENF_RING != 1
  %error "the record stores PLAY as a zero register and RING as a 0/1 byte"
%endif

SBL_POLLS    equ 4096           ; DSP handshake poll budget (bounded even
                                ; from ISR context: ~4-8ms of ISA reads)

; --- detection tables (.text data - far code reads them through DS) ----------
sbl_bases:                      ; the pinned scan order (SPEC.md 34.2)
    dw 0x220, 0x240, 0x210, 0x230, 0x250, 0x260, 0

sbl_dsc_irqn: db 7, 5, 3, 2     ; IRQ candidates, pinned order (SPEC.md 34.5)
sbl_dsc_mbit: db 0x80, 0x20, 0x08, 0x04     ; 8259 mask bits
sbl_dsc_t:                      ; per candidate, in the same order: the IVT
    dw 0x3C, sbl_stub7, 0       ; byte offset (int 0Fh/0Dh/0Bh/0Ah), then a
    dw 0x34, sbl_stub5, 0       ; FAR POINTER that sbl_f_irqdisc's .swap
    dw 0x2C, sbl_stub3, 0       ; exchanges with the vector: the stub goes
    dw 0x28, sbl_stub2, 0       ; in and the old vector lands here, and the
SBL_DSC_N    equ 4              ; second swap puts both back. The segment
                                ; words are stamped with our CS first

; =============================================================================
; DSP handshake primitives (.text - ISR-adjacent, SPEC.md 34.7). Every poll
; carries a timeout (SPEC.md 34.5, binding).
; =============================================================================

; -----------------------------------------------------------------------------
; sbl_dsp_wr - write one byte to the DSP command/data port
; in:       AL = byte; [sbl_base] set
; out:      CF = 1 the DSP never went ready (timeout)
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
sbl_dsp_wr:
    push ax
    push cx
    push dx
    mov ah, al                  ; park the byte
    mov dx, [sbl_base]
    add dx, 0x0C                ; 2xCh: write-buffer status
    mov cx, SBL_POLLS
.poll:
    in al, dx
    test al, 0x80               ; bit 7 clear = ready for a write
    jz .ok
    loop .poll
    stc
    jmp .out
.ok:
    mov al, ah
    out dx, al                  ; 2xCh is also the write port
    clc
.out:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sbl_dsp_rd - read one byte from the DSP
; in:       [sbl_base] set
; out:      CF = 0, AL = byte; CF = 1 nothing arrived (timeout, AL trash)
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
sbl_dsp_rd:
    push cx
    push dx
    mov dx, [sbl_base]
    add dx, 0x0E                ; 2xEh: read-buffer status
    mov cx, SBL_POLLS
.poll:
    in al, dx
    test al, 0x80               ; bit 7 set = data available
    jnz .ok
    loop .poll
    stc
    jmp .out
.ok:
    sub dx, 4                   ; 2xAh: read data
    in al, dx
    clc
.out:
    pop dx
    pop cx
    ret

; =============================================================================
; DMA channel 1 + DSP transfer control (.text)
; =============================================================================

; -----------------------------------------------------------------------------
; sbl_arm_half - program a single-cycle transfer of one half (DSP < 2.00)
;
; in:       AL = half (0/1); rate already set via 40h
; out:      nothing
; clobbers: nothing (flags)
;
; The DMA byte pairs run under pushf/cli (the 8237 flip-flop is shared
; state); the DSP 14h + length writes are bounded polls. Called from verb-0
; start and from sbl_isr's re-arm branch - the ~40-instruction audible-gap
; mitigation on 1.x DSPs (SPEC.md 34.5).
; -----------------------------------------------------------------------------
sbl_arm_half:
    push ax
    push cx
    push dx
    xor dx, dx                  ; DX = half * SBL_HALF: the offset INSIDE
    or al, al                   ; our buffer. A half is 0 or 1, so the
    jz .base                    ; stride is a test and an add rather than a
    mov dx, SBL_HALF            ; hand-counted shift - DERIVED, so a change
.base:                          ; to the geometry cannot leave it behind
    mov ax, 0x4914              ; AH = DMA single mode, read (memory -> card),
    jmp short sbl_arm_rec.c     ; ch 1; AL = DSP 14h, 8-bit single-cycle out

; -----------------------------------------------------------------------------
; sbl_arm_rec - program a single-cycle capture of one ring half (DSP < 2.00)
;
; in:       AL = half (0/1); rate already set via 40h
; out:      nothing
; clobbers: nothing (flags)
;
; The input mirror of sbl_arm_half: DMA mode 45h (single, write - card to
; memory), the half's ring offset (SBL_RING + half * SBL_RHALF), then DSP
; 24h (8-bit single-cycle ADC) + length. Called from verb-4 start and from
; sbl_isr's input re-arm branch (SPEC.md 34.6).
;
; ONE BODY, sbl_arm_rec.c, serves both: the two differed in the offset, the mode
; byte and the command byte and in nothing else. What it costs the ISR's
; re-arm (single-cycle DSPs only, once per block) is the two immediate loads
; and arm_half's short jump - ~23 cycles in a block of >= 93 ms.
; -----------------------------------------------------------------------------
sbl_arm_rec:
    push ax
    push cx
    push dx
    mov dx, SBL_RING            ; ring offset = SBL_RING + half * SBL_RHALF.
    or al, al                   ; It used to be (half+1) << 12, which is the
    jz .base                    ; same number ONLY while SBL_RING happens to
    add dx, SBL_RHALF           ; equal SBL_RHALF - a coincidence of the old
.base:                          ; geometry, never a layout rule (SPEC.md 2.2)
    mov ax, 0x4524              ; AH = DMA single mode, write (card ->
                                ; memory), ch 1; AL = DSP 24h, 8-bit
                                ; single-cycle in. FALLS THROUGH
%if SBL_HALF != SBL_RHALF
  %error "sbl_arm_rec.c programs one length for both directions"
%endif
.c:                             ; DX = offset in our buffer, AH = mode, AL =
    mov cl, al                  ; the DSP command; the caller pushed AX/CX/DX
    add dx, [sbl_dmaoff]        ; ...and this is where that buffer sits in
                                ; its 8237 page, computed at its claim
                                ; (sbl_dma_get). Nothing is doubled - address
                                ; doubling is a 16-bit-channel concept the XT
                                ; does not have
    pushf
    cli
    mov al, 0x05
    out 0x0A, al                ; mask channel 1
    out 0x0C, al                ; clear the flip-flop (any value)
    mov al, dl
    out 0x02, al                ; address lo
    mov al, dh
    out 0x02, al                ; address hi
    out 0x0C, al                ; clear the flip-flop again
    mov al, (SBL_HALF-1) & 0xFF
    out 0x03, al                ; count lo (len - 1)
    mov al, (SBL_HALF-1) >> 8
    out 0x03, al                ; count hi
    mov al, ah
    out 0x0B, al                ; the mode
    mov al, [sbl_page]          ; the 8237 page our claim landed in
    out 0x83, al
    mov al, 0x01
    out 0x0A, al                ; unmask channel 1
    popf
    mov al, cl                  ; the DSP command, then the length
    call sbl_dsp_wr
    mov al, (SBL_HALF-1) & 0xFF
    call sbl_dsp_wr
    mov al, (SBL_HALF-1) >> 8
    call sbl_dsp_wr
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sbl_hw_start - program the card and start the opened stream's playback
;
; in:       [sbl_tc], [sbl_auto], the double buffer filled; task context
; out:      nothing
; clobbers: nothing (flags)
;
; Time constant (40h), speaker on (D1h), then the mode split: auto-init
; programs the DMA once for the whole cycle (59h) plus the DSP block size
; (48h, one half) and 1Ch; single-cycle arms half 0 (SPEC.md 34.5). The
; cycle is [sbl_xoff]/[sbl_xpage]/[sbl_xcnt]: the double buffer, or - a
; DIRECT stream (SPEC.md 34.5.2) - the package's own ring in the pool.
; -----------------------------------------------------------------------------
sbl_hw_start:
    push ax
    push bx
    push cx
    cmp byte [sbl_wide], 0      ; the wide-rate regime (SPEC.md 34.5, DSP
    je .tc                      ; >= 4.00): 41h takes the rate in Hz,
    mov bx, [sbl_rate]          ; big-endian, in place of the TC
    xchg bl, bh
    mov al, 0x41
    call sbl_dsp_wr3
    jmp short .spk
.tc:
    mov al, 0x40                ; time constant
    call sbl_dsp_wr
    mov al, [sbl_tc]
    call sbl_dsp_wr
.spk:
    mov al, 0xD1                ; speaker (DAC) on
    call sbl_dsp_wr
    cmp byte [sbl_auto], 0
    je .single
    mov ax, [sbl_xoff]          ; the cycle: the double buffer's 2*half, or
    mov bx, [sbl_xcnt]          ; a DIRECT ring's RL (SPEC.md 34.5.2); verb 0
    mov cl, 0x59                ; set all three. Auto-init, read, channel 1
    mov ch, [sbl_xpage]
    call sbl_dma_auto
    mov bx, [sbl_half]          ; BX = DSP block length - 1
    dec bx
    mov ah, 0x1C                ; the 48h regime: block size (one half - 1)
    cmp byte [sbl_hisp], 0      ; then ONE start byte, and that byte is the
    je .blkchk                  ; whole difference between legacy auto-init
    mov ah, 0x90                ; (1Ch) and SB Pro high-speed (90h). After
    jmp short .block            ; 90h the DSP takes NO command until a reset
.blkchk:                        ; - so sbl_halt masks the 8237 and
    cmp byte [sbl_wide], 0      ; sbl_stop_stream resets, never D0h/DAh
    je .block
    mov al, 0xC6                ; wide start: C6h, mode 00h (8-bit mono
    call sbl_dsp_wr             ; unsigned), block length - 1 lo/hi -
    xor al, al                  ; halt/continue/DAh-exit are unchanged,
    call sbl_dsp_wr3            ; so close and resume need no second path
    jmp short .out
.block:
    mov al, 0x48                ; DSP block size = one half
    call sbl_dsp_wr3
    cmp byte [sbl_adpcm], 0     ; ...or 7Dh, auto-init 4-bit ADPCM with its
    je .start                   ; reference byte (SPEC.md 34.5.3), which
    mov ah, 0x7D                ; only the legacy regime on a DSP < 4.00
                                ; reaches (the open refuses it otherwise)
.start:
    mov al, ah                  ; 1Ch legacy / 90h high-speed / 7Dh ADPCM4
    call sbl_dsp_wr
    jmp short .out
.single:
    mov al, 0
    call sbl_arm_half           ; half 0 (always 2KB: a DSP < 2.00 can
                                ; never open wide); the ISR re-arms
.out:
    pop cx
    pop bx
    pop ax
    ret
; -----------------------------------------------------------------------------
; sbl_hw_start_in - program the card and start the opened stream's capture
;
; in:       [sbl_tc], [sbl_auto]; task context
; out:      nothing
; clobbers: nothing (flags)
;
; The input mirror of sbl_hw_start: speaker (DAC) OFF first - a live DAC
; feeds the output back into the capture path on real hardware - then the
; time constant, then the mode split: auto-init programs DMA once for the
; whole 8KB ring (55h at SBL_RING) plus the DSP block size (48h, one 4KB
; half) and 2Ch; single-cycle arms half 0 via sbl_arm_rec (SPEC.md 34.6).
; -----------------------------------------------------------------------------
sbl_hw_start_in:
    push ax
    push bx
    push cx
    mov al, 0xD3                ; speaker (DAC) off for the capture path
    call sbl_dsp_wr
    mov al, 0x40                ; time constant
    call sbl_dsp_wr
    mov al, [sbl_tc]
    call sbl_dsp_wr
    cmp byte [sbl_auto], 0
    je .single
    mov ax, [sbl_dmaoff]        ; address = the ring, in its 8237 page
    add ax, SBL_RING
    mov bx, 2*SBL_RHALF-1       ; count = the whole ring - 1
    mov cl, 0x55                ; auto-init, write (card -> memory), ch 1
    mov ch, [sbl_page]
    call sbl_dma_auto
    mov bx, SBL_RHALF-1         ; DSP block size = one ring half
    mov al, 0x48
    call sbl_dsp_wr3
    mov al, 0x2C                ; auto-init 8-bit DMA input
    call sbl_dsp_wr
    jmp short .out
.single:
    mov al, 0
    call sbl_arm_rec            ; half 0; the ISR re-arms from there
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sbl_dma_auto - program DMA channel 1 for an AUTO-INIT cycle: both starts'
;                one recipe (SPEC.md 34.5, binding)
; in:       AX = the address inside the page, BX = the count - 1, CL = the
;           mode byte (59h playback, 55h capture), CH = the page
; out:      nothing; clobbers AL (flags)
;
; Mask, flip-flop, address lo/hi, flip-flop, count lo/hi, mode, page, unmask,
; under one pushf/cli: the flip-flop is shared state. Called from task
; context at a start, never from the ISR - the single-cycle re-arms that DO
; run in the ISR (sbl_arm_half / sbl_arm_rec) keep their own inline sequence
; -----------------------------------------------------------------------------
sbl_dma_auto:
    pushf
    cli
    push ax
    mov al, 0x05
    out 0x0A, al                ; mask channel 1
    out 0x0C, al                ; flip-flop clear (any value)
    pop ax
    out 0x02, al                ; address lo
    mov al, ah
    out 0x02, al                ; address hi
    out 0x0C, al                ; flip-flop clear
    mov al, bl
    out 0x03, al                ; count lo
    mov al, bh
    out 0x03, al                ; count hi
    mov al, cl
    out 0x0B, al                ; mode
    mov al, ch
    out 0x83, al                ; the page our cycle sits in
    mov al, 0x01
    out 0x0A, al                ; unmask
    popf
    ret

; sbl_dsp_wr3 - AL, then BL, then BH, to the DSP: a command and its 16-bit
; operand low byte first. Every register preserved (flags not)
sbl_dsp_wr3:
    push ax
    call sbl_dsp_wr
    mov al, bl
    call sbl_dsp_wr
    mov al, bh
    call sbl_dsp_wr
    pop ax
    ret
; -----------------------------------------------------------------------------
; sbl_halt - pause DSP DMA requests (D0h, bounded write-poll)
; in/out: nothing; clobbers nothing (flags)
;
; High-speed (SB Pro, SPEC.md 34.5): the DSP ignores D0h once 90h has run, so
; mask DMA channel 1 at the 8237 instead - one OUT, ISR-safe, and the DSP just
; sees its requests dry up. sbl_go_on unmasks; the transfer picks up from the
; 8237's live address/count with the DSP never having left high-speed mode.
; -----------------------------------------------------------------------------
sbl_halt:
    push ax
    cmp byte [sbl_hisp], 0
    je .cmd
    mov al, 0x05               ; mask 8-bit DMA channel 1
    out 0x0A, al
    pop ax
    ret
.cmd:
    mov al, 0xD0
    call sbl_dsp_wr
    pop ax
    ret

; -----------------------------------------------------------------------------
; sbl_resume - take a paused stream back to PLAYING and restart the card
; in:  a paused stream, IF = 0 - the caller's own window, which has verified
;      act and generation first (SPEC.md 34.3)
; out: nothing; clobbers AX (flags)
;
; The one body behind the three resumes - verb 1 (a DIRECT stream fed again),
; the refill task and the drain task: flags, expectation and watchdog, then
; sbl_go_on. The expectation is armed BEFORE the card restarts, for the
; first-IRQ race sbl_v_open describes.
; -----------------------------------------------------------------------------
sbl_resume:
    xor ax, ax
    mov [sbl_paused], al
    mov [sbl_str_state], al     ; SBL_ST_PLAY
    inc ax
    mov [sbl_expect], al
    mov ax, [sbl_wd_rst]
    mov [sbl_wd], ax
    ; FALLS THROUGH into sbl_go_on

; -----------------------------------------------------------------------------
; sbl_go_on - continue a paused stream: D4h on auto-init hardware, a fresh
;             half arm on single-cycle (nothing was in flight there)
; in:  [sbl_play] = the half to sound (or capture into) next; [sbl_str_dir]
; out: nothing; clobbers nothing (flags)
; -----------------------------------------------------------------------------
sbl_go_on:
    push ax
    cmp byte [sbl_hisp], 0
    je .noths
    mov al, 0x01               ; high-speed: unmask DMA channel 1 (the DSP
    out 0x0A, al               ; never left high-speed mode - no D4h)
    jmp .out
.noths:
    cmp byte [sbl_auto], 0
    je .single
    mov al, 0xD4                ; continue 8-bit DMA, either direction
    call sbl_dsp_wr
    jmp .out
.single:
    mov al, [sbl_play]
    cmp byte [sbl_str_dir], 0
    jne .rec
    call sbl_arm_half
    jmp .out
.rec:
    call sbl_arm_rec
.out:
    pop ax
    ret

; =============================================================================
; sbl_isr - the SB block-IRQ handler (SPEC.md 34.5, binding)
;
; mou_isr discipline: push used regs + DS + ES, DS = the DRIVER's own
; segment (CS - this ISR lives in a loaded image, not the kernel), IF=0
; throughout, no BIOS, never sti, own EOI (out 20h, 20h - the BIOS does not
; handle this IRQ). Never touches VGA, gfx_lock or the switch path.
;
; Guard first: the interrupt is SB's only if sbl_expect is set AND 2xEh
; bit 7 confirms - and that confirm read IS the SB-side ACK. An unexpected
; IRQ on line 7 may be either spurious (the 8259 sets no ISR bit) or REAL
; (an SB on line 7 - the factory default - e.g. a block IRQ latched just
; before a close): the 8259's in-service register decides (OCW3 0Bh, then
; read 20h) - ISR bit 7 set is EOId, clear irets with no EOI. A non-7
; stray is always EOId, because a real unacknowledged ISR bit would block
; its whole priority level forever (a non-specific EOI from a higher-
; priority handler always retires the higher bit, never this one).
;
; Then the half-swap: consumed += one block (capped at the valid length),
; the finished half is invalidated and left for the refill task, the
; watchdog rewinds. If the next half was refilled: auto-init hardware needs
; nothing, single-cycle re-arms it (sbl_arm_half). If it was NOT refilled -
; data ran out or the refill task starved - pause (D0h on auto-init; a
; single-cycle DSP simply is not re-armed), bump sbl_under, mark the
; stream underrun-paused and stop expecting IRQs; the refill task resumes
; with sbl_go_on once data reappears (SPEC.md 34.5, the underrun contract).
; =============================================================================
sbl_isr:
    push ax
    push bx
    push dx
    push ds
    push es
    push cs
    pop ds                      ; DS = OUR segment: an ISR that lives in a
    cld                         ; driver has to point DS at itself, and every
                                ; byte it touches below is ours (SPEC.md 51.2)

    cmp byte [sbl_expect], 0 ; the spurious guard (SPEC.md 34.5, binding)
    je .spur
    mov dx, [sbl_base]
    add dx, 0x0E
    in al, dx                   ; the mandatory 8-bit ACK - and ONLY that:
                                ; bit 7 is "response byte waiting", which a
                                ; block IRQ never sets on real DSPs/86Box
                                ; (SPEC.md 34.5; gating on it wedged every
                                ; stream outside QEMU)
    mov ax, [sbl_wd_rst]        ; the block IRQ arrived: rewind the watchdog.
    mov [sbl_wd], ax            ; ONCE, for all three paths below - each of
                                ; them did it, and nothing between here and
                                ; there reads it (IF=0 throughout)
    cmp byte [sbl_str_dir], 0   ; input streams take the mirror half-swap
    jne .input
    cmp byte [sbl_direct], 0    ; the card reads the package's own ring
    jne .direct                 ; (SPEC.md 34.5.2): no halves to swap

    mov ax, [sbl_half]          ; the mode's block size (SPEC.md 34.5 -
    cmp byte [sbl_ring_on], 0   ; wide streams play 4KB halves)
    je .lincons                 ; ring: counters are free-running and a
    add [sbl_consumed], ax      ; half only plays after being filled WHOLE
    jmp .consd                  ; (consumed+half <= fed <= total by
                                ; construction - SPEC.md 34.5), so no cap
.lincons:
    add ax, [sbl_consumed]      ; one block played (cap at the valid length:
                                ; the tail of the last half is silence pad)
    cmp ax, [sbl_total]
    jbe .cons
    mov ax, [sbl_total]
.cons:
    mov [sbl_consumed], ax
.consd:

    mov bl, [sbl_play]          ; the half that just finished...
    xor bh, bh
    mov byte [sbl_valid+bx], 0  ; ...needs a refill
    xor bl, 1
    mov [sbl_play], bl          ; the DSP is on the other half now

    cmp byte [sbl_valid+bx], 0  ; was the next half ever refilled?
    je .under
    cmp byte [sbl_auto], 0      ; yes: auto-init needs nothing; single-cycle
    jne .eoi                    ; re-arms for the half now sounding
    mov al, bl
    call sbl_arm_half
    jmp .eoi

.direct:                        ; DIRECT (SPEC.md 34.5.2): one block of the
    mov ax, [sbl_half]          ; ring played, and the DMA has already gone on
    add [sbl_consumed], ax      ; into the next - so the one question is
    cmp byte [sbl_ext], 0       ; EXTERNAL (SPEC.md 34.5.3): the package's
    je .dfed                    ; total is ITS word and the consumed count is
    les bx, [sbl_extctl]        ; published back, so a producer that may call
    mov ax, [es:bx+SND_EXT_TOTAL]   ; nothing feeds by writing memory
    mov [sbl_total], ax
    mov ax, [sbl_consumed]
    mov [es:bx+SND_EXT_CONS], ax
.dfed:                          ; whether that block was all fed. Same answer
                                ; as the double buffer's valid flag, asked of
    mov ax, [sbl_total]         ; the counters: total - consumed >= half. No
    sub ax, [sbl_consumed]      ; is the underrun contract below, word for
    cmp ax, [sbl_half]          ; word, and verb 1 is what resumes it - there
    jae .eoi                    ; is no refill task to do it
.under:                         ; the underrun contract (SPEC.md 34.5)
    cmp byte [sbl_auto], 0
    je .nohalt                  ; single-cycle: nothing in flight to halt
    call sbl_halt               ; D0h, bounded write-poll
.nohalt:
    mov al, 1                   ; (AL: three register stores beat three
    mov [sbl_paused], al        ; immediate ones in bytes AND cycles)
    mov [sbl_str_state], al     ; SBL_ST_UNDER
    dec ax
    mov [sbl_expect], al
    inc word [sbl_under]
    jmp .eoi

.input:                         ; the mirror half-swap (SPEC.md 34.6): the
    mov bl, [sbl_play]          ; half that just finished capturing...
    xor bh, bh
    mov byte [sbl_valid+bx], 1  ; ...holds data awaiting the drain task
    xor bl, 1
    mov [sbl_play], bl          ; the DSP captures into the other half now
    cmp byte [sbl_valid+bx], 0  ; is the new capture half drained yet?
    jne .under                  ; no - the drain starved: pause input rather
                                ; than overwrite un-drained data (bounded loss
                                ; + a visible status, the underrun contract's
                                ; input mirror - the SAME halt and the same
                                ; four stores as playback's, so the same code)
    cmp byte [sbl_auto], 0      ; auto-init needs nothing; single-cycle
    jne .eoi                    ; re-arms the half now capturing
    mov al, bl
    call sbl_arm_rec

.eoi:                           ; own EOI - nothing chains behind us. ONE
    mov al, 0x20                ; copy, the stray's below included: every
    out 0x20, al                ; path that EOIs falls or jumps here
.done:
    pop es
    pop ds
    pop dx
    pop bx
    pop ax
    iret
.spur:
    cmp byte [sbl_irq], 7       ; line 7: real or spurious? only the 8259
    jne .eoi                    ; knows - a REAL IRQ 7 (the SB's factory
    mov al, 0x0B                ; default line) sets ISR bit 7. OCW3: the
    out 0x20, al                ; next read of 0x20 returns the ISR
    in al, 0x20
    test al, 0x80               ; bit 7 clear = truly spurious: no EOI
    jz .done                    ; (SPEC.md 34.5, binding)
    jmp short .eoi              ; a real stray, any line: EOI or it wedges

; =============================================================================
; The IRQ-discovery candidate stubs (.text - interrupt handlers, so SPEC.md
; 33 bars them from far even though discovery itself is cold). Each records
; its candidate index in sbl_dsc_fired ONLY after the 2xEh bit-7 read
; confirms the DSP asserted the line. A confirm-failed interrupt is still
; a real one when the 8259 holds its in-service bit - the candidates are
; force-unmasked for the window, so an unrelated device on 2/3/5 can fire
; - and gets an EOI; only line 7 can be truly spurious, decided by the
; OCW3 0Bh in-service read exactly as in sbl_isr (SPEC.md 34.5).
; =============================================================================
sbl_stub7:
    push ax
    mov al, 0
    jmp short sbl_stub_c
sbl_stub5:
    push ax
    mov al, 1
    jmp short sbl_stub_c
sbl_stub3:
    push ax
    mov al, 2
    jmp short sbl_stub_c
sbl_stub2:
    push ax
    mov al, 3
sbl_stub_c:
    push dx
    push ds
    mov ah, al                  ; AH = candidate index (AL becomes scratch)
    push cs
    pop ds                      ; DS = ours, like sbl_isr's
    or ah, ah                   ; candidate 0 (IRQ 7) first proves it is not
    jnz .accept                 ; the 8259's spurious-IRQ-7 artifact:
    mov al, 0x0B                ; OCW3 - the next 0x20 read returns the
    out 0x20, al                ; in-service register
    in al, 0x20
    test al, 0x80               ; ISR bit 7 clear = spurious: no ack, no
    jz .out                     ; EOI, no record (sbl_isr's split, 34.5)
.accept:
    mov dx, [sbl_base]          ; accept-on-fire: 2xEh is read as the SB-
    add dx, 0x0E                ; side ACK and nothing else - its bit 7
    in al, dx                   ; means "response byte waiting" and F2h
                                ; queues none on real DSPs or 86Box
                                ; (SPEC.md 34.5 - the QEMU-only 0xAA is
                                ; why a bit-7 "confirm" once shipped)
    mov [sbl_dsc_fired], ah     ; the discovery loop reads this
    mov al, 0x20                ; a stray from an unrelated device on
    out 0x20, al                ; 2/3/5 still needs its EOI either way,
                                ; or its whole priority level wedges
.out:
    pop ds
    pop dx
    pop ax
    iret

; =============================================================================
; The staging-grant allocator (SPEC.md 34.6): first-fit over the SND_SEG
; pool, occupancy derived from the records (the package-pool idiom), every
; grant stamped with the owning instance and force-freed at teardown.
; =============================================================================

; -----------------------------------------------------------------------------
; sbl_grant_alloc - claim CX bytes of the staging pool for an instance
;
; in:       CX = bytes (1..SBL_POOLMAX), DH = owning instance (0xFF kernel)
; out:      CF = 0, SI = grant offset in SND_SEG; CF = 1, AX = 7 bad size /
;           8 no space
; clobbers: nothing else (flags)
;
; The scan and the claim are one pushf/cli..popf unit so two tasks cannot
; both win the same bytes (the SPEC.md 34.3 single-window rule).
; -----------------------------------------------------------------------------
sbl_grant_alloc:
    push bx
    push di
    push bp                     ; BP = grown once already (a register: this
                                ; verb is ANY-TASK, so no shared byte)
    push si                     ; SI is only an output on success: the
                                ; refusals restore the caller's
    mov ax, cx                  ; 1..ceiling, one unsigned test: CX = 0
    dec ax                      ; wraps to 0FFFFh. Past the pool's ceiling
    cmp ax, SBL_POOLKB * 1024 - 1   ; no pool this driver will hold can take
    ja .e7                      ; it, so it is a bad size, not "no space"
    call sbl_pool_get           ; sized to THIS grant if it is the first
    jc .e8n                     ; (SPEC.md 34.6.3)
    xor bp, bp
.scan:
    pushf
    cli
    xor si, si                  ; SI = candidate base: offsets are relative
                                ; to the POOL's own claim now, not to the
                                ; DMA buffer it used to sit above
.rescan:
    mov ax, cx
    dec ax
    add ax, si                  ; AX = candidate's last byte
    jc .e8f                     ; ran off the top of the segment...
    cmp ax, [sbl_poolsz]        ; ...or off the top of the POOL we claimed
    jae .e8f
    xor bx, bx
.g:
    cmp word [sbl_gr_act+bx], 0
    je .next
    mov di, [sbl_gr_off+bx]
    add di, [sbl_gr_size+bx]
    dec di                      ; DI = this grant's last byte
    cmp di, si
    jb .next                    ; grant ends before the candidate: no clash
    cmp ax, [sbl_gr_off+bx]
    jb .next                    ; candidate ends before the grant: no clash
    mov si, di                  ; overlap: skip past this grant, rescan all
    inc si
    jz .e8f                     ; wrapped off 0xFFFF: pool exhausted
    jmp .rescan
.next:
    add bx, 2
    cmp bx, SBL_GMAX*2
    jb .g
    xor bx, bx                  ; fits at SI: find a free record
.f:
    cmp word [sbl_gr_act+bx], 0
    je .claim
    add bx, 2
    cmp bx, SBL_GMAX*2
    jb .f
    jmp .e8f                    ; every record in use
.claim:
    mov [sbl_gr_off+bx], si
    mov [sbl_gr_size+bx], cx
    mov al, dh
    mov ah, 0
    mov [sbl_gr_inst+bx], ax    ; word store keeps the record scannable whole
    mov word [sbl_gr_act+bx], 1
    popf
    pop ax                      ; discard the saved SI: the grant offset is
    clc                         ; the output (AX is dead on CF = 0)
    jmp .out
.e8f:
    or bp, bp                   ; no room: GROW the pool, once, and scan
    jnz .e8p                    ; again - never with a stream open, which is
    cmp byte [sbl_str_act], 0   ; reading from it (SPEC.md 34.6.3). The test
    jne .e8p                    ; and the grow stay in the scan's IF=0
    inc bp                      ; window: a direct open between them would
    call sbl_pool_grow          ; pin the pool by MC_RLOC alone, which
    jc .e8p                     ; OSAPI_MEM_REGROW does not consult
    popf
    jmp .scan
.e8p:
    popf
.e8n:                           ; ...and the same refusal from OUTSIDE the
    mov ax, 8                   ; window, where the pool claim happens
    jmp .err
.e7:
    mov ax, 7
.err:
    push ax                     ; a refusal AFTER sbl_pool_get succeeded -
    call sbl_pool_put           ; a bad size, or no room in a pool we just
    pop ax                      ; claimed - must not strand the 20KB. It is
                                ; a no-op when any grant is live, so this is
                                ; safe on every refusal path
    pop si                      ; refused: the caller's SI comes back
    stc
.out:
    pop bp
    pop di
    pop bx
    ret

; -----------------------------------------------------------------------------
; sbl_grant_find - find the caller's grant record starting at an offset
; in:  SI = grant offset, DH = calling instance
; out: CF = 0, BX = record index (word-scaled); CF = 1 no such grant
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
sbl_grant_find:
    xor bx, bx
.g:
    cmp word [sbl_gr_act+bx], 0
    je .next
    cmp [sbl_gr_off+bx], si
    jne .next
    cmp [sbl_gr_inst+bx], dh
    jne .next
    clc
    ret
.next:
    add bx, 2
    cmp bx, SBL_GMAX*2
    jb .g
    stc
    ret

; -----------------------------------------------------------------------------
; sbl_grant_chk - is [SI, SI+CX-1] inside one grant the caller owns?
;
; in:       SI = start offset, CX = length (>= 1), DH = calling instance
; out:      CF = 0, AX = bytes from SI to the grant's end (the feed bound);
;           CF = 1 refused
; clobbers: nothing else (flags)
;
; The bounds math runs in DI, never DX: DH is this loop's own instance
; operand, and an earlier record that passes the instance compare but
; fails the range checks must not poison the compare for the records
; after it (the Phase 5 gate caught exactly that with two live grants).
; -----------------------------------------------------------------------------
sbl_grant_chk:
    push bx
    push di
    mov ax, cx
    dec ax
    add ax, si                  ; AX = the range's last byte
    jc .bad                     ; wraps: never inside a grant
    xor bx, bx
.g:
    cmp word [sbl_gr_act+bx], 0
    je .next
    cmp [sbl_gr_inst+bx], dh
    jne .next
    mov di, [sbl_gr_off+bx]
    cmp si, di
    jb .next                    ; starts before this grant
    add di, [sbl_gr_size+bx]
    dec di                      ; DI = the grant's last byte
    cmp ax, di
    ja .next                    ; ends past it
    mov ax, di                  ; inside: AX = grant end - SI + 1
    sub ax, si
    inc ax
    clc
    pop di
    pop bx
    ret
.next:
    add bx, 2
    cmp bx, SBL_GMAX*2
    jb .g
.bad:
    stc
    pop di
    pop bx
    ret

; =============================================================================
; The stream surface: slot 0x00EA's live body (SPEC.md 20.3/34.5/34.6).
; osapi_snd_stream (snd.inc) tail-jumps here. AL = verb, AH = handle or
; sub-op; every verb preserves all registers except its documented outputs.
; =============================================================================
sbl_stream_op:
    cmp byte [sbl_up], 0        ; no card attached: nothing can stream
    je .e4
    cmp al, SBL_NVERB - 1
    ja .e7                      ; anything higher: bad verb
    mov bp, ax                  ; A TABLE, through BP: the kernel's call into
    and bp, 0x00FF              ; a driver proc takes BP as the proc and both
    shl bp, 1                   ; stream sites bank it, and every verb reads
    jmp [ds:sbl_vtab+bp]        ; AH and BX, so neither is ours to index with.
                                ; Verb 1 (the feed every stream makes) costs
                                ; what the compare chain charged it, ~43
                                ; cycles; every verb past it costs less
.e7:
    mov ax, 7
    stc
    ret
.e4:
    mov ax, 4
    stc
    ret
sbl_vtab:
    dw sbl_v_open, sbl_v_feed, sbl_v_close, sbl_v_status, sbl_v_open_in
    dw sbl_v_read, sbl_v_stage, sbl_v_grant, sbl_v_busy, sbl_v_ppos
    dw sbl_v_pause
SBL_NVERB    equ ($ - sbl_vtab) / 2

; -----------------------------------------------------------------------------
; sbl_v_busy - verb 8: the KERNEL asking whether a stream is open
;
; in:  DL = 1 the kernel is starting a PCM_EXCL clip, 0 it has finished one
; out: AX = 1 a stream is open (and the clip must be refused), else 0. CF = 0.
;
; NOT IN THE PACKAGE SDK - it is the kernel's half of a two-way exclusion it
; is the only side that can see. An exclusive speaker clip raises sch_lock for
; its whole duration, which would freeze this driver's refill worker and force
; its stream through the underrun path; and a stream's block IRQ arriving
; mid-clip is a DMA transfer the clip's busy loop cannot pace around. So the
; two tiers must never overlap - but the STREAM side lives here and the CLIP
; side lives in snd.inc, and neither can read the other's state. This is the
; kernel telling us its half and asking ours in one call.
; -----------------------------------------------------------------------------
sbl_v_busy:
    mov [sbl_clip], dl          ; what the kernel is about to do...
    xor ax, ax
    cmp byte [sbl_str_act], 0   ; ...and what we already are
    je .out
    inc ax
.out:
    clc
    ret

; -----------------------------------------------------------------------------
; sbl_live - is the stream DH serves still the open one? (module-internal)
; in:  DH = a generation (a worker's, or a resume's)
; out: ZF = 1 yes: open, and that generation; ZF = 0 closed or superseded
;
; The task-side checks only. The per-chunk ones inside sbl_fill_guard and
; sbl_drain_half stay inline: they run once per 512 bytes copied, and a call
; there would be a cost on the path that moves every sample.
; -----------------------------------------------------------------------------
sbl_live:
    cmp byte [sbl_str_act], 1
    jne .r
    cmp dh, [sbl_gen]
.r:
    ret

; -----------------------------------------------------------------------------
; sbl_hcheck - validate a stream handle (module-internal)
; in:  AH = handle
; out: CF = 1 stale (no open stream, or the generation moved on)
; clobbers: nothing (flags)
; -----------------------------------------------------------------------------
sbl_hcheck:
    cmp byte [sbl_str_act], 1
    jne .st
    cmp ah, [sbl_gen]
    jne .st
    clc
    ret
.st:
    stc
    ret

; -----------------------------------------------------------------------------
; sbl_v_open - verb 0: open a playback stream (SPEC.md 20.3/34.5)
;
; in:       DX = rate Hz (4000..22222), SI = grant offset, CX = valid bytes
;           staged so far (> 0; CX short of the grant is the caller
;           explicitly accepting progressive-feed risk - the staged rule),
;           AH = open flags (verb 0 carries no handle, so the byte is
;           free): bit 0 = SND_OPENF_RING - ring mode (SPEC.md 34.5): the
;           ring length RL = grant end - SI must be a power of two in
;           4096..32768 and CX must be SBL_HALF..RL, else err 7
; out:      AL = 0 ok with AH = handle, else AX = 1 busy / 2 rate / 4 no
;           sink or no IRQ found / 6 no task slot / 7 bad grant or ring;
;           CF mirrors the error
; clobbers: nothing else (flags)
;
; First use runs IRQ discovery (sbl_irq_disc, far) and hooks sbl_isr onto
; the winner. The record publishes act=1 before the refill task spawns,
; so neither the tick nor the ISR can see a half-built stream (the
; publish-last idiom) - but sbl_expect arms BEFORE sbl_hw_start, the
; same order sbl_refill's resume uses: the first block IRQ can beat the
; opener's next instruction, and an unexpected real IRQ never reads 2xEh,
; leaving the DSP INT latched and the stream permanently silent.
;
; One serialization assumption, pinned (SPEC.md 20.3, per-verb): the OPEN
; verbs run only from window callbacks on the single UI task, so the busy
; check here and the eventual act=1 publish - ~60 IF=1 instructions apart,
; and IRQ discovery can even sleep on ticks in between - never race a
; second open. The any-task verbs (1/3/5/6/7) carry their own pushf/cli
; windows instead (SPEC.md 34.3); a future task-context OPEN would have to
; claim the record under pushf/cli first (the sbl_grant_alloc standard).
; -----------------------------------------------------------------------------
sbl_v_open:
    push bx
    push cx
    push dx
    push si
    mov [sbl_oflags], ah        ; latch the open flags before AH dies - a
                                ; TEMP, not the live record: the busy refusal
                                ; below must not disturb an OPEN stream's
                                ; [sbl_ring_on]. RING, EXT, ADPCM4 and the
                                ; ring size code (SPEC.md 34.5.3)
    cmp byte [sbl_str_act], 0   ; (no [sbl_up] test: sbl_stream_op, the
                                ; only way in, has just made it)
    jne .e1n                     ; one stream per card (SPEC.md 34.3)
    cmp byte [sbl_clip], 0
    jne .e1n                     ; an exclusive clip holds the machine
    cmp bx, 4000                ; TC = 256 - 1e6/rate must fit a byte. THE
    jb .e2n                     ; RATE ARRIVES IN BX, not DX: DH carries the
    mov bp, SBL_HALF            ; requesting instance (SPEC.md 34.3) and
    cmp bx, 22222               ; would otherwise be the rate's top half, so
    jbe .ratok                  ; the kernel banks it for us on the way in
    cmp bx, 44100               ; above 22,222 Hz needs one of two regimes -
    ja .e2n                      ; err 2 above 44,100, never a clamp
    cmp byte [sbl_verhi], 3     ; below DSP 3.00 there is neither wide (41h,
    jb .e2n                      ; SB16) nor high-speed (90h, SB Pro): err 2,
    mov bp, 2*SBL_HALF          ; never a clamp. WHICH of the two is not
                                ; decided here - both use the 4KB half, and
                                ; .irqok reads the version back. BP IS THE
                                ; HALF WIDTH UNTIL .irqok COMMITS IT: a TEMP,
                                ; because the busy refusal above must not
                                ; disturb a live stream's [sbl_half], and a
                                ; register because the kernel banks BP round
                                ; every driver call (it names the proc)
.ratok:
    jcxz .e7x                   ; nothing staged at all
    test byte [sbl_oflags], SND_OPENF_EXT
    jz .pool
    test byte [sbl_oflags], SND_OPENF_RING
    jz .e7x                     ; an EXTERNAL ring (SPEC.md 34.5.3) is a ring:
    push cx                     ; DI:SI, RL = 4096 << the size code, and no
    mov al, [sbl_oflags]        ; grant - the package claimed it itself
    mov cl, SND_OPENF_RLSH
    shr al, cl
    and al, 3
    mov cl, al
    mov ax, 4096
    shl ax, cl
    pop cx
    jmp short .grantok
.e7x:                           ; the refusals' trampolines, within short
    jmp .e7                     ; reach of every test above and of the IRQ
.e1n:                           ; discovery's below: the real exits are at
    jmp .e1                     ; the far end, where sbl_v_open_in shares
.e2n:                           ; them
    jmp .e2
.e4n:
    jmp .e4
.pool:
    call sbl_grant_chk          ; the range must sit in the caller's grant;
    jc .e7x                     ; out AX = bytes to the grant's end (BX is
.grantok:                       ; still the rate, see above)
    mov [sbl_str_max], ax       ; the feed bound (linear; ring uses the mask)
    mov [sbl_str_inst], dh
    test byte [sbl_oflags], SND_OPENF_RING  ; ring validation (SPEC.md 20.3
    jz .irqchk                  ; verb 0 / 34.5): RL = AX = bytes SI..grant end
    cmp ax, 4096                ; power of two, 4096..32768...
    jb .e7x
    cmp ax, 32768
    ja .e7x
    mov dx, ax
    dec dx                      ; DX = candidate mask
    test ax, dx
    jnz .e7x                    ; not a power of two
    cmp cx, ax                  ; ...and one half <= CX <= RL: a ring never
    ja .e7x                     ; pads, so the DSP may not start on a half
    cmp cx, bp                  ; no fill can complete (SPEC.md 34.5 rule 3;
    jb .e7x                     ; the WIDE half when the rate widened it)
    mov [sbl_rmask], dx         ; RL-1: the ring mask
.irqchk:
    cmp byte [sbl_irq], 0xFF    ; IRQ discovery, deferred to first use
    jne .irqok                  ; (SPEC.md 34.5)
    call sbl_f_irqdisc           ; far orchestration; hooks the winner
    or al, al
    jz .e4n                      ; no candidate confirmed: no usable card
.irqok:
    mov ax, bp                  ; commit the half width, then DERIVE the
    mov [sbl_half], ax          ; regime from it and [sbl_verhi] - the two
    mov word [sbl_wide], 0      ; facts the rate gate already checked. Past
                                ; every refusal, so no TEMP is owed (safe
    cmp ax, SBL_HALF            ; pre-publish: sbl_str_act == 0 held at .e1,
    je .tcrate                  ; so nothing live reads these). The word is
                                ; sbl_wide and sbl_hisp
    cmp byte [sbl_verhi], 4     ; the half widened, so it is one of the two:
    jb .hsreg                   ; DSP >= 4.00 wide, 3.xx SB Pro high-speed
    mov byte [sbl_wide], 1      ; wide (SPEC.md 34.5): 41h wants the rate
    mov [sbl_rate], bx          ; in Hz; the watchdog is the shared .wdcalc
    jmp .wdcalc                 ; below - it spans the wide blocks either way
.e2x:                           ; (in short reach of the ADPCM4 tests below)
    jmp .e2
.hsreg:
    mov byte [sbl_hisp], 1      ; high-speed: 4KB half but a TC byte, not 41h
    mov ax, bx                  ; ...and ROUNDED: rate/2 added to the dividend.
    shr ax, 1                   ; Truncation puts 44,100 on TC 234 = 45,454
    jmp short .tcadd            ; Hz, half a semitone sharp and 3% short; 233
.tcrate:                        ; = 43,478 Hz is the nearest the byte can say
    xor ax, ax                  ; legacy: truncate, as it always has
.tcadd:
    xor dx, dx                  ; TC = 256 - 1,000,000/rate (SPEC.md 34.5)
    add ax, 0x4240
    adc dx, 0x000F
.tcdiv:
    div bx                      ; AX = 1e6/rate (45..250 in the TC range,
    neg al                      ; ~22 at 44.1kHz for the high-speed regime)
    mov [sbl_tc], al
    ; watchdog dividend: the blocks the half is actually made of. SBL_WD_WIDE
    ; is exactly 2*SBL_WD_OUT (%if-guarded above), so the wide/high-speed case
    ; is a doubling and not a second constant - and the half itself says which
    ; it is, so this arm serves all three regimes (BX is still the rate).
.wdcalc:
    mov ax, SBL_WD_OUT & 0xFFFF
    mov dx, SBL_WD_OUT >> 16
    cmp byte [sbl_half+1], SBL_HALF >> 8
    je .wddiv
    shl ax, 1
    rcl dx, 1
.wddiv:
    div bx
    add ax, 3
    mov [sbl_wd_rst], ax
    mov [sbl_wd], ax
    xor ax, ax
    mov [sbl_direct], ax        ; ...and sbl_ext, the byte after it
    mov [sbl_adpcm], al
    ; DIRECT OR DOUBLE-BUFFERED (SPEC.md 34.5.2). A ring the 8237 can reach
    ; is played where it lies: the ring IS a double buffer already, halves
    ; and all, so copying it into another one bought nothing but the copy and
    ; the 8KB it lands in. Three facts decide it and all three are known now:
    ; auto-init (a single-cycle DSP re-arms per half from the ISR, which this
    ; path does not carry), a ring of at least two halves (one is a block the
    ; producer could never write ahead of), and a ring whose physical span
    ; does not cross a 64KB page - which sbl_pool_get asks the allocator for,
    ; so it is the case and not the hope, and the fallback is the copy path
    test byte [sbl_oflags], SND_OPENF_ADPCM4
    jz .noad                    ; ADPCM4 (SPEC.md 34.5.3): the 7Dh start is
    cmp byte [sbl_auto], 0      ; auto-init only, and the TC regime only -
    je .e2x                     ; the card decodes it at the SAMPLE rate
    cmp word [sbl_half], SBL_HALF
    jne .e2x
    cmp byte [sbl_verhi], 4     ; ...and DSP 2.00-3.xx only: an SB16's DSP
    jb .adok                    ; 4.xx dropped Creative's ADPCM commands, so
                                ; 7Dh there is no sound rather than a refusal
    test byte [sbl_oflags], SND_OPENF_FORCE
    jz .e2x                     ; ...UNLESS THE CALLER ASKS: compatible cards
.adok:                          ; that answer 4.xx and decode it do exist, and
    mov byte [sbl_adpcm], 1     ; the user is the one who can hear which
                                ; (SPEC.md 34.5.3.1)
.noad:
    mov al, [sbl_oflags]        ; AN EXTERNAL RING'S BLOCK (SPEC.md 34.5.3):
    test al, SND_OPENF_EXT      ; the half shifted right by the code in AH
    jz .blk                     ; bits 6-7, so a slow stream's block is not
    mov cl, SND_OPENF_BLKSH     ; seconds long. After the watchdog and the
    shr al, cl                  ; ADPCM4 test, which read the full half
    mov cl, al
    shr word [sbl_half], cl
.blk:
    mov ax, [sbl_poolseg]       ; where the ring is: the pool...
    test byte [sbl_oflags], SND_OPENF_EXT
    jz .rseg
    mov ax, di                  ; ...or the package's own claim
.rseg:
    mov [sbl_rseg], ax
    test byte [sbl_oflags], SND_OPENF_RING
    jz .dbuf
    cmp byte [sbl_auto], 0
    je .dbuf
    mov ax, [sbl_half]
    shl ax, 1
    dec ax                      ; AX = 2 halves - 1, against RL - 1
    cmp ax, [sbl_rmask]
    ja .dbuf
    call sbl_ring_phys          ; SI = the ring: AX/DL = where the chip sees it
    jc .dbuf                    ; straddles a page: the copy path
    mov [sbl_xoff], ax
    mov [sbl_xpage], dl
    mov ax, [sbl_rmask]         ; the DMA cycle IS the ring
    mov [sbl_xcnt], ax
    mov byte [sbl_direct], 1
    test byte [sbl_oflags], SND_OPENF_EXT
    jz .pin
    mov byte [sbl_ext], 1       ; EXTERNAL (SPEC.md 34.5.3): the control words
    mov ax, si                  ; sit after the ring, in the package's claim,
    add ax, [sbl_rmask]         ; which an MC_DMA claim keeps still with no
    inc ax                      ; pin of ours - and there is no pool to pin
    mov [sbl_extctl], ax
    mov [sbl_extctl+2], di
    jmp short .dset
.e7x2:
    jmp .e7
.pin:                           ; ...and the pool may not move under the chip
    mov dx, [sbl_poolseg]       ; now: PIN it (AX = 0) until sbl_stop_stream
    xor ax, ax                  ; declares it again. [drv_wcnt] used to be
    call OSAPI_MEM_MOVABLE      ; what kept the chip's memory still, and a
    jmp short .dset             ; direct stream has no task to count (DX is
                                ; dead here: the exit restores it)
.dbuf:
    test byte [sbl_oflags], SND_OPENF_EXT
    jnz .e7x2                   ; an external ring is played in place or not
                                ; at all: there is no grant to copy it from
    call sbl_dma_get            ; the double buffer, claimed for this stream
    jnc .dgot                   ; and freed with it
    jmp .e8
.dgot:
    mov ax, [sbl_dmaoff]
    mov [sbl_xoff], ax
    mov al, [sbl_page]
    mov [sbl_xpage], al
    mov ax, [sbl_half]
    shl ax, 1
    dec ax
    mov [sbl_xcnt], ax          ; 2 halves - 1
.dset:
    mov [sbl_str_off], si       ; the stream record (not yet published)
    mov [sbl_total], cx
    xor ax, ax
    mov [sbl_fed], ax
    mov [sbl_consumed], ax
    mov [sbl_play], al
    mov [sbl_valid], ax         ; both halves
    mov [sbl_paused], al
    mov [sbl_str_dir], al       ; an OUTPUT stream (SPEC.md 34.6)
    mov [sbl_str_state], al     ; SBL_ST_PLAY
    mov al, [sbl_oflags]        ; commit the ring flag into the record
    and al, SND_OPENF_RING      ; (SPEC.md 34.5; sbl_rmask was set above)
    mov [sbl_ring_on], al
    inc byte [sbl_gen]
    mov byte [sbl_str_act], 1   ; PUBLISH, on both paths, before anything
                                ; acts: nothing can act on a published record
                                ; yet - the ISR and the tick both wait on
                                ; sbl_expect, armed only at .start, and no
                                ; caller holds the new handle
    cmp byte [sbl_direct], 0    ; DIRECT: nothing to prime and nobody to
    jne .start                  ; pace - verb 0's CX >= one half is already
                                ; the first block, and verb 1 is the rest
    mov dh, [sbl_gen]           ; double-buffered: the priming fills are the
    xor bx, bx                  ; refill task's own sbl_fill_guard - act +
    call sbl_fill_guard         ; this generation are what it verifies. Half
    inc bx                      ; 0 always has data (ring: CX >= one half,
    call sbl_fill_guard         ; validated; linear: CX > 0, a short half
                                ; pads); half 1 takes what remains by the
                                ; guard's own rule - ring fills whole halves
                                ; only, never padded (SPEC.md 34.5 rule 1),
                                ; linear any remainder (+ pad)
    mov ax, sbl_refill_task     ; ...then pace: the DRIVER owns pacing (SPEC.md
    call OSAPI_DRV_TASK         ; 51.7). DH reaches the worker's own DH -
                                ; per-task, so a close-and-reopen between here
                                ; and its first instruction cannot leave two
                                ; workers alive. It runs in this segment with
                                ; T_INST = 0xFF, so it bills to System: a
                                ; driver has no instance
    jc .nospawn
.start:
    mov byte [sbl_expect], 1    ; arm BEFORE the hardware starts (the
    call sbl_hw_start           ; sbl_refill resume order): if the opener
                                ; is preempted right after 1Ch and stays
                                ; off-CPU a whole block period, the first
                                ; block IRQ must not ride the spurious
                                ; path - unconfirmed via 2xEh, the DSP
                                ; would keep its INT latched and the edge-
                                ; triggered line could never re-fire.
                                ; Arming early is safe: sbl_wd is already
                                ; loaded, so sbl_tick cannot underflow it
.opened:
    mov ah, [sbl_gen]           ; out: AH = handle, AL = 0
    mov al, 0
    clc
    jmp short .out
.nospawn:
    mov byte [sbl_str_act], 0   ; unwind: nothing started
    call sbl_dma_put            ; ...and the buffer it would have played
    mov al, 6                   ; the documented clean pool-full error
    jmp short .err
.e1:
    mov al, 1
    jmp short .err
.e2:
    mov al, 2
    jmp short .err
.e4:
    mov al, 4
    jmp short .err
.e8:
    mov al, 8                   ; no page-safe 8KB for the double buffer
    jmp short .err              ; (SPEC.md 34.5.2) - verb 7's "no space"
.e7:
    mov al, 7
.err:
    mov ah, 0                   ; AX = the code (verb 4's exits too)
    stc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sbl_v_open_in - verb 4: open a record stream (SPEC.md 20.3/34.6, Phase 5)
;
; in:       DX = rate Hz (4000 up to the input ceiling: 13,000 on a
;           single-cycle DSP, 15,000 on auto-init - SPEC.md 34.5),
;           SI = grant offset, CX = capture capacity in bytes (> 0, inside
;           the caller's grant)
; out:      AL = 0 ok with AH = handle, else AX = 1 busy (half-duplex: any
;           open stream, or an exclusive clip) / 2 rate / 4 no sink or no
;           IRQ found / 6 no task slot / 7 bad grant; CF mirrors the error
; clobbers: nothing else (flags)
;
; The mirror of sbl_v_open: same record, direction = in, the same deferred
; IRQ discovery, the same publish-then-pace order (act = 1 before the
; drain task spawns, sbl_expect armed BEFORE sbl_hw_start_in for the
; same first-IRQ race). sbl_total holds the capture CAPACITY; the drain
; task advances sbl_consumed as ring halves land in the grant, so verb 3
; reads {state, bytes captured} with no direction branch. Half-duplex is
; enforced HERE, by the router's one owner record - the driver never sees
; an in/out conflict (SPEC.md 34.3). The busy check -> act=1 publish gap
; rests on sbl_v_open's pinned serialization assumption (SPEC.md 20.3:
; stream verbs are UI-task/window-callback context only).
; -----------------------------------------------------------------------------
sbl_v_open_in:
    push bx
    push cx
    push dx
    push si
    cmp byte [sbl_str_act], 0   ; ([sbl_up]: sbl_stream_op tested it)
    jne sbl_v_open.e1           ; half-duplex: one stream record, either
                                ; direction (SPEC.md 34.3/34.6)
    cmp byte [sbl_clip], 0
    jne sbl_v_open.e1           ; an exclusive clip holds the machine
    cmp bx, 4000                ; the rate is in BX here too (see sbl_v_open)
    jb sbl_v_open.e2
    mov ax, 13000               ; ...and the INPUT ceiling is honoured:
    cmp byte [sbl_auto], 0      ; 13k on 1.x, 15k on >= 2.00 normal mode
    je .ceil                    ; (SPEC.md 34.5) - err 2, never a clamp
    mov ax, 15000
.ceil:
    cmp bx, ax
    ja sbl_v_open.e2
    jcxz sbl_v_open.e7          ; zero capacity captures nothing
    call sbl_grant_chk          ; the capture range must sit in the caller's
    jc sbl_v_open.e7            ; grant; out AX = bytes to the grant's end.
                                ; BX is still the rate (see above)
    mov [sbl_str_max], ax
    mov [sbl_str_inst], dh
    cmp byte [sbl_irq], 0xFF    ; IRQ discovery, deferred to first use
    jne .irqok                  ; (SPEC.md 34.5)
    call sbl_f_irqdisc           ; far orchestration; hooks the winner
    or al, al
    jz sbl_v_open.e4            ; no candidate confirmed: no usable card
.irqok:
    mov ax, 0x4240              ; TC = 256 - 1,000,000/rate (SPEC.md 34.5)
    mov dx, 0x000F
    div bx                      ; AX = 1e6/rate (66..250 by the range check)
    neg al                      ; AL = 256 - AX (mod 256)
    mov [sbl_tc], al
    mov ax, SBL_WD_IN & 0xFFFF  ; watchdog: 2 ring-half periods in ticks + 3
    mov dx, SBL_WD_IN >> 16     ; = (2*SBL_RHALF*18.2)/rate + 3, derived
    div bx
    add ax, 3
    mov [sbl_wd_rst], ax
    mov [sbl_wd], ax
    call sbl_dma_get            ; the record ring lives in the DMA buffer,
    jc sbl_v_open.e8            ; claimed for this stream (SPEC.md 34.5.2)
    mov [sbl_str_off], si       ; the stream record (not yet published)
    mov [sbl_total], cx         ; = the capture capacity (SPEC.md 34.6)
    xor ax, ax
    mov [sbl_direct], al
    mov [sbl_fed], ax
    mov [sbl_consumed], ax      ; = bytes drained into the grant
    mov [sbl_play], al          ; the half the DSP captures into
    mov [sbl_valid], ax         ; input sense: 1 = captured, awaiting drain
    mov [sbl_drain_h], al       ; the next half the drain task empties
    mov [sbl_paused], al
    mov [sbl_ring_on], al       ; input has no ring mode (SPEC.md 34.5)
    mov [sbl_str_state], al     ; SBL_ST_PLAY
    inc ax
    mov [sbl_str_dir], al       ; an INPUT stream (SPEC.md 34.6)
    inc byte [sbl_gen]
    mov [sbl_str_act], al       ; publish, then pace: the DRIVER owns pacing
    mov dh, [sbl_gen]           ; (SPEC.md 51.7), as for playback above
    mov ax, sbl_drain_task
    call OSAPI_DRV_TASK
    jc sbl_v_open.nospawn
    mov byte [sbl_expect], 1    ; arm BEFORE the hardware starts - the
    call sbl_hw_start_in        ; sbl_v_open first-IRQ order, same race
    jmp sbl_v_open.opened       ; out: AH = handle, AL = 0

; -----------------------------------------------------------------------------
; sbl_v_feed - verb 1: extend a progressively staged stream (SPEC.md 20.3)
;
; in:       AH = handle, CX = new total valid length. Linear: >= the old,
;           and within the grant. Ring (SPEC.md 34.5): free-running mod
;           65536 - new - old < 0x8000 (monotonic forward) and new - fed
;           <= RL (never overwrite bytes the refill has not copied out)
; out:      AX = 0 ok / 7 bad length or an input stream (SPEC.md 34.6) /
;           0FFFFh stale; CF set on both errors
; clobbers: nothing else (flags)
;
; ANY-TASK (SPEC.md 20.3's per-verb context rule), so the hcheck, the
; direction check, both bound checks and the [sbl_total] store are ONE
; pushf/cli..popf window (SPEC.md 34.3): a caller preempted between a
; stale-handle check and the store, across a close+reopen on another
; task, must never validate against one stream and store into its
; successor. Bounded: a dozen compares and one word store - ISR-legal.
; An underrun-paused stream resumes on the refill task's next wake; a
; watchdog-stopped stream does not (its task is gone - the owner closes).
; -----------------------------------------------------------------------------
sbl_v_feed:
    pushf
    cli
    call sbl_hcheck
    jc .stale
    cmp byte [sbl_str_dir], 0   ; a record stream has no valid length to
    jne .e7                     ; extend (SPEC.md 34.6): err 7
    cmp byte [sbl_ring_on], 0
    jne .ring
    cmp cx, [sbl_total]
    jb .e7                      ; the valid length never shrinks
    cmp cx, [sbl_str_max]
    ja .e7                      ; past the grant's end
    jmp .store
.ring:
    mov ax, cx                  ; free-running bounds, both subtractions
    sub ax, [sbl_total]         ; (SPEC.md 34.5 rule 2)
    test ax, ax
    js .e7                      ; delta >= 0x8000: not monotonic forward
    mov ax, cx
    cmp byte [sbl_direct], 0    ; DIRECT (SPEC.md 34.5.2): nothing is copied
    je .fedb                    ; out, so the bytes not yet PLAYED are what a
    sub ax, [sbl_consumed]      ; feed must not lap - consumed stands where
    jmp short .bound            ; fed stood
.fedb:
    sub ax, [sbl_fed]           ; outstanding bytes after this feed
.bound:
    jz .store                   ; nothing outstanding is trivially fine
    dec ax
    cmp ax, [sbl_rmask]         ; <= RL  <=>  (outstanding - 1) <= RL-1
    ja .e7
.store:
    mov [sbl_total], cx
    cmp byte [sbl_paused], 0    ; ...and a DIRECT stream the ISR paused
    je .ok                      ; resumes HERE, the moment a whole block is
    cmp byte [sbl_direct], 0    ; fed again: this was the refill task's job,
    je .ok                      ; and a direct stream has none. The same
    mov ax, cx                  ; flags, watchdog and ports as sbl_refill's
    sub ax, [sbl_consumed]      ; resume, in this one window (SPEC.md 34.3)
    cmp ax, [sbl_half]
    jb .ok
    call sbl_resume             ; D4h (SPEC.md 34.5)
.ok:
    popf
    xor ax, ax
    clc
    ret
.e7:
    popf
    mov ax, 7
    stc
    ret
.stale:
    popf
    mov ax, 0xFFFF
    stc
    ret

; -----------------------------------------------------------------------------
; sbl_v_pause - verb 10: PAUSE an external ring's stream now (SPEC.md 34.5.4)
; in:       AH = handle
; out:      AX = 0 ok / 7 not an external output ring / 0FFFFh stale; CF
;           set on both errors
; clobbers: nothing else (flags)
;
; The underrun contract's pause, taken on request instead of for want of
; data: the DSP halted where it is (D0h, or the 8237 masked in high speed),
; state underrun-paused, and verb 1 resumes it exactly as it resumes an
; underrun. An external ring's producer (98.3.1) keeps the ring full, so
; waiting for it to run dry would be up to a ring's worth of sound late.
; sbl_expect stays SET: a block that ended just before the D0h still has
; its IRQ coming, and the ISR must take it as ours - ack, count - rather
; than as a stray. The watchdog skips any state but PLAY, so a pause may
; be as long as the user likes.
; -----------------------------------------------------------------------------
sbl_v_pause:
    pushf
    cli
    call sbl_hcheck
    jc sbl_v_feed.stale         ; (verb 1's refusals: the same three words)
    cmp byte [sbl_ext], 0
    je sbl_v_feed.e7
    cmp byte [sbl_str_dir], 0
    jne sbl_v_feed.e7
    cmp byte [sbl_str_state], SBL_ST_PLAY
    jne sbl_v_feed.ok           ; already paused, or ended: nothing to stop
    call sbl_halt
    mov al, 1
    mov [sbl_paused], al
    mov [sbl_str_state], al     ; SBL_ST_UNDER
    jmp short sbl_v_feed.ok

; -----------------------------------------------------------------------------
; sbl_v_close - verb 2: close the stream (SPEC.md 20.3)
;
; in:       AH = handle
; out:      AX = 0 ok / 0FFFFh stale (CF mirrors)
; clobbers: nothing else (flags)
;
; The refill task notices act = 0 (or the next open's new generation) at
; its next wake and exits - ending the task is the record teardown's job,
; never the ISR's.
; -----------------------------------------------------------------------------
sbl_v_close:
    call sbl_hcheck
    jc .stale
    call sbl_stop_stream
    xor ax, ax
    clc
    ret
.stale:
    mov ax, 0xFFFF
    stc
    ret

; -----------------------------------------------------------------------------
; sbl_stop_stream - halt playback and free the stream record (internal)
;
; in:       an open stream ([sbl_str_act] = 1)
; out:      nothing; the DSP is silent, sbl_expect clear, act = 0
; clobbers: nothing (flags)
;
; One pushf/cli window covers the ports AND the ownership bytes (the
; SPEC.md 34.3 release rule): halt DMA requests (D0h), leave auto-init mode
; (DAh) so a dangling ring cannot restart, speaker off (D3h), and a 2xEh
; read to retire any block IRQ the card had already latched - a stray
; delivered after popf lands on the ISR's spurious path, which EOIs it.
;
; High-speed (SB Pro): the DSP takes no command until a reset, so the cli
; window only masks DMA channel 1 (sbl_halt) and retires the IRQ; the reset
; that actually leaves high-speed mode - a few ms of ISA polling - runs
; AFTER popf, on the task, with the stream already dead (act/expect clear).
; -----------------------------------------------------------------------------
sbl_stop_stream:
    push ax
    push cx                     ; CX: sbl_f_reset's poll budget
    push dx
    pushf
    cli
    call sbl_halt               ; D0h (or, high-speed, mask DMA ch 1)
    cmp byte [sbl_hisp], 0
    jne .hs_ports
    cmp byte [sbl_auto], 0
    je .noda
    mov al, 0xDA                ; exit auto-init at the block edge
    call sbl_dsp_wr
.noda:
    mov al, 0xD3                ; speaker (DAC) off
    call sbl_dsp_wr
.hs_ports:
    mov al, 0x05                ; and MASK the channel: the memory it was
    out 0x0A, al                ; pointed at is about to be freed or let move
    mov dx, [sbl_base]          ; (below), and every start unmasks it again
    add dx, 0x0E
    in al, dx                   ; retire a latched block IRQ SB-side
    xor al, al
    mov [sbl_expect], al
    mov [sbl_str_act], al       ; handles are stale from here
    popf
    call sbl_unpin              ; the pool movable again, or the buffer freed
    cmp byte [sbl_hisp], 0
    je .out
    mov cx, SBL_POLLS           ; the only way out of high-speed mode is the
    call sbl_f_reset            ; probe's own reset (SPEC.md 34.5) - same
    jnc .reset_ok               ; ports, same 0AAh handshake, bounded
    mov cx, SBL_POLLS
    call sbl_f_reset            ; once more; a DSP still in 90h swallows
.reset_ok:                      ; every later open's commands (silence)
    mov al, 0xD3               ; speaker off now that the DSP takes commands
    call sbl_dsp_wr
    mov byte [sbl_hisp], 0
.out:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sbl_unpin - give back what the stream held of the heap (SPEC.md 34.5.2)
; in:  a stream just stopped ([sbl_str_act] = 0, the channel masked)
; out: nothing; clobbers nothing (flags)
;
; A DIRECT stream pinned the pool (verb 0) and gets it declared movable again:
; the chip is masked, so the compactor may have it. A double-buffered one
; claimed the 8KB buffer and frees it here - freeing is safe with the worker
; still alive, because its every write is an act+gen-verified IF=0 chunk and
; act is already 0. Outside every cli window: both are kernel calls.
; -----------------------------------------------------------------------------
sbl_unpin:
    push ax
    push dx
    mov ax, [sbl_direct]        ; AL = direct, AH = ext (the next byte)
    or al, al
    jz .buf
    mov word [sbl_direct], 0
    or ah, ah                   ; an external ring pinned nothing of ours
    jnz .out                    ; (SPEC.md 34.5.3)
    mov dx, [sbl_poolseg]
    mov ax, sbl_reloc
    call OSAPI_MEM_MOVABLE
    jmp short .out
.buf:
    call sbl_dma_put
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sbl_v_status - verb 3: poll a stream (SPEC.md 20.3 - THE notification
;                mechanism; there are no sound events)
;
; in:       AH = handle
; out:      AX = 0 playing / 1 underrun-paused / 2 ended (watchdog-stopped)
;           with DX = bytes consumed (ring mode: free-running mod 65536 -
;           the owner works in deltas, SPEC.md 34.5); or AX = 0FFFFh stale
;           (CF = 1, DX = 0)
; clobbers: nothing else (flags)
;
; ANY-TASK (SPEC.md 20.3): the hcheck and the two loads are one pushf/cli
; window (SPEC.md 34.3) - a stale handle must read STALE, never the
; successor stream's {state, consumed} through a close+reopen it was
; preempted across.
; -----------------------------------------------------------------------------
sbl_v_status:
    pushf
    cli
    call sbl_hcheck
    jc .stale
    mov al, [sbl_str_state]
    mov ah, 0
    mov dx, [sbl_consumed]
    popf
    clc
    ret
.stale:
    popf
    mov ax, 0xFFFF
    xor dx, dx
    stc
    ret

; -----------------------------------------------------------------------------
; sbl_v_ppos - verb 9: verb 3 with DX = the bytes PLAYED, exactly (SPEC.md
;              34.5.1)
; in/out:   as verb 3
;
; Verb 3's count advances one whole block per block IRQ, so between IRQs it
; says where the card WAS - 93 ms of 22 kHz, 372 ms of XT mode's 5.5 kHz -
; and a display that wants where the card IS had to estimate the rest from
; ticks, guessing when the report was raised. The 8237 knows: channel 1's
; count is how far the DMA has got into the block playing now, so this is
; verb 3's count plus that, under the same cli window, and no estimate.
;
; ONE RACE, and it is the IRQ this cli is holding off: the DMA can already be
; in the NEXT half while the ISR that counts the last one is pending. Then the
; half the count names is not [sbl_play], and the finished block is added
; here as the ISR is about to add it. A record stream, a paused or ended one
; and a stream with no IRQ armed answer verb 3's count unchanged.
; -----------------------------------------------------------------------------
sbl_v_ppos:
    pushf
    cli
    call sbl_hcheck
    jc sbl_v_status.stale
    push bx
    push cx
    mov al, [sbl_str_state]
    mov ah, 0
    mov dx, [sbl_consumed]
    cmp byte [sbl_str_dir], 0
    jne .out                    ; recording: verb 3's count is the answer
    cmp al, SBL_ST_PLAY
    jne .out
    cmp byte [sbl_expect], 0
    je .out
    push ax
    out 0x0C, al                ; flip-flop clear (any value)
    in al, 0x03                 ; channel 1's count: lo...
    mov cl, al
    in al, 0x03                 ; ...hi. It is LEFT - 1, and FFFFh at
    mov ch, al                  ; terminal count
    inc cx                      ; CX = bytes left in this DMA cycle
    cmp byte [sbl_direct], 0
    je .dbuf
    mov bx, [sbl_rmask]         ; DIRECT (SPEC.md 34.5.2): the cycle is the
    lea ax, [bx+1]              ; ring, so AX = RL - left is the ring offset
    sub ax, cx                  ; the chip reads next, and the bytes played
    mov cx, dx                  ; are consumed plus how far that is past
    and cx, bx                  ; consumed's own ring offset - which is right
    sub ax, cx                  ; with the IRQ pending too, where the double
    and ax, bx                  ; buffer below needs a case for it
    jmp short .add
.dbuf:
    mov bx, [sbl_half]
    cmp byte [sbl_auto], 0
    je .single
    mov ax, bx                  ; auto-init: one cycle is BOTH halves
    shl ax, 1
    sub ax, cx                  ; AX = bytes into the double buffer
    cmp ax, bx
    jb .h0
    sub ax, bx                  ; the second half is playing
    cmp byte [sbl_play], 1
    je .add
    jmp short .fin              ; ...and the ISR has not flipped to it yet
.h0:
    cmp byte [sbl_play], 0
    je .add
.fin:
    add dx, bx                  ; the finished block the pending ISR counts
    jmp short .add
.single:
    mov ax, bx                  ; single-cycle: one cycle is one half, and a
    sub ax, cx                  ; terminal count (CX = 0 after the inc) is
.add:                           ; the whole half, not yet counted
    add dx, ax
    pop ax
.out:
    pop cx
    pop bx
    popf
    clc
    ret

; -----------------------------------------------------------------------------
; sbl_v_read - verb 5: copy recorded (or staged) bytes out of a grant
;
; in:       SI = source offset in SND_SEG (inside a grant the caller owns),
;           CX = bytes, DI = destination offset in the CALLER's segment
;           (BX, threaded per-call by slot 0x00EA's wrapper - SPEC.md 20.3)
; out:      AX = 0 ok / 7 refused (range not inside the caller's grant);
;           CF mirrors
; clobbers: nothing else (flags; ES restored per SPEC.md 1)
;
; The kernel-staged copy OUT - verb 6's exact mirror (SPEC.md 34.6):
; packages never hold an ES pointer into SND_SEG. No handle: the grant
; owns the bytes, so captured data stays readable after the stream that
; recorded it closes, until the grant is freed. Works on every machine,
; like verbs 6-7 - the pool exists wherever SND_SEG does. ANY-TASK
; (SPEC.md 20.3): the segment arriving in a register is what made it so -
; a parked .bss word could be overwritten by the next caller's wrapper
; while this caller sat preempted right here, and the rep movsb below
; would then WRITE into the wrong package's segment.
; -----------------------------------------------------------------------------
sbl_v_read:
    push cx
    push dx
    push si
    push di
    push es
    jcxz .ok                    ; nothing to copy is trivially fine
    call sbl_grant_chk          ; range-check the SOURCE
    jc .e7
    mov es, bx                  ; ES:DI = the caller's destination, in the
                                ; segment the slot wrapper threaded in BX -
                                ; the staging copy IS the v3 boundary
                                ; (SPEC.md 20.3/34.6; a package's DI is an
                                ; offset in its OWN segment, never the
                                ; kernel's)
    cld
.copy:                          ; IN CHUNKS, THE POOL SEGMENT RE-DERIVED PER
    jcxz .ok                    ; CHUNK UNDER IF=0 (SPEC.md 66.5.5): the pool
    mov ax, SBL_DCHUNK          ; is movable and this runs on the CALLER's
    cmp cx, ax                  ; task, which the compactor's park handshake
    jae .csz                    ; never asks - a worker preempted mid-copy
    mov ax, cx                  ; would resume through a stale segment. A
.csz:                           ; chunk cannot be preempted; between chunks
    sub cx, ax                  ; the word is read again, and a compaction is
    pushf                       ; atomic against this task (sch_lock)
    cli
    push cx
    mov cx, ax
    push ds
    mov ds, [sbl_poolseg]       ; DS:SI = the grant source, in the POOL claim
    SBL_MOVS                    ; pool:SI -> caller DI
    pop ds
    pop cx
    popf
    jmp .copy
.ok:
    xor ax, ax
    clc
    jmp .out
.e7:
    mov ax, 7
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; sbl_v_stage - verb 6: copy caller data into a grant (SPEC.md 34.6)
;
; in:       SI = source offset in the CALLER's segment (BX, threaded
;           per-call by slot 0x00EA's wrapper - SPEC.md 20.3), CX = bytes,
;           DI = destination offset in SND_SEG (inside a grant the caller
;           owns)
; out:      AX = 0 ok / 7 refused (range not inside the caller's grant);
;           CF mirrors
; clobbers: nothing else (flags; ES restored per SPEC.md 1)
;
; The kernel-staged copy in: packages never hold an ES pointer into
; SND_SEG (SPEC.md 2.2, the dsk_get_dir idiom). ANY-TASK (SPEC.md 20.3) -
; the sbl_v_read register rationale, in the read direction. A worker's own
; teardown cannot free the grant under this copy (deferred to
; OSAPI_TASK_ALIVE, SPEC.md 34.6); the residual same-package race is the
; SPEC.md 34.6 author rule: never verb-7-free a grant your worker may be
; mid-stage into.
; -----------------------------------------------------------------------------
sbl_v_stage:
    push cx
    push dx
    push si
    push di
    push es
    jcxz sbl_v_read.ok          ; nothing to copy is trivially fine
    push si
    mov si, di                  ; range-check the DESTINATION
    call sbl_grant_chk
    pop si
    jc sbl_v_read.e7
    cld
.copy:                          ; chunked exactly as sbl_v_read: the pool is
    jcxz sbl_v_read.ok          ; movable and this is the caller's task
    mov ax, SBL_DCHUNK          ; (SPEC.md 66.5.5)
    cmp cx, ax
    jae .csz
    mov ax, cx
.csz:
    sub cx, ax
    pushf
    cli
    mov es, [sbl_poolseg]       ; ES:DI = the grant, in the POOL claim -
                                ; re-derived per chunk, with DS still ours
    push cx
    mov cx, ax
    push ds
    mov ds, bx                  ; the caller's segment (slot wrapper, BX):
    SBL_MOVS                    ; the source pointer lives THERE since v3
    pop ds                      ; callerseg:SI -> pool:DI
    pop cx
    popf
    jmp .copy                   ; (the exits are sbl_v_read's: the same five
                                ; saves, the same two answers)

; -----------------------------------------------------------------------------
; sbl_v_grant - verb 7: staging-pool grants (SPEC.md 20.3/34.6)
;
; in:       AH = 0 alloc (CX = bytes; out SI = grant offset) or 1 free
;           (SI = grant offset)
; out:      AX = 0 ok (alloc: SI = offset) / 7 bad size, unknown sub-op,
;           not the caller's grant, or an open stream still reads from the
;           grant being freed (close it first) / 8 no space; CF mirrors
; clobbers: nothing else (flags)
;
; ANY-TASK (SPEC.md 20.3): alloc's scan+claim was always one pushf/cli
; unit inside sbl_grant_alloc; the FREE path's find -> live-stream-overlap
; check -> clear is now one window too (SPEC.md 34.3) - lock-free, a free
; racing an open on another task could clear a grant the just-opened
; stream reads from, exactly what the overlap check exists to refuse.
; -----------------------------------------------------------------------------
sbl_v_grant:
    push bx
    push dx
    or ah, ah
    jz .alloc
    cmp ah, 1
    jne .e7
                                ; read - outside the window is fine)
    pushf
    cli
    call sbl_grant_find
    jc .e7f
    cmp byte [sbl_str_act], 1   ; refused while an open stream reads from
    jne .fok                    ; this grant (SPEC.md 20.3 verb 7): the
    mov ax, [sbl_str_off]       ; refill task would copy unowned bytes -
    cmp ax, [sbl_gr_off+bx]     ; and a later alloc's, once re-granted
    jb .fok
    mov dx, [sbl_gr_off+bx]
    add dx, [sbl_gr_size+bx]
    dec dx                      ; DX = the grant's last byte (off+size can
    cmp ax, dx                  ; be 0x10000 exactly: compare last bytes)
    jbe .e7f                    ; live stream inside: close it first
.fok:
    mov word [sbl_gr_act+bx], 0
    popf
    call sbl_pool_put           ; ...and the pool itself if that was the last
    xor ax, ax                  ; grant. OUTSIDE the window: this frees
    clc                         ; through the kernel, which takes its own
    jmp .out
.e7f:
    popf
    jmp .e7
.alloc:
    call sbl_grant_alloc        ; with (force-freed at teardown, 34.3)
    jc .err                     ; AX already 7/8
    xor ax, ax
    clc
    jmp .out
.e7:
    mov ax, 7
.err:
    stc
.out:
    pop dx
    pop bx
    ret

; =============================================================================
; The refill machinery (SPEC.md 34.5): the transient kernel task and the
; grant -> double-buffer copies.
; =============================================================================

; -----------------------------------------------------------------------------
; sbl_fill_guard - one buffer half, act+gen-guarded: the refill task's fill,
;                  and verb 0's priming fills (which publish act first)
;
; in:       BX = half (0/1), DH = the stream generation this task serves;
;           [sbl_str_off]/[sbl_fed]/[sbl_total] current
; out:      nothing; on success [sbl_fed] is advanced and the half's valid
;           flag published (linear: 0x80 tail pad; ring: one whole half,
;           never padded - SPEC.md 34.5 rule 1)
; clobbers: nothing (flags)
;
; The plain fill with sbl_drain_half's teardown rule bolted on - and since
; verb 0's priming publishes the record before it fills, the ONLY fill: the
; un-guarded sbl_fill_half it once primed with is gone. The refill
; task runs with IF=1 and can be preempted anywhere across a close+reopen
; on the UI task; the un-guarded fill validated availability against one
; stream and then advanced the SUCCESSOR's [sbl_fed] (ring: to fed >
; total, making total - fed read ~0xF800 and flooding unstaged ring bytes
; through both availability checks) and could scribble a half the
; successor had already primed. So: the availability check, the counter
; reads and the [sbl_fed] advance are ONE pushf/cli window that verifies
; act + generation FIRST (advancing fed inside that window is safe - only
; the same-generation refill task reads it, and a gen that moves later
; means open rewrote it anyway); the grant -> half copy and the linear
; silence pad run in SBL_DCHUNK pieces, each its own act+gen-verified
; window (the destination half is the successor's too - a mismatch stops
; at the chunk edge); and the valid flag is published only under a final
; verified window, so a half is never published against a moved
; generation. Both source and destination live in SND_SEG (DS = ES =
; SND_SEG for the movs); kernel state is read before each switch.
; -----------------------------------------------------------------------------
sbl_fill_guard:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov es, [sbl_seg]
    xor di, di                  ; DI = half * [sbl_half]: the destination
    or bx, bx                   ; ([sbl_half] already carries the wide case,
    jz .dsh                     ; verb 0 having committed it)
    mov di, [sbl_half]
.dsh:
    pushf
    cli
    cmp byte [sbl_str_act], 1   ; verify INSIDE the window, before any
    jne .bail                   ; counter is read or advanced: a task
    cmp dh, [sbl_gen]           ; parked here across a close+reopen must
    jne .bail                   ; never touch the successor's counters
    mov ax, [sbl_total]
    sub ax, [sbl_fed]           ; AX = available (free-running-safe in
    jz .bail                    ; BOTH modes; <= RL in ring mode by the
    cmp byte [sbl_ring_on], 0   ; feed bound)
    je .lin
    cmp ax, [sbl_half]          ; ring: whole halves only, never pad -
    jb .bail                    ; short of one half is the normal underrun
    mov si, [sbl_fed]           ; path (SPEC.md 34.5 rule 1)
    and si, [sbl_rmask]         ; physical = str_off + (fed & (RL-1))
    add si, [sbl_str_off]
    mov cx, [sbl_half]
    xor bp, bp                  ; BP = silence pad: none in ring mode
    add [sbl_fed], cx
    jmp short .claimed
.bail:                          ; the first window's refusals, within reach
    jmp .dead                   ; of a SHORT jump: the four tests above fall
                                ; through on the path every fill takes
.lin:
    mov si, [sbl_str_off]       ; SI = grant + progress: the source
    add si, [sbl_fed]
    mov cx, ax
    cmp cx, [sbl_half]
    jbe .cok
    mov cx, [sbl_half]
.cok:
    mov bp, [sbl_half]          ; BP = silence pad after the data
    sub bp, cx
    add [sbl_fed], cx
.claimed:
    popf                        ; counters are claimed under this gen; the
    cld                         ; copy itself runs in guarded chunks
.copy:
    jcxz .pad
    mov ax, SBL_DCHUNK          ; AX = this chunk's size
    cmp cx, ax
    jae .csz
    mov ax, cx
.csz:
    sub cx, ax                  ; CX = remaining after this chunk
    pushf
    cli
    cmp byte [sbl_str_act], 1   ; re-verify per chunk (IF=0 excludes the
    jne .dead                   ; switch): a close+reopen on another task
    cmp dh, [sbl_gen]           ; must not see this copy scribble a half
    jne .dead                   ; the successor primed
    push cx
    mov cx, ax
    push ds
    mov ds, [sbl_poolseg]       ; DS = the POOL: the grant is there now, the
    SBL_MOVS                    ; destination half is in the DMA claim (ES)
    pop ds
    pop cx
    popf
    jmp .copy
.pad:
    mov cx, bp                  ; CX = pad remaining (linear short data)
.padc:
    jcxz .publish
    mov si, SBL_DCHUNK          ; SI's source duty is done: chunk size now
    cmp cx, si
    jae .psz
    mov si, cx
.psz:
    sub cx, si
    pushf
    cli
    cmp byte [sbl_str_act], 1   ; the pad writes the same shared half, so
    jne .dead                   ; it carries the same per-chunk guard
    cmp dh, [sbl_gen]
    jne .dead
    push cx
    mov cx, si
    mov al, 0x80                ; 8-bit unsigned silence
    rep stosb
    pop cx
    popf
    jmp .padc
.publish:
    pushf
    cli
    cmp byte [sbl_str_act], 1   ; publish only if every chunk landed under
    jne .dead                   ; this generation - a half must never go
    cmp dh, [sbl_gen]           ; valid after the generation moved
    jne .dead
    mov byte [sbl_valid+bx], 1
.dead:                          ; (first-window refusals fall out the same
                                ; way: nothing claimed, nothing published)
    popf
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sbl_refill - one refill pass (task context)
;
; in:       DH = the stream generation this task serves
; out:      nothing
; clobbers: nothing (flags)
;
; Fills the half the DSP needs next first, then the other - both through
; sbl_fill_guard, whose act+gen-verified windows keep a pass preempted
; across a close+reopen from acting on the successor stream. Then the
; resume check: an underrun-paused stream whose next half is now valid
; continues - flags, expectation, watchdog and ports in ONE pushf/cli
; window (SPEC.md 34.3), which RE-CHECKS act and generation first: a close
; (or teardown) that ran between the checks above and the window must not
; see D4h/14h re-arm a stream it already stopped, nor the ISR scribble a
; released record.
; -----------------------------------------------------------------------------
sbl_refill:
    push ax
    push bx
    mov bl, [sbl_play]          ; the half the DSP wants next, first
    xor bh, bh
    call .half
    xor bl, 1
    call .half
    cmp byte [sbl_paused], 0
    je .done
    mov bl, [sbl_play]          ; paused: can we continue?
    xor bh, bh
    cmp byte [sbl_valid+bx], 0
    je .done                    ; still starved
    pushf
    cli
    call sbl_live               ; re-verify under IF=0: closed since the
    jne .skip                   ; checks above, or superseded by a newer open?
    call sbl_resume             ; D4h / re-arm (SPEC.md 34.5)
.skip:
    popf
.done:
    pop bx
    pop ax
    ret
.half:                          ; fill half BX if invalid and data exists:
    cmp byte [sbl_valid+bx], 0  ; availability, the counter ops, the copy
    jne .hout                   ; and the valid publish all run act+gen-
    call sbl_fill_guard         ; guarded (the sbl_drain_half rule) - a
.hout:                          ; task parked here across a close+reopen
    ret                         ; must never act on the successor stream

; -----------------------------------------------------------------------------
; sbl_refill_task - the transient stream-pacing task (SPEC.md 34.5)
;
; Spawned by verb 0 (task_spawn arg: DL = owning instance -> T_INST,
; DH = the stream generation it serves). Copies grant -> double-buffer
; halves as the ISR flags them consumed, resumes an underrun-paused stream,
; and exits - via task_exit, never ret - when the stream is closed, torn
; down, superseded by a newer open, or stopped by the watchdog. No resident
; sound task exists (rejected, SPEC.md 34.5): this slot and its stack come
; from the ordinary dynamic pool and return to it with the stream.
; -----------------------------------------------------------------------------
sbl_refill_task:
    mov si, sbl_refill_pass     ; ONE LOOP FOR BOTH WORKERS (sbl_worker,
    jmp sbl_worker              ; below): only the pass differs
sbl_refill_pass:
    call OSAPI_TASK_PARK        ; ...and the heap compactor waits on THIS
                                ; (SPEC.md 66.5.5). Here, at the head of the
                                ; pass, is the one point in this task that
                                ; provably holds no pointer into the pool:
                                ; sbl_refill loads [sbl_poolseg] itself and
                                ; returns before we come back round, and its
                                ; every window re-verifies act + generation,
                                ; so a close while parked here is seen there.
                                ; Without the call no claim this driver owns
                                ; could ever be compacted
    jmp sbl_refill

; =============================================================================
; The drain machinery (SPEC.md 34.6): the transient kernel task and the
; record-ring -> grant copies - sbl_refill's input mirror.
; =============================================================================

; -----------------------------------------------------------------------------
; sbl_drain_half - copy one captured ring half into the caller's grant
;
; in:       BX = half (0/1), DH = the stream generation this task serves;
;           [sbl_str_off]/[sbl_consumed]/[sbl_total] current
; out:      nothing; [sbl_consumed] advanced by the bytes actually landed
;           (min(SBL_RHALF, capacity remaining); a half past capacity
;           copies nothing)
; clobbers: nothing (flags)
;
; Both source and destination live in SND_SEG (sbl_fill_guard's trick in
; reverse): DS = ES = SND_SEG for the movs. Unlike the output mirror, this
; copy WRITES grant bytes, and force-teardown (snd_release_inst on the UI
; task, or a verb-2 close + verb-7 free) frees - and a relaunch can
; re-grant - those bytes the moment the stream stops: a whole-half rep
; movsb (~21ms on an 8088) preempted mid-copy must never resume into
; freed pool memory. So the copy runs in SBL_DCHUNK pieces, each one
; pushf/cli window that re-verifies act + generation BEFORE writing (the
; sbl_refill resume rule): every act-clear and grant-free runs in task
; context, and IF=0 excludes the switch, so a chunk that starts writing
; holds the grant for its whole write - a torn-down stream stops at the
; chunk edge with nothing written after the free. [sbl_consumed] advances
; inside the window AFTER the chunk lands: verb 3 reports it as "bytes
; captured into the grant" (SPEC.md 20.3), so the poll-then-read pattern
; (verb 3, then verb 5) must never see it run ahead of the copy.
; (The output mirror got the same treatment: sbl_fill_guard guards the
; refill path's counters, copy and publish identically, because a reopen
; makes even the read-only output copy mutate the successor's [sbl_fed] -
; and verb 0 primes through it too, publishing act before it fills.)
; The caller clears the half's valid flag afterwards.
; -----------------------------------------------------------------------------
sbl_drain_half:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov es, [sbl_poolseg]       ; ES:DI = the grant, in the POOL claim; the
                                ; ring it comes FROM is in the DMA claim, so
                                ; this copy crosses the two now
    mov si, SBL_RING            ; SI = SBL_RING + half * SBL_RHALF: the
    or bx, bx                   ; source (the sbl_arm_rec derivation)
    jz .src
    add si, SBL_RHALF
.src:
    mov di, [sbl_str_off]       ; DI = grant + progress: the destination
    add di, [sbl_consumed]
    mov cx, [sbl_total]
    sub cx, [sbl_consumed]      ; CX = capacity remaining
    cmp cx, SBL_RHALF
    jbe .n
    mov cx, SBL_RHALF
.n:
    cld
.chunk:
    jcxz .done
    mov bx, SBL_DCHUNK          ; BX = this chunk's size
    cmp cx, bx
    jae .sz
    mov bx, cx
.sz:
    sub cx, bx                  ; CX = remaining after this chunk
    pushf
    cli
    cmp byte [sbl_str_act], 1   ; still the stream's grant? closed / torn
    jne .dead                   ; down: its pool bytes may be freed
    cmp dh, [sbl_gen]           ; superseded by a newer open?
    jne .dead
    push cx
    mov cx, bx
    push ds
    mov ds, [sbl_seg]           ; DS = the DMA claim, where the ring is
    SBL_MOVS                    ; one chunk, atomic vs. teardown (IF=0)
    pop ds
    pop cx
    add [sbl_consumed], bx      ; landed bytes only (verb 3's contract)
    popf
    jmp .chunk
.dead:
    popf                        ; stop writing: the grant is not ours
.done:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sbl_drain - one drain pass (task context)
;
; in:       DH = the stream generation this task serves
; out:      nothing
; clobbers: nothing (flags)
;
; Empties captured halves oldest-first (sbl_drain_h tracks the order the
; ISR fills them). Then two mirror duties, each under ONE pushf/cli window
; that RE-CHECKS act and generation first (the sbl_refill rule - a close
; or teardown between the checks and the window must not see the DSP
; restarted or a released record scribbled):
;   - capacity full while still capturing: stop the DSP (halt + exit
;     auto-init) and mark paused(1) - the input mirror of data exhaustion,
;     with sbl_consumed = capacity visible via verb 3 (SPEC.md 34.6);
;   - overrun-paused with the capture half now drained: resume (D4h /
;     re-arm), rewind the watchdog, expect IRQs again.
; -----------------------------------------------------------------------------
sbl_drain:
    push ax
    push bx
    call .one                   ; up to both halves, oldest first
    call .one
    mov ax, [sbl_total]
    cmp ax, [sbl_consumed]
    jne .notfull
    cmp byte [sbl_str_state], SBL_ST_PLAY
    jne .done                   ; already stopped / paused / ended
    pushf                       ; capacity reached while capturing: stop
    cli
    call sbl_live               ; re-verify under IF=0 (the sbl_refill
    jne .fskip                  ; rule): closed, or superseded, since?
    call sbl_halt               ; D0h, bounded
    cmp byte [sbl_auto], 0
    je .fnoda
    mov al, 0xDA                ; exit auto-init at the block edge
    call sbl_dsp_wr
.fnoda:
    mov byte [sbl_expect], 0
    mov byte [sbl_str_state], SBL_ST_UNDER  ; paused(1): capacity full -
.fskip:                                     ; the owner's cue to close
    popf
    jmp .done
.notfull:
    cmp byte [sbl_paused], 0    ; overrun-paused: can capture continue?
    je .done
    mov bl, [sbl_play]
    xor bh, bh
    cmp byte [sbl_valid+bx], 0
    jne .done                   ; the capture half is still un-drained
    pushf
    cli
    call sbl_live               ; the same re-verification under IF=0
    jne .rskip
    call sbl_resume             ; D4h / re-arm (input-aware)
.rskip:
    popf
.done:
    pop bx
    pop ax
    ret
.one:                           ; empty half [sbl_drain_h] if captured
    mov bl, [sbl_drain_h]
    xor bh, bh
    cmp byte [sbl_valid+bx], 0
    je .oout
    call sbl_drain_half         ; copy (capacity-capped), then retire the
    mov byte [sbl_valid+bx], 0  ; half and advance oldest-first order
    xor bl, 1
    mov [sbl_drain_h], bl
.oout:
    ret

; -----------------------------------------------------------------------------
; sbl_drain_task - the transient record-pacing task (SPEC.md 34.6)
;
; Spawned by verb 4 (task_spawn arg: DL = owning instance -> T_INST,
; DH = the stream generation it serves). Moves ring halves into the
; owner's grant as the ISR flags them captured, stops the DSP when the
; grant capacity fills, resumes an overrun-paused capture, and exits - via
; task_exit, never ret - when the stream is closed, torn down, superseded
; by a newer open, or stopped by the watchdog. Same pacing bound as
; sbl_refill_task: a ring half is >= 4.9 ticks even at the 15kHz input
; ceiling, so one-tick pacing always keeps up.
; -----------------------------------------------------------------------------
sbl_drain_task:
    mov si, sbl_drain           ; the drain pass, in the shared loop. The
    jmp short sbl_worker        ; jump is not dead weight: tests/unit/
                                ; t_drvclaim.py walks a task's cone by named
                                ; jumps and by the pass named into SI, and a
                                ; fallthrough is neither
sbl_worker:                     ; --- both workers: SI = the pass -----------
    mov byte [sbl_wtask], 1     ; detach waits on this (SPEC.md 51.7): our
.loop:
    call sbl_live               ; closed / torn down, or superseded by a
    jne .die                    ; newer stream?
    cmp byte [sbl_str_state], SBL_ST_END
    je .die                     ; watchdog-stopped: nothing left to pace
    call si                     ; sbl_refill_pass / sbl_drain: both keep SI
    mov ax, 1                   ; a block is >= 1.7 ticks at the 22kHz
    call OSAPI_TASK_SLEEP       ; ceiling: one-tick pacing always keeps up
    jmp .loop
.die:
    mov byte [sbl_wtask], 0     ; ...and clears it as its last act but one
    xor ax, ax                  ; AX = 0: this worker is exiting (SPEC.md
    call OSAPI_DRV_TASK         ; 51.7). NEVER RETURNS

; =============================================================================
; Tick, teardown and boot glue (called from snd.inc, SPEC.md 34.5/34.7)
; =============================================================================

; -----------------------------------------------------------------------------
; sbl_tick - the stream watchdog (SPEC.md 34.5), a snd_tick leaf
;
; in:       IF = 0, DS = KERNEL_SEG, behind the snd_live gate; AX/DX free
; out:      nothing
; clobbers: AX, DX
;
; A block IRQ that fails to arrive within ~2x the block period stops the
; stream instead of hanging the owner: halt, stop expecting, state = ended.
; The refill task exits on seeing the state; the owner sees it via verb 3
; and closes. Idle cost: one compare and a branch.
; -----------------------------------------------------------------------------
sbl_tick:
    cmp byte [sbl_str_act], 1
    jne .out
    cmp byte [sbl_expect], 0 ; only while a transfer is armed (paused /
    je .out                     ; ended streams expect nothing)
    cmp byte [sbl_str_state], SBL_ST_PLAY
    jne .out
    dec word [sbl_wd]
    jnz .out
    call sbl_halt               ; the IRQ never came: stop the stream
    mov byte [sbl_expect], 0
    mov byte [sbl_str_state], SBL_ST_END
.out:
    ret

; -----------------------------------------------------------------------------
; sbl_release_inst - force-release every SB grant an instance holds
;
; in:       AL = instance slot
; out:      nothing
; clobbers: nothing (flags)
;
; snd_release_inst's SB leg (SPEC.md 34.3): closes a live stream the dying
; instance owns (its refill task exits at the next wake) and frees every
; staging grant stamped with it. A closed package can never leave a
; dangling stream or a leaked grant.
; -----------------------------------------------------------------------------
sbl_release_inst:
    push bx
    cmp byte [sbl_str_act], 1
    jne .grants
    cmp al, [sbl_str_inst]
    jne .grants
    call sbl_stop_stream
.grants:
    xor bx, bx
.g:
    cmp word [sbl_gr_act+bx], 0
    je .next
    cmp [sbl_gr_inst+bx], al
    jne .next
    mov word [sbl_gr_act+bx], 0
.next:
    add bx, 2
    cmp bx, SBL_GMAX*2
    jb .g
    call sbl_pool_put           ; a dying instance may have held the last one
    pop bx
    ret

; =============================================================================
; Attach and detach - the driver half of what used to be boot-time kernel
; wiring (SPEC.md 51.4)
; =============================================================================

;SBL_WANT: the double buffer's claim in KB, and all of it the chip's
SBL_WANT   equ SBL_DMASZ / 1024

; -----------------------------------------------------------------------------
; sbl_attach - find a card
; in:  nothing
; out: CF = 0 attached; CF = 1 and AL = DRVE_HW (no card) or DRVE_TWICE -
;      and in either case NOTHING is hooked and NOTHING is held (SPEC.md 51.2)
; clobbers: AX (flags)
;
; IT CLAIMS NOTHING (SPEC.md 34.5.2). The 8KB double buffer was claimed here
; and held for the session, whether or not anything ever played - and the
; streams that matter now play straight out of their own ring and never touch
; it. A double-buffered stream claims it at open and frees it at close, so an
; idle card costs the heap its image and not a byte more, and DRVE_MEM moved to
; where memory is actually wanted: verb 0 and verb 4 answer 8.
; -----------------------------------------------------------------------------
sbl_attach:
    cmp byte [sbl_up], 0
    jne .twice                  ; ALREADY ATTACHED, so somebody attached twice
                                ; and that is a bug in the CALLER - not a fact
                                ; about the machine, and not something to
                                ; recover from quietly. Refuse before the state
                                ; reset and before the first port write, so a
                                ; live stream, the hooked vector and the running
                                ; DSP are all exactly as they were
    call sbl_state_init         ; before any port write: a reload starts clean
    call sbl_f_probe            ; the reset scan + E1h version gate
    or al, al
    jz .nohw
    mov byte [sbl_up], 1        ; publish LAST (SPEC.md 29.2): the stream
    clc                         ; verbs test this and nothing else
    jmp short .out
.nohw:
    mov al, DRVE_HW
    stc
    jmp short .out
.twice:
    mov al, DRVE_TWICE
    stc
.out:                           ; (no saves: sbl_state_init and sbl_f_probe
    ret                         ; keep every register but AX)

; -----------------------------------------------------------------------------
; sbl_dma_get - claim the double buffer, if it is not already held
; in:  nothing
; out: CF = 0 and [sbl_seg]/[sbl_page]/[sbl_dmaoff] set; CF = 1 the heap has
;      nothing page-safe, and nothing changed
; clobbers: nothing (flags)
;
; **The 8237 has no register for address bits 16..19.** Port 83h holds them
; for channel 1 and the chip never carries into them, so a transfer that
; would cross a 64KB PHYSICAL boundary wraps to the start of its page and
; plays the wrong memory. OSAPI_MEM_CLAIM_DMA_HI moves that test inside the
; allocator's own scan (SPEC.md 50.3), and CX = AX because the chip addresses
; all 8KB. From the TOP (SPEC.md 50.3.2), the door for a buffer the hardware
; holds the address of.
;
; NOT DECLARED MOVABLE, and it does not need to be: it lives exactly as long
; as one double-buffered stream (sbl_unpin frees it), and for all of that the
; chip may be pointed at it. So sbl_ring_reloc is gone - SPEC.md 66.6.4's
; three words are recomputed at every claim instead.
; -----------------------------------------------------------------------------
sbl_dma_get:
    cmp word [sbl_seg], 0
    jne .ok                     ; a watchdog-ended stream's, still held
    push ax
    push cx
    push dx
    mov ax, SBL_WANT
    mov cx, ax
    call OSAPI_MEM_CLAIM_DMA_HI ; out CF, DX = base segment
    jc .fail
    mov [sbl_seg], dx
    mov ax, dx                  ; ...and the two words the base decides: the
    mov cl, 4                   ; buffer's offset inside its 64KB physical
    shl ax, cl                  ; page (the low 16 bits of base x 16)...
    mov [sbl_dmaoff], ax
    mov cl, 12                  ; ...and which page that is
    shr dx, cl
    mov [sbl_page], dl
    clc
.fail:                          ; (CF = 1 from the claim)
    pop dx
    pop cx
    pop ax
    ret
.ok:
    clc
    ret

; sbl_dma_put - free the double buffer if it is held. Clobbers nothing.
; Never from inside a cli window: it frees through the kernel.
sbl_dma_put:
    push dx
    mov dx, [sbl_seg]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
    mov word [sbl_seg], 0
.out:
    pop dx
    ret

; -----------------------------------------------------------------------------
; sbl_ring_phys - where the 8237 would see a ring in the pool (SPEC.md 34.5.2)
; in:  SI = the ring's offset in [sbl_rseg] - the pool, or an external ring's
;      own claim (SPEC.md 34.5.3) - and [sbl_rmask] = RL - 1
; out: CF = 0 the whole ring is inside one 64KB physical page, AX = its offset
;      in that page and DL = the page; CF = 1 it straddles one
; clobbers: DH (flags)
; -----------------------------------------------------------------------------
sbl_ring_phys:
    push cx
    mov ax, [sbl_rseg]
    mov dx, ax
    mov cl, 4
    shl ax, cl                  ; the pool's base, low 16 bits...
    mov cl, 12
    shr dx, cl                  ; ...and its page
    add ax, si                  ; + the ring's offset in the pool, carrying
    adc dl, 0                   ; into the page
    push ax
    add ax, [sbl_rmask]         ; the ring's last byte: a carry out of the
    pop ax                      ; low 16 bits is a page crossed (pop and pop
    pop cx                      ; leave the flags alone)
    ret

; -----------------------------------------------------------------------------
; sbl_reloc - THE HEAP COMPACTOR MOVED THE STAGING POOL (SPEC.md 66.2/66.5.5)
; in:  BX = the base segment it WAS at, DX = the base it is at NOW.
;      DS = CS = ours, ES = KERNEL_SEG. The bytes have already moved.
; out: nothing; every register preserved
;
; ONE WORD, and that is a property of the design rather than luck. A grant is
; an OFFSET into the pool, never a segment - sbl_grant_alloc hands out spans
; and sbl_grant_chk bounds them - and no client ever sees the pool at all,
; because the staging copy IS the v3 boundary (SPEC.md 20.3/34.6): a package's
; DI is an offset in its OWN segment and the driver `rep movsb`s across. So
; nothing outside this file derives anything from [sbl_poolseg], and nothing
; inside it keeps a copy.
;
; ...EXCEPT THE 8237, while a DIRECT stream plays out of the pool (SPEC.md
; 34.5.2) - and that is why verb 0 pins the pool for such a stream and
; sbl_unpin declares this proc again only once the channel is masked. So the
; compactor never calls this with the chip pointed at the pool.
; -----------------------------------------------------------------------------
sbl_reloc:
    cmp bx, [sbl_poolseg]
    jne .out
    mov [sbl_poolseg], dx
.out:
    ret

; -----------------------------------------------------------------------------
; sbl_pool_get - claim the staging pool, if it is not already held
;
; in:  CX = the grant about to be asked of it (1..SBL_POOLKB KB)
; out: CF = 0 and [sbl_poolseg]/[sbl_poolsz] usable; CF = 1 the heap could
;      not fund it and NOTHING changed
; clobbers: nothing (flags)
;
; SIZED TO THE ASK (SPEC.md 34.6.3). It was a flat SBL_POOLKB, walking down
; in 4KB tiers when the heap would not fund it - so a Tracker playing an 8KB
; ring held a 20KB pool, and a 16KB one held 4KB it never touched. Now the
; first grant decides: its size in whole KB, at least SBL_POOLMIN so a small
; first grant leaves room beside it. A later grant that does not fit GROWS the
; pool (sbl_pool_grow). And the tiers are gone because they cannot help: a
; pool smaller than the grant it was claimed for is a refusal either way, and
; the callers that can live with less - Tracker, Audio, the Recorder - already
; tier their own grant and ask again.
;
; PAGE-SAFE WHEN IT CAN BE (SPEC.md 34.5.2): OSAPI_MEM_CLAIM_DMA_HI with the
; whole pool as the head, so a ring granted out of it is one the 8237 can play
; in place - which is what spares the 8KB double buffer. From the TOP, the door
; for memory a chip is pointed at (SPEC.md 50.3.2); movable all the same, the
; compactor honouring the head (SPEC.md 66.4.2). When the heap has no such run
; the plain claim is the fallback: the pool still works, and a stream out of
; it takes the copy path as every stream used to.
; -----------------------------------------------------------------------------
sbl_pool_get:
    push ax
    push bx
    push cx
    push dx
    cmp word [sbl_poolseg], 0
    jne .ok                     ; already held: grants share one claim
    call sbl_kb                 ; AX = CX in whole KB
    cmp ax, SBL_POOLMIN
    jae .min
    mov ax, SBL_POOLMIN
.min:
    mov bx, ax                  ; BX = the KB, across both asks
    mov cx, ax                  ; the whole pool is the chip's
    call OSAPI_MEM_CLAIM_DMA_HI ; out CF, DX = base segment
    jnc .got
    mov ax, bx
    call OSAPI_MEM_CLAIM_HI     ; not page-safe, but a pool all the same
    jc .fail
.got:
    mov [sbl_poolseg], dx
    mov ax, bx                  ; [sbl_poolsz] = the KB in bytes
    mov cl, 10
    shl ax, cl
    mov [sbl_poolsz], ax
    mov ax, sbl_reloc           ; ...and it is MOVABLE (SPEC.md 66.5.5): a
    call OSAPI_MEM_MOVABLE      ; LATE claim, so it lands wherever the heap
                                ; is by then (docs/FIELD-NOTES.md 2)
.ok:
    clc
.fail:                          ; (both refusals arrive with CF = 1)
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sbl_pool_grow - make room in a held pool for one more grant of CX bytes
; in:  CX = the grant that did not fit; no stream open
; out: CF = 0 the pool is bigger now (and may have moved); CF = 1 it is at its
;      ceiling or the heap refused, and nothing changed
; clobbers: nothing (flags)
;
; OSAPI_MEM_REGROW, which extends in place when it can and moves otherwise,
; the page-safe head re-honoured either way (SPEC.md 50.3). It adds the WHOLE
; grant at the top rather than the shortfall, because a pool with room that is
; not contiguous is the case this is reached in as often as a small one. Never
; with a stream open: a direct one is playing out of this very claim, and a
; double-buffered one has a worker copying from it.
; -----------------------------------------------------------------------------
sbl_pool_grow:
    push ax
    push cx
    push dx
    call sbl_kb                 ; AX = the grant in KB...
    mov cl, [sbl_poolsz+1]      ; CX = the pool in KB: always whole KB
    xor ch, ch                  ; (sbl_pool_get), so the high byte / 4
    shr cx, 1
    shr cx, 1
    add ax, cx                  ; ...plus the pool
    cmp ax, SBL_POOLKB
    jbe .ask
    mov ax, SBL_POOLKB          ; the ceiling (a recording holds up to it)
    cmp ax, cx
    jbe .fail                   ; ...and the pool is already there
.ask:
    mov dx, [sbl_poolseg]
    call OSAPI_MEM_REGROW       ; out CF, DX = the base NOW
    jc .fail
    mov [sbl_poolseg], dx
    mov cl, 10                  ; [sbl_poolsz] = AX KB in bytes
    shl ax, cl
    mov [sbl_poolsz], ax
    CLC_OR_STC .fail
.out:
    pop dx
    pop cx
    pop ax
    ret

; sbl_kb - AX = CX bytes in whole KB, rounded up (CX up to 0xFFFF). Flags.
sbl_kb:
    push cx
    mov ax, cx
    add ax, 1023                ; the carry is bit 16 of the sum...
    rcr ax, 1                   ; ...and comes back in as bit 15
    mov cl, 9
    shr ax, cl                  ; 17 bits >> 10 in all
    pop cx
    ret


; -----------------------------------------------------------------------------
; sbl_pool_put - release the staging pool once the last grant has gone
;
; in:  nothing
; out: nothing; a no-op while any grant record is live
; clobbers: nothing (flags)
;
; Called from the two places a grant record is cleared, and from detach.
; **Never from inside their pushf/cli windows** - this frees through the
; kernel's API, and the claim table takes its own critical section.
; -----------------------------------------------------------------------------
sbl_pool_put:
    push ax
    push bx
    push dx
    cmp word [sbl_poolseg], 0
    je .out                     ; nothing held
    xor bx, bx
.g:
    cmp word [sbl_gr_act+bx], 0
    jne .out                    ; somebody still holds bytes in it
    add bx, 2
    cmp bx, SBL_GMAX*2
    jb .g
    mov dx, [sbl_poolseg]
    call OSAPI_MEM_FREE
    xor ax, ax
    mov [sbl_poolseg], ax
    mov [sbl_poolsz], ax
.out:
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sbl_detach - stop everything, unhook, and give the memory back
; in:  nothing
; out: nothing. CANNOT FAIL (SPEC.md 51.2), and MUST NOT RETURN until the
;      refill/drain worker is gone - the kernel frees our image next.
; clobbers: nothing (flags)
; -----------------------------------------------------------------------------
sbl_detach:
    push ax
    push cx
    push dx
    push di
    cmp byte [sbl_up], 0
    je .out
    mov byte [sbl_up], 0        ; no new stream can open from here on
    cmp byte [sbl_str_act], 0
    je .quiet
    call sbl_stop_stream        ; halt the DSP and clear the record; the
                                ; worker's own loop test is sbl_str_act, so
                                ; this is what tells it to die
.quiet:
    mov cx, 40                  ; ...and WAIT for it, bounded in real TICKS
.wait:                          ; (~2s): 40 bare yields pass in microseconds
    call OSAPI_GET_TICKS        ; against a worker that sleeps a tick between
    mov dx, ax                  ; passes, and timed out with it still asleep.
.tk:                            ; This wait is best-effort ordering - the
    cmp byte [sbl_wtask], 0     ; worker's last card touch before the DSP
    je .gone                    ; reset below - not the survival guarantee:
    call OSAPI_TASK_YIELD       ; the KERNEL waits for the death itself
    call OSAPI_GET_TICKS        ; ([drv_wcnt], cleared inside task_exit) in
    cmp ax, dx                  ; drv_unload, before the image is freed
    je .tk                      ; (SPEC.md 51.7). Same tick still: yield again
    loop .wait
.gone:
    call sbl_unhook             ; vector, mask and DSP back as we found them
    mov di, sbl_gr_act          ; every grant is void once the card is gone,
    mov cx, SBL_GMAX * 2        ; so the pool can go with it
    xor al, al
    call snd_fill
    call sbl_pool_put
    call sbl_dma_put            ; ...and the buffer LAST, after the vector is
.out:                           ; gone: freeing it first would leave a live
    pop di                      ; IRQ pointing into the next claim
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sbl_unhook - quiesce the card for reboot (SPEC.md 34.7)
;
; in:       nothing (snd_unhook calls this after the speaker restore)
; out:      nothing
; clobbers: nothing (flags)
;
; The mirror image of the discovery hook: mask the IRQ at the 8259, reset
; the DSP (silences any transfer and returns it to its power-on state),
; restore the saved vector. A never-hooked card needs nothing.
; -----------------------------------------------------------------------------
sbl_unhook:
    push ax
    push bx
    push cx
    push dx
    push es
    cmp byte [sbl_hooked], 0
    je .out
    mov cl, [sbl_irq]           ; mask the line first: no IRQ mid-teardown
    mov ah, 1
    shl ah, cl
    in al, 0x21
    or al, ah
    out 0x21, al
    mov cx, 2000                ; DSP reset - the probe's own routine, the
    call sbl_f_reset            ; scan's per-base budget (~10ms of ISA reads)
    mov al, [sbl_irq]           ; restore the saved vector
    add al, 8                   ; IRQ n -> int 8+n
    mov ah, 0
    shl ax, 1
    shl ax, 1
    mov bx, ax
    xor ax, ax
    mov es, ax
    pushf
    cli
    mov ax, [sbl_oldvec]
    mov [es:bx], ax
    mov ax, [sbl_oldvec+2]
    mov [es:bx+2], ax
    popf
    mov byte [sbl_hooked], 0
.out:
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sbl_state_init - store the module's .bss before the services publish
;
; in:       nothing (called from sbl_attach; the probe that fills sbl_base and
;           the version runs AFTER this)
; out:      sbl_zb..sbl_ze zeroed, then [sbl_irq] / [sbl_dsc_fired] /
;           [sbl_str_inst] = 0xFF and [sbl_half] = SBL_HALF
; clobbers: nothing (flags)
;
; ONE FILL over the run SBL_STATE lays out for it, rather than a store per
; cell - the cells were already one record in all but their order, and the
; run is exactly the cells an attach must start from zero.
; -----------------------------------------------------------------------------
sbl_state_init:
    push ax
    push cx
    push di
    mov di, sbl_zb
    mov cx, sbl_ze - sbl_zb
    xor ax, ax
    call snd_fill
    dec ax                      ; AL = 0xFF
    mov [sbl_irq], al           ; discovery deferred to first use (34.5)
    mov [sbl_dsc_fired], al
    mov [sbl_str_inst], al
    mov word [sbl_half], SBL_HALF
    pop di
    pop cx
    pop ax
    ret

; =============================================================================
; Detection and IRQ discovery. Kernel-resident ancestry: these were far
; code behind FARSHIMs with KCALL'd DSP handshakes; in the driver they are
; ordinary near calls in the image's own segment, DS = the driver, and the
; tables above and all sbl_ state are plain [addr] reads.
; =============================================================================


; -----------------------------------------------------------------------------
; sbl_f_probe - the SB reset scan + version gate (far body; SPEC.md 34.2)
;
; in:       nothing (DS = the driver's segment; attach-time, from the
;           DRVV_ATTACH probe)
; out:      AL = 1 present - [sbl_base] set, DSP version read via E1h,
;           [sbl_auto] decided (>= 2.00); AL = 0 absent
; clobbers: AH (the attach probe holds nothing in it); nothing else
;
; Scan order pinned: {220h,240h,210h,230h,250h,260h}, a bounded poll budget
; per base (an absent bus floats FFh and fails the 0AAh compare fast); one
; slow retry on 220h for a present-but-slow DSP (SPEC.md 34.2). Presence is
; published by the caller AFTER this returns - publish-last.
; -----------------------------------------------------------------------------
sbl_f_probe:
    push bx
    push cx
    push dx
    push si
    cld
    mov si, sbl_bases
.base:
    lodsw
    or ax, ax
    jz .retry                   ; list exhausted: one slow retry on 220h
    mov [sbl_base], ax
    mov cx, 2000                ; ~10ms of ISA status reads per base
    call sbl_f_reset
    jnc .found
    jmp .base
.retry:
    mov word [sbl_base], 0x220
    xor cx, cx                  ; 65536 polls: the ~100ms slow-DSP allowance
    call sbl_f_reset
    jc .no
.found:
    mov al, 0xE1                ; DSP version: the auto-init strategy gate
    call sbl_dsp_wr
    jc .no
    call sbl_dsp_rd
    jc .no
    mov [sbl_verhi], al
    call sbl_dsp_rd
    jc .no
    mov [sbl_verlo], al
    mov al, [sbl_verhi]         ; >= 2.00: auto-init DMA (SPEC.md 34.5)
    cmp al, 2                   ; CF = below 2.00...
    sbb al, al                  ; ...AL = 0FFh below, 0 at or above...
    inc ax                      ; ...AL = 0 below, 1 at or above
    mov [sbl_auto], al
    mov al, 1                   ; [sbl_base] is the base the scan accepted
    jmp .out
.no:
    mov word [sbl_base], 0      ; **AND THE SCAN'S OWN SCRATCH WITH IT** (SPEC.md
                                ; 51.11.2.1). This cell is the scan's cursor as
                                ; well as its answer - `.retry` above writes
                                ; 0x220 into it to make one slow attempt at the
                                ; head of the list - so a machine with no DSP
                                ; anywhere left this routine having REFUSED and
                                ; still naming a base. `sbl_attach` reads AL and
                                ; is not fooled; `snd_hwinfo` reads this WORD and
                                ; was, so an AdLib-only machine - where the OPL
                                ; attaches, the driver stays mounted and the
                                ; suspend therefore asks - published
                                ; `BLASTER=A220 D1 T1`: a Sound Blaster at 220h,
                                ; on DMA 1, that is not in the machine.
                                ; `sbl_state_init` cannot cover this, running
                                ; BEFORE the probe rather than after it
    xor al, al
.out:
    pop si
    pop dx
    pop cx
    pop bx
    ret                         ; near: .fartext is retired here

; -----------------------------------------------------------------------------
; sbl_f_reset - reset the DSP at [sbl_base] and wait for 0AAh (far-local)
;
; in:       [sbl_base], CX = poll budget (0 = 65536)
; out:      CF = 0 a DSP answered; CF = 1 nothing there
; clobbers: AL, CX (flags)
; -----------------------------------------------------------------------------
sbl_f_reset:
    push dx
    mov dx, [sbl_base]
    add dx, 6                   ; 2x6h: reset
    mov al, 1
    out dx, al
    push cx
    add dx, 8                   ; hold >= 3us: 8 ISA reads of 2xEh
    mov cx, 8
.dly:
    in al, dx
    loop .dly
    sub dx, 8
    pop cx
    xor al, al
    out dx, al
    add dx, 8                   ; 2xEh: data-available status
.poll:
    in al, dx
    test al, 0x80
    jz .next
    sub dx, 4                   ; 2xAh: read the identity byte
    in al, dx
    add dx, 4
    cmp al, 0xAA
    je .yes
.next:
    loop .poll
    stc
    jmp .ret
.yes:
    clc
.ret:
    pop dx
    ret                         ; near: called from sbl_f_probe (same CS)

; -----------------------------------------------------------------------------
; sbl_f_irqdisc - IRQ discovery orchestration (far body; SPEC.md 34.5)
;
; in:       nothing (task context, IF = 1 - the wait needs live ticks)
; out:      AL = 1 - [sbl_irq] set, sbl_isr hooked on the winner
;           (mouse_init order: saved old vector, installed while masked,
;           unmasked) and staying hooked until snd_unhook; AL = 0 no
;           candidate confirmed (retried once)
; clobbers: nothing else (flags)
;
; Hook all four candidates onto the .text stubs, open their mask bits,
; issue F2h (trigger-8-bit-IRQ, supported by every DSP), and wait ~3 ticks
; for a stub to CONFIRM via 2xEh bit 7 - a coincidental spurious IRQ 7
; fails the confirm and is ignored. Losers are unhooked and the saved PIC
; mask restored before the winner is re-hooked properly.
; -----------------------------------------------------------------------------
sbl_f_irqdisc:
    push bx
    push cx
    push dx
    push si
    push es
    xor ax, ax
    mov es, ax
    cmp byte [sbl_verhi], 4     ; SB16-class DSP: the CT1745 mixer reads
    jb .probe                   ; the configured line back - register 80h
    mov dx, [sbl_base]          ; via index 2x4h / data 2x5h, magic bits
    add dx, 4                   ; 1 = IRQ2, 2 = IRQ5, 4 = IRQ7, 8 = IRQ10
    mov al, 0x80                ; (QEMU and 86Box agree; SPEC.md 34.5).
    out dx, al                  ; Deterministic: no F2h, nothing hooked or
    inc dx                      ; unmasked until the winner is.
    in al, dx
    mov bl, 3                   ; IRQ2 -> candidate 3 of {7,5,3,2}
    cmp al, 1
    je .mwin
    mov bl, 1                   ; IRQ5 -> candidate 1
    cmp al, 2
    je .mwin
    mov bl, 0                   ; IRQ7 -> candidate 0
    cmp al, 4
    jne .probe                  ; IRQ10 (slave PIC) or garbage: fall back
.mwin:                          ; to the firing probe below
    jmp .win                    ; (one long jump for the three short ones)
.probe:
    xor si, si                  ; hook the 4 candidates onto the stubs: the
.cs:                            ; stubs are OURS, not the kernel's, and this
    mov [sbl_dsc_t+si+4], cs    ; image may have moved since the last time
    add si, 6
    cmp si, 6 * SBL_DSC_N
    jb .cs
    call .swap                  ; stubs in, the old vectors kept
    in al, 0x21                 ; open the candidates' mask bits
    mov [sbl_dsc_mask], al
    and al, 0x53                ; ~(80h|20h|08h|04h)
    out 0x21, al
    mov byte [sbl_dsc_fired], 0xFF
    mov cx, 2                   ; F2h, retried once (SPEC.md 34.5)
.try:
    mov al, 0xF2                ; trigger 8-bit IRQ
    call sbl_dsp_wr
    jc .settle                  ; DSP wedged: nothing will fire
    call OSAPI_GET_TICKS
    mov bx, ax
.wait:
    cmp byte [sbl_dsc_fired], 0xFF
    jne .settle
    call OSAPI_GET_TICKS
    sub ax, bx
    cmp ax, 3                   ; ~165ms per attempt
    jb .wait
    loop .try
.settle:
    mov al, [sbl_dsc_mask]      ; restore the PIC mask...
    out 0x21, al
    call .swap                  ; ...and every candidate's vector
    mov bl, [sbl_dsc_fired]
    cmp bl, 0xFF
    je .none
.win:
    xor bh, bh                  ; the winner: BX = candidate index
    mov al, [sbl_dsc_irqn+bx]
    mov [sbl_irq], al
    add al, 8                   ; SI = (IRQ + 8) * 4: its IVT cell
    cbw
    shl ax, 1
    shl ax, 1
    mov si, ax
    mov ah, [sbl_dsc_mbit+bx]   ; mouse_init order: mask...
    in al, 0x21
    or al, ah
    out 0x21, al
    pushf                       ; ...save the old vector + install sbl_isr
    cli
    mov ax, [es:si]
    mov [sbl_oldvec], ax
    mov ax, [es:si+2]
    mov [sbl_oldvec+2], ax
    mov word [es:si], sbl_isr
    mov word [es:si+2], cs      ; sbl_isr lives in this driver's segment
    popf
    mov byte [sbl_hooked], 1
    mov ah, [sbl_dsc_mbit+bx]   ; ...and unmask
    not ah
    in al, 0x21
    and al, ah
    out 0x21, al
    mov al, 1
    jmp .out
.none:
    xor al, al
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    ret                         ; near: .fartext is retired here
.swap:                          ; ES = 0: every candidate's vector exchanged
    xor si, si                  ; with its far pointer in sbl_dsc_t, each
.sw:                            ; under its own pushf/cli
    mov bx, [sbl_dsc_t+si]
    pushf
    cli
    mov ax, [es:bx]
    xchg ax, [sbl_dsc_t+si+2]
    mov [es:bx], ax
    mov ax, [es:bx+2]
    xchg ax, [sbl_dsc_t+si+4]
    mov [es:bx+2], ax
    popf
    add si, 6
    cmp si, 6 * SBL_DSC_N
    jb .sw
    ret


; =============================================================================
; state (SPEC.md 34.7) - stored by sbl_state_init at attach, before the
; services publish; unreadable until they do. sbl_expect and sbl_under keep
; the names SPEC.md 34.5 pins for them.
;
; A MACRO, EXPANDED BY sound.asm AT THE VERY END OF THE IMAGE, and every cell
; is `db 0`/`dw 0`: os88drv.py takes a driver's trailing zero paragraphs off
; the file and the kernel zeroes them at load (drivers/os88drv.inc), so state
; that sat in the middle of the code shipped on every disk and this does not.
; The cells that start at 0xFF or SBL_HALF are stored by sbl_state_init,
; which every attach runs before anything reads them.
;
; ORDER IS LOAD-BEARING IN ONE PLACE: sbl_zb..sbl_ze is the run sbl_state_init
; clears in one fill, so a cell belongs inside it exactly when an attach must
; start it from zero. Outside it are the claims and the flags an attach must
; NOT reset - [sbl_wtask] (a worker detach timed out on still owns it) and
; [sbl_clip] (the kernel's half of the clip exclusion) among them.
; =============================================================================
%macro SBL_STATE 0
sbl_zb:                         ; --- cleared at every attach ----------------
sbl_base:     dw 0              ; DSP base port (220h..260h), 0 = none
sbl_verhi:    db 0              ; DSP version from E1h: major
sbl_verlo:    db 0              ;   minor
sbl_auto:     db 0              ; 1 = auto-init strategy (DSP >= 2.00, 34.5)
sbl_irq:      db 0              ; discovered IRQ line, 0xFF = not yet
sbl_hooked:   db 0              ; sbl_isr installed on [sbl_irq]'s vector
sbl_dsc_fired: db 0             ; discovery: confirmed candidate index
sbl_expect:   db 0              ; a block IRQ is legitimate (SPEC.md 34.5)
sbl_under:    dw 0              ; underrun-pause count (debug, cumulative)

sbl_str_act:  db 0              ; stream record: 1 = open
sbl_gen:      db 0              ;   generation = the handle (staleness)
sbl_str_inst: db 0              ;   owning instance (0xFF = kernel)
sbl_str_state: db 0             ;   SBL_ST_* (verb 3)
sbl_str_dir:  db 0              ;   0 = output, 1 = input (SPEC.md 34.6)
sbl_str_off:  dw 0              ;   grant offset the stream reads from
                                ;   (input: captures into)
sbl_str_max:  dw 0              ;   bytes from there to the grant's end
sbl_total:    dw 0              ;   valid bytes (grows via verb 1);
                                ;   input: the capture capacity
sbl_fed:      dw 0              ;   bytes copied into the double buffer
sbl_consumed: dw 0              ;   bytes played (capped at sbl_total);
                                ;   input: bytes drained into the grant
sbl_play:     db 0              ;   the half the DSP sounds (input:
                                ;   captures into) next
sbl_valid:    db 0, 0           ;   per-half: refilled since last consumed
                                ;   (input: captured, awaiting drain)
sbl_drain_h:  db 0              ;   input: next half the drain task empties
sbl_paused:   db 0              ;   D0h issued; sbl_go_on owed
sbl_tc:       db 0              ;   DSP time constant for the open rate
sbl_ring_on:  db 0              ;   1 = ring mode (SPEC.md 34.5): the three
                                ;   counters are free-running mod 65536 and
                                ;   fills are whole unpadded halves
sbl_rmask:    dw 0              ;   ring: RL-1 (RL = power of two 4096..
                                ;   32768); physical offset of stream byte
                                ;   n = sbl_str_off + (n & sbl_rmask)
sbl_wd:       dw 0              ;   watchdog countdown (snd_tick, 34.5)
sbl_wd_rst:   dw 0              ;   its reload: ~2 block periods in ticks
sbl_half:     dw 0              ;   the open's half size: 2048, or 4096 in
                                ;   the wide-rate regime (SPEC.md 34.5 -
                                ;   the 2x4KB buffer annexes the record
                                ;   ring's first 4KB, idle by half-duplex)
sbl_wide:     db 0              ;   1 = wide: rate via 41h, start via C6h
sbl_hisp:     db 0              ;   1 = SB Pro high-speed: TC + 90h, and
                                ;   sbl_halt masks DMA / sbl_stop_stream resets
sbl_rate:     dw 0              ;   wide: the open rate in Hz (41h operand)
sbl_direct:   db 0              ;   1 = DIRECT (SPEC.md 34.5.2): the card
                                ;   plays the ring in the pool, no double
                                ;   buffer and no refill task
sbl_ext:      db 0              ;   1 = EXTERNAL ring (SPEC.md 34.5.3). MUST
                                ;   follow sbl_direct: verb 0 clears the two
                                ;   with one word store
sbl_adpcm:    db 0              ;   1 = ADPCM4: 7Dh starts it
sbl_oflags:   db 0              ;   verb 0's AH - a TEMP of the same kind:
                                ;   bit 0 (SND_OPENF_RING) is committed into
                                ;   sbl_ring_on only with the rest of the
                                ;   record
sbl_gr_act:   times SBL_GMAX dw 0   ; grants in use (occupancy IS these)
sbl_ze:                         ; --- end of the attach-time clear -----------
%if sbl_ext - sbl_direct != 1 || sbl_hisp - sbl_wide != 1
  %error "sbl_v_open clears sbl_direct/sbl_ext and sbl_wide/sbl_hisp as words"
%endif

sbl_oldvec:   dw 0, 0           ; the winner's saved vector (off, seg)
sbl_dsc_mask: db 0              ; discovery: saved 8259 mask
sbl_rseg:     dw 0              ;   the ring's segment for sbl_ring_phys
sbl_extctl:   dw 0, 0           ;   external: the control words (off, seg)
sbl_xpage:    db 0              ;   the auto-init DMA cycle hw_start
sbl_xoff:     dw 0              ;   programs: page, offset in it, and
sbl_xcnt:     dw 0              ;   length - 1 (verb 0 sets all three)

; --- where our DMA buffer landed (SPEC.md 51.4) ------------------------------
; It was a pinned 64KB segment whose page and offset were constants. A
; driver's is a heap claim, so all three are computed by sbl_dma_get whenever
; it is claimed, and read by every 8237 programming site.
sbl_seg:      dw 0              ; the double buffer and record ring, page-
                                ;   safe, held only while a stream that is
                                ;   NOT direct is open (SPEC.md 34.5.2)
sbl_poolseg:  dw 0              ; the STAGING POOL's own claim, 0 = not held.
                                ;   Taken on the first grant, released with
                                ;   the last (sbl_pool_get/put)
sbl_page:     db 0              ; the 8237 page it landed in (address bits
                                ; 16..19 - the XT's 8237 has no register for
                                ; them, which is what 83h is)
sbl_dmaoff:   dw 0              ; ...and its offset inside that page
sbl_poolsz:   dw 0              ; staging pool bytes actually claimed
sbl_up:       db 0              ; 1 = attached: a card was found and hooked
sbl_wtask:    db 0              ; 1 = a refill/drain worker is on the run
                                ; queue. Detach spins on this, because the
                                ; kernel frees our image the moment detach
                                ; returns (SPEC.md 51.7)
sbl_clip:     db 0              ; 1 = the kernel says a PCM_EXCL clip owns
                                ; the machine (SPEC.md 34.4). The kernel
                                ; arbitrates BOTH directions of the
                                ; exclusion, because it is the only side
                                ; that can see both tiers

sbl_gr_off:   times SBL_GMAX dw 0     ; staging grants (SPEC.md 34.6): offset,
sbl_gr_size:  times SBL_GMAX dw 0     ;   size in bytes,
sbl_gr_inst:  times SBL_GMAX dw 0     ;   owning instance (low byte)
%endmacro
