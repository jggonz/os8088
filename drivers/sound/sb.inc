; =============================================================================
; os8088 - sndsb.inc
;
; The Sound Blaster driver (SPEC.md 34.5/34.6, Phase 4 of docs/SOUND-PLAN.md):
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
SBL_POOLKB   equ 20             ; the staging pool, a SEPARATE claim taken on
                                ; the first grant and released with the last
                                ; (SPEC.md 51.4). Grant offsets are relative
                                ; to ITS base, not the DMA buffer's. This is
                                ; the FIRST TIER asked for and not a fixed
                                ; size - sbl_pool_get walks down by
                                ; SBL_POOLSTEP when the heap will not fund it
SBL_POOLSTEP equ 4              ; the tier step, in KB. It must DIVIDE
                                ; SBL_POOLKB, so the walk lands on the floor
                                ; exactly rather than stepping past it
SBL_POOLMIN  equ 4              ; the smallest pool worth holding: below this
                                ; nothing in the tree asks for a grant that
                                ; would fit (sbtest's smallest is 2,400 B)
%if SBL_POOLKB % SBL_POOLSTEP
  %error "SBL_POOLSTEP must divide SBL_POOLKB"
%endif
%if SBL_POOLMIN % SBL_POOLSTEP
  %error "SBL_POOLSTEP must divide SBL_POOLMIN"
%endif
SBL_POOLMAX  equ 0xD000         ; the biggest grant the offset arithmetic can
                                ; express. The live bound is [sbl_poolsz],
                                ; which is 0 until the pool is claimed
SBL_GMAX     equ 8              ; staging-grant records
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

SBL_ST_PLAY  equ 0              ; stream states (verb 3, SPEC.md 20.3):
                                ;   out = playing, in = recording
SBL_ST_UNDER equ 1              ;   out = underrun-paused (resumable by
                                ;   feed); in = paused (grant capacity
                                ;   full, or the drain task starved)
SBL_ST_END   equ 2              ;   stopped by the snd_tick watchdog

SBL_POLLS    equ 4096           ; DSP handshake poll budget (bounded even
                                ; from ISR context: ~4-8ms of ISA reads)

; --- detection tables (.text data - far code reads them through DS) ----------
sbl_bases:                      ; the pinned scan order (SPEC.md 34.2)
    dw 0x220, 0x240, 0x210, 0x230, 0x250, 0x260, 0

sbl_dsc_irqn: db 7, 5, 3, 2     ; IRQ candidates, pinned order (SPEC.md 34.5)
sbl_dsc_mbit: db 0x80, 0x20, 0x08, 0x04     ; 8259 mask bits
sbl_dsc_vec:  dw 0x3C, 0x34, 0x2C, 0x28     ; IVT byte offsets (int 0Fh/0Dh/
                                            ; 0Bh/0Ah)
sbl_dsc_stub: dw sbl_stub7, sbl_stub5, sbl_stub3, sbl_stub2

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
    add dx, [sbl_dmaoff]        ; ...and this is where that buffer sits in
                                ; its 8237 page. It was a constant 0 while
                                ; the buffer was a pinned segment; a driver's
                                ; is a heap claim, so the page and the offset
                                ; within it are both computed at attach
                                ; (sbl_dma_map). Nothing is doubled - address
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
    mov al, 0x49                ; single mode, read (memory -> card), ch 1
    out 0x0B, al
    mov al, [sbl_page]          ; the 8237 page our claim landed in
    out 0x83, al
    mov al, 0x01
    out 0x0A, al                ; unmask channel 1
    popf
    mov al, 0x14                ; DSP: 8-bit single-cycle DMA output
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
; sbl_arm_rec - program a single-cycle capture of one ring half (DSP < 2.00)
;
; in:       AL = half (0/1); rate already set via 40h
; out:      nothing
; clobbers: nothing (flags)
;
; The input mirror of sbl_arm_half: DMA mode 45h (single, write - card to
; memory), the half's ring offset (SBL_RING + half*4096, page 3), then DSP
; 24h (8-bit single-cycle ADC) + length. Called from verb-4 start and from
; sbl_isr's input re-arm branch (SPEC.md 34.6).
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
    add dx, [sbl_dmaoff]        ; ...plus where our buffer sits in its page
    pushf
    cli
    mov al, 0x05
    out 0x0A, al                ; mask channel 1
    out 0x0C, al                ; clear the flip-flop (any value)
    mov al, dl
    out 0x02, al                ; address lo (0)
    mov al, dh
    out 0x02, al                ; address hi
    out 0x0C, al                ; clear the flip-flop again
    mov al, (SBL_RHALF-1) & 0xFF
    out 0x03, al                ; count lo (len - 1)
    mov al, (SBL_RHALF-1) >> 8
    out 0x03, al                ; count hi
    mov al, 0x45                ; single mode, write (card -> memory), ch 1
    out 0x0B, al
    mov al, [sbl_page]          ; the 8237 page our claim landed in
    out 0x83, al
    mov al, 0x01
    out 0x0A, al                ; unmask channel 1
    popf
    mov al, 0x24                ; DSP: 8-bit single-cycle DMA input
    call sbl_dsp_wr
    mov al, (SBL_RHALF-1) & 0xFF
    call sbl_dsp_wr
    mov al, (SBL_RHALF-1) >> 8
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
; programs the DMA once for the whole 4KB ring (59h) plus the DSP block
; size (48h, 2KB) and 1Ch; single-cycle arms half 0 (SPEC.md 34.5).
; -----------------------------------------------------------------------------
sbl_hw_start:
    push ax
    push bx
    cmp byte [sbl_wide], 0      ; the wide-rate regime (SPEC.md 34.5, DSP
    je .tc                      ; >= 4.00): 41h takes the rate in Hz,
    mov al, 0x41                ; big-endian, in place of the TC
    call sbl_dsp_wr
    mov al, [sbl_rate+1]
    call sbl_dsp_wr
    mov al, [sbl_rate]
    call sbl_dsp_wr
    jmp .spk
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
    mov bx, [sbl_half]          ; BX = DMA count = 2*half - 1
    shl bx, 1
    dec bx
    pushf
    cli
    mov al, 0x05
    out 0x0A, al                ; mask channel 1
    out 0x0C, al                ; flip-flop clear
    mov al, [sbl_dmaoff]        ; address = our buffer's offset in its page
    out 0x02, al
    mov al, [sbl_dmaoff+1]
    out 0x02, al
    out 0x0C, al                ; flip-flop clear
    mov al, bl
    out 0x03, al                ; count = the whole ring - 1
    mov al, bh
    out 0x03, al
    mov al, 0x59                ; auto-init, read, channel 1
    out 0x0B, al
    mov al, [sbl_page]
    out 0x83, al
    mov al, 0x01
    out 0x0A, al                ; unmask
    popf
    mov bx, [sbl_half]          ; BX = DSP block length - 1
    dec bx
    cmp byte [sbl_wide], 0
    je .legacy
    mov al, 0xC6                ; wide start: C6h, mode 00h (8-bit mono
    call sbl_dsp_wr             ; unsigned), block length - 1 lo/hi -
    xor al, al                  ; halt/continue/DAh-exit are unchanged,
    call sbl_dsp_wr             ; so close and resume need no second path
    mov al, bl
    call sbl_dsp_wr
    mov al, bh
    call sbl_dsp_wr
    jmp .out
.legacy:
    mov al, 0x48                ; DSP block size = one half
    call sbl_dsp_wr
    mov al, bl
    call sbl_dsp_wr
    mov al, bh
    call sbl_dsp_wr
    mov al, 0x1C                ; auto-init 8-bit DMA output
    call sbl_dsp_wr
    jmp .out
.single:
    mov al, 0
    call sbl_arm_half           ; half 0 (always 2KB: a DSP < 2.00 can
                                ; never open wide); the ISR re-arms
.out:
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
    push bx                     ; the ring's DMA address is computed now
    mov al, 0xD3                ; speaker (DAC) off for the capture path
    call sbl_dsp_wr
    mov al, 0x40                ; time constant
    call sbl_dsp_wr
    mov al, [sbl_tc]
    call sbl_dsp_wr
    cmp byte [sbl_auto], 0
    je .single
    pushf
    cli
    mov al, 0x05
    out 0x0A, al                ; mask channel 1
    out 0x0C, al                ; flip-flop clear
    mov bx, [sbl_dmaoff]        ; address = the ring, in its 8237 page
    add bx, SBL_RING
    mov al, bl
    out 0x02, al
    mov al, bh
    out 0x02, al
    out 0x0C, al                ; flip-flop clear
    mov al, (2*SBL_RHALF-1) & 0xFF
    out 0x03, al                ; count = the whole ring - 1
    mov al, (2*SBL_RHALF-1) >> 8
    out 0x03, al
    mov al, 0x55                ; auto-init, write (card -> memory), ch 1
    out 0x0B, al
    mov al, [sbl_page]
    out 0x83, al
    mov al, 0x01
    out 0x0A, al                ; unmask
    popf
    mov al, 0x48                ; DSP block size = one ring half
    call sbl_dsp_wr
    mov al, (SBL_RHALF-1) & 0xFF
    call sbl_dsp_wr
    mov al, (SBL_RHALF-1) >> 8
    call sbl_dsp_wr
    mov al, 0x2C                ; auto-init 8-bit DMA input
    call sbl_dsp_wr
    jmp .out
.single:
    mov al, 0
    call sbl_arm_rec            ; half 0; the ISR re-arms from there
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sbl_halt - pause DSP DMA requests (D0h, bounded write-poll)
; in/out: nothing; clobbers nothing (flags)
; -----------------------------------------------------------------------------
sbl_halt:
    push ax
    mov al, 0xD0
    call sbl_dsp_wr
    pop ax
    ret

; -----------------------------------------------------------------------------
; sbl_go_on - continue a paused stream: D4h on auto-init hardware, a fresh
;             half arm on single-cycle (nothing was in flight there)
; in:  [sbl_play] = the half to sound (or capture into) next; [sbl_str_dir]
; out: nothing; clobbers nothing (flags)
; -----------------------------------------------------------------------------
sbl_go_on:
    push ax
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
    cmp byte [sbl_str_dir], 0   ; input streams take the mirror half-swap
    jne .input

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

    mov ax, [sbl_wd_rst]        ; the block IRQ arrived: rewind the watchdog
    mov [sbl_wd], ax

    cmp byte [sbl_valid+bx], 0  ; was the next half ever refilled?
    je .under
    cmp byte [sbl_auto], 0      ; yes: auto-init needs nothing; single-cycle
    jne .eoi                    ; re-arms for the half now sounding
    mov al, bl
    call sbl_arm_half
    jmp .eoi

.under:                         ; the underrun contract (SPEC.md 34.5)
    cmp byte [sbl_auto], 0
    je .nohalt                  ; single-cycle: nothing in flight to halt
    call sbl_halt               ; D0h, bounded write-poll
.nohalt:
    mov byte [sbl_paused], 1
    mov byte [sbl_str_state], SBL_ST_UNDER
    mov byte [sbl_expect], 0
    inc word [sbl_under]
    jmp .eoi

.input:                         ; the mirror half-swap (SPEC.md 34.6): the
    mov bl, [sbl_play]          ; half that just finished capturing...
    xor bh, bh
    mov byte [sbl_valid+bx], 1  ; ...holds data awaiting the drain task
    xor bl, 1
    mov [sbl_play], bl          ; the DSP captures into the other half now
    mov ax, [sbl_wd_rst]        ; the block IRQ arrived: rewind the watchdog
    mov [sbl_wd], ax
    cmp byte [sbl_valid+bx], 0  ; is the new capture half drained yet?
    je .inok
    cmp byte [sbl_auto], 0      ; no - the drain starved: pause input
    je .inoh                    ; rather than overwrite un-drained data
    call sbl_halt               ; (bounded loss + a visible status, the
.inoh:                          ; underrun contract's input mirror)
    mov byte [sbl_paused], 1
    mov byte [sbl_str_state], SBL_ST_UNDER
    mov byte [sbl_expect], 0
    inc word [sbl_under]
    jmp .eoi
.inok:
    cmp byte [sbl_auto], 0      ; auto-init needs nothing; single-cycle
    jne .eoi                    ; re-arms the half now capturing
    mov al, bl
    call sbl_arm_rec

.eoi:
    mov al, 0x20                ; own EOI - nothing chains behind us
    out 0x20, al
    jmp .done
.spur:
    cmp byte [sbl_irq], 7       ; line 7: real or spurious? only the 8259
    jne .seoi                   ; knows - a REAL IRQ 7 (the SB's factory
    mov al, 0x0B                ; default line) sets ISR bit 7. OCW3: the
    out 0x20, al                ; next read of 0x20 returns the ISR
    in al, 0x20
    test al, 0x80               ; bit 7 clear = truly spurious: no EOI
    jz .done                    ; (SPEC.md 34.5, binding)
.seoi:
    mov al, 0x20                ; a real stray, any line: EOI or it wedges
    out 0x20, al
.done:
    pop es
    pop ds
    pop dx
    pop bx
    pop ax
    iret

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
    push si                     ; SI is only an output on success: the
                                ; refusals restore the caller's
    or cx, cx                   ; not jcxz: .e7 is out of a short jump's
    jnz .szok                   ; reach now that the pool claim sits here
    jmp .e7
.szok:
    call sbl_pool_get           ; BEFORE the size test: [sbl_poolsz] is 0
    jc .e8n                     ; until the pool is actually held
    cmp cx, [sbl_poolsz]        ; the pool we actually got, not a constant
    ja .e7
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
    push dx
    mov dx, [sbl_poolsz]
    cmp ax, dx                  ; ...or off the top of the POOL we claimed
    pop dx
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
; The stream surface: slot 0x0100's live body (SPEC.md 20.3/34.5/34.6).
; osapi_snd_stream (snd.inc) tail-jumps here. AL = verb, AH = handle or
; sub-op; every verb preserves all registers except its documented outputs.
; =============================================================================
sbl_stream_op:
    cmp byte [sbl_up], 0        ; no card attached: nothing can stream
    je .e4
    cmp al, 0
    je sbl_v_open
    cmp al, 1
    je sbl_v_feed
    cmp al, 2
    je sbl_v_close
    cmp al, 3
    je sbl_v_status
    cmp al, 4
    je sbl_v_open_in
    cmp al, 5
    je sbl_v_read
    cmp al, 6
    je sbl_v_stage
    cmp al, 7
    je sbl_v_grant
    cmp al, 8
    je sbl_v_busy
    mov ax, 7                   ; anything higher: bad verb
    stc
    ret
.e4:
    mov ax, 4
    stc
    ret

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
    mov byte [sbl_ropen], 0     ; latch the ring flag before AH dies (a
    test ah, 1                  ; TEMP, not the live record byte: the busy
    jz .lin                     ; refusal below must not disturb an OPEN
    mov byte [sbl_ropen], 1     ; stream's [sbl_ring_on])
.lin:
    cmp byte [sbl_up], 0
    jz .e4                      ; no card: the documented clean failure
    cmp byte [sbl_str_act], 0
    jne .e1                     ; one stream per card (SPEC.md 34.3)
    cmp byte [sbl_clip], 0
    jne .e1                     ; an exclusive clip holds the machine
    cmp bx, 4000                ; TC = 256 - 1e6/rate must fit a byte. THE
    jb .e2                      ; RATE ARRIVES IN BX, not DX: DH carries the
    mov word [sbl_hopen], SBL_HALF  ; requesting instance (SPEC.md 34.3) and
    cmp bx, 22222               ; would otherwise be the rate's top half, so
    jbe .ratok                  ; the kernel banks it for us on the way in
    cmp bx, 44100               ; the wide-rate regime (SPEC.md 34.5):
    ja .e2                      ; 22,223..44,100 on a DSP >= 4.00 only -
    cmp byte [sbl_verhi], 4     ; err 2 below that, never a clamp
    jb .e2
    mov word [sbl_hopen], 2*SBL_HALF
.ratok:
    or cx, cx                   ; nothing staged at all? (8086 conditional
    jnz .args                   ; jumps are short-only: .e7 sits past reach,
    jmp .e7                     ; so the refusals trampoline)
.args:
    call sbl_grant_chk          ; BX is already the rate (see above)          ; the range must sit in the caller's grant;
    jnc .grantok                ; out AX = bytes to the grant's end
    jmp .e7
.grantok:
    mov [sbl_str_max], ax       ; the feed bound (linear; ring uses the mask)
    mov [sbl_str_inst], dh
    cmp byte [sbl_ropen], 0     ; ring validation (SPEC.md 20.3 verb 0 /
    je .irqchk                  ; 34.5): RL = AX = bytes SI..grant end
    cmp ax, 4096                ; power of two, 4096..32768...
    jb .e7t
    cmp ax, 32768
    ja .e7t
    mov dx, ax
    dec dx                      ; DX = candidate mask
    test ax, dx
    jnz .e7t                    ; not a power of two
    cmp cx, ax                  ; ...and one half <= CX <= RL: a ring never
    ja .e7t                     ; pads, so the DSP may not start on a half
    cmp cx, [sbl_hopen]         ; no fill can complete (SPEC.md 34.5 rule 3;
    jae .rok                    ; the WIDE half when the rate widened it)
.e7t:
    jmp .e7
.rok:
    mov [sbl_rmask], dx         ; RL-1: the ring mask
.irqchk:
    cmp byte [sbl_irq], 0xFF    ; IRQ discovery, deferred to first use
    jne .irqok                  ; (SPEC.md 34.5)
    call sbl_f_irqdisc           ; far orchestration; hooks the winner
    or al, al
    jz .e4                      ; no candidate confirmed: no usable card
.irqok:
    mov ax, [sbl_hopen]         ; commit the half width (safe pre-publish:
    mov [sbl_half], ax          ; sbl_str_act == 0 held at .e1, so nothing
    mov byte [sbl_wide], 0      ; live reads these)
    cmp ax, SBL_HALF
    je .tcrate
    mov byte [sbl_wide], 1      ; wide (SPEC.md 34.5): 41h wants the rate
    mov [sbl_rate], bx          ; in Hz, and the watchdog spans the wide
    mov ax, SBL_WD_WIDE & 0xFFFF    ; blocks: (2 * 2*SBL_HALF * 18.2)/rate
    mov dx, SBL_WD_WIDE >> 16       ; + 3, derived (149,094 at 2KB halves)
    div bx
    add ax, 3
    jmp .wdset
.tcrate:
    mov ax, 0x4240              ; TC = 256 - 1,000,000/rate (SPEC.md 34.5)
    mov dx, 0x000F
    div bx                      ; AX = 1e6/rate (45..250 by the range check)
    neg al                      ; AL = 256 - AX (mod 256)
    mov [sbl_tc], al
    mov ax, SBL_WD_OUT & 0xFFFF ; watchdog: 2 block periods in ticks + 3 =
    mov dx, SBL_WD_OUT >> 16    ; (2*SBL_HALF*18.2)/rate + 3, derived
    div bx
    add ax, 3
.wdset:
    mov [sbl_wd_rst], ax
    mov [sbl_wd], ax
    mov [sbl_str_off], si       ; the stream record (not yet published)
    mov [sbl_total], cx
    mov word [sbl_fed], 0
    mov word [sbl_consumed], 0
    mov byte [sbl_play], 0
    mov byte [sbl_valid], 0
    mov byte [sbl_valid+1], 0
    mov byte [sbl_paused], 0
    mov byte [sbl_str_dir], 0   ; an OUTPUT stream (SPEC.md 34.6)
    mov al, [sbl_ropen]         ; commit the ring flag into the record
    mov [sbl_ring_on], al       ; (SPEC.md 34.5; sbl_rmask was set above)
    mov byte [sbl_str_state], SBL_ST_PLAY
    inc byte [sbl_gen]
    xor bx, bx                  ; prime the double buffer: half 0 always
    call sbl_fill_half          ; has data (ring: CX >= SBL_HALF validated;
    mov byte [sbl_valid], 1     ; linear: CX > 0, short halves pad)...
    mov ax, [sbl_total]
    sub ax, [sbl_fed]
    cmp byte [sbl_ring_on], 0
    je .lin1
    cmp ax, [sbl_half]          ; ...half 1: ring fills are whole halves
    jb .primed                  ; only, never padded (SPEC.md 34.5 rule 1)
    jmp .fill1
.lin1:
    or ax, ax
    jz .primed                  ; ...linear: any remainder fills (+ pad)
.fill1:
    mov bx, 1
    call sbl_fill_half
    mov byte [sbl_valid+1], 1
.primed:
    mov byte [sbl_str_act], 1   ; publish, then pace: the DRIVER owns pacing
    mov dh, [sbl_gen]           ; now (SPEC.md 51.7). DH reaches the worker's
    mov ax, sbl_refill_task     ; own DH - per-task, so a close-and-reopen
    call OSAPI_DRV_TASK         ; between here and its first instruction
                                ; cannot leave two workers alive. It runs in
                                ; this segment with T_INST = 0xFF, so it
                                ; bills to System: a driver has no instance
    jc .nospawn
    mov byte [sbl_expect], 1 ; arm BEFORE the hardware starts (the
    call sbl_hw_start           ; sbl_refill resume order): if the opener
                                ; is preempted right after 1Ch and stays
                                ; off-CPU a whole block period, the first
                                ; block IRQ must not ride the spurious
                                ; path - unconfirmed via 2xEh, the DSP
                                ; would keep its INT latched and the edge-
                                ; triggered line could never re-fire.
                                ; Arming early is safe: sbl_wd is already
                                ; loaded, so sbl_tick cannot underflow it
    mov ah, [sbl_gen]           ; out: AH = handle, AL = 0
    mov al, 0
    clc
    jmp .out
.nospawn:
    mov byte [sbl_str_act], 0   ; unwind: nothing started
    mov ax, 6                   ; the documented clean pool-full error
    jmp .err
.e1:
    mov ax, 1
    jmp .err
.e2:
    mov ax, 2
    jmp .err
.e4:
    mov ax, 4
    jmp .err
.e7:
    mov ax, 7
.err:
    stc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    ret

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
    sub ax, [sbl_fed]           ; outstanding bytes after this feed
    jz .store                   ; nothing outstanding is trivially fine
    dec ax
    cmp ax, [sbl_rmask]         ; <= RL  <=>  (outstanding - 1) <= RL-1
    ja .e7
.store:
    mov [sbl_total], cx
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
; -----------------------------------------------------------------------------
sbl_stop_stream:
    push ax
    push dx
    pushf
    cli
    call sbl_halt               ; D0h, bounded
    cmp byte [sbl_auto], 0
    je .noda
    mov al, 0xDA                ; exit auto-init at the block edge
    call sbl_dsp_wr
.noda:
    mov al, 0xD3                ; speaker (DAC) off
    call sbl_dsp_wr
    mov dx, [sbl_base]
    add dx, 0x0E
    in al, dx                   ; retire a latched block IRQ SB-side
    mov byte [sbl_expect], 0
    mov byte [sbl_str_act], 0   ; handles are stale from here
    popf
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
    cmp byte [sbl_up], 0
    jz .e4                      ; no card: the documented clean failure
    cmp byte [sbl_str_act], 0
    jne .e1                     ; half-duplex: one stream record, either
                                ; direction (SPEC.md 34.3/34.6)
    cmp byte [sbl_clip], 0
    jne .e1                     ; an exclusive clip holds the machine
    cmp bx, 4000                ; the rate is in BX here too (see sbl_v_open)
    jb .e2
    mov ax, 13000               ; ...and the INPUT ceiling is honoured:
    cmp byte [sbl_auto], 0      ; 13k on 1.x, 15k on >= 2.00 normal mode
    je .ceil                    ; (SPEC.md 34.5) - err 2, never a clamp
    mov ax, 15000
.ceil:
    cmp bx, ax
    ja .e2
    or cx, cx                   ; zero capacity captures nothing (8086
    jnz .args                   ; conditionals are short-only: trampoline)
    jmp .e7
.args:
    call sbl_grant_chk          ; BX is already the rate (see above)          ; the capture range must sit in the caller's
    jnc .grantok                ; grant; out AX = bytes to the grant's end
    jmp .e7
.grantok:
    mov [sbl_str_max], ax
    mov [sbl_str_inst], dh
    cmp byte [sbl_irq], 0xFF    ; IRQ discovery, deferred to first use
    jne .irqok                  ; (SPEC.md 34.5)
    call sbl_f_irqdisc           ; far orchestration; hooks the winner
    or al, al
    jz .e4                      ; no candidate confirmed: no usable card
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
    mov [sbl_str_off], si       ; the stream record (not yet published)
    mov [sbl_total], cx         ; = the capture capacity (SPEC.md 34.6)
    mov word [sbl_fed], 0
    mov word [sbl_consumed], 0  ; = bytes drained into the grant
    mov byte [sbl_play], 0      ; the half the DSP captures into
    mov byte [sbl_valid], 0     ; input sense: 1 = captured, awaiting drain
    mov byte [sbl_valid+1], 0
    mov byte [sbl_drain_h], 0   ; the next half the drain task empties
    mov byte [sbl_paused], 0
    mov byte [sbl_str_dir], 1   ; an INPUT stream (SPEC.md 34.6)
    mov byte [sbl_ring_on], 0   ; input has no ring mode (SPEC.md 34.5)
    mov byte [sbl_str_state], SBL_ST_PLAY
    inc byte [sbl_gen]
    mov byte [sbl_str_act], 1   ; publish, then pace: the DRIVER owns pacing
    mov dh, [sbl_gen]           ; (SPEC.md 51.7), as for playback above
    mov ax, sbl_drain_task
    call OSAPI_DRV_TASK
    jc .nospawn
    mov byte [sbl_expect], 1 ; arm BEFORE the hardware starts - the
    call sbl_hw_start_in        ; sbl_v_open first-IRQ order, same race
    mov ah, [sbl_gen]           ; out: AH = handle, AL = 0
    mov al, 0
    clc
    jmp .out
.nospawn:
    mov byte [sbl_str_act], 0   ; unwind: nothing started
    mov ax, 6                   ; the documented clean pool-full error
    jmp .err
.e1:
    mov ax, 1
    jmp .err
.e2:
    mov ax, 2
    jmp .err
.e4:
    mov ax, 4
    jmp .err
.e7:
    mov ax, 7
.err:
    stc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sbl_v_read - verb 5: copy recorded (or staged) bytes out of a grant
;
; in:       SI = source offset in SND_SEG (inside a grant the caller owns),
;           CX = bytes, DI = destination offset in the CALLER's segment
;           (BX, threaded per-call by slot 0x0100's wrapper - SPEC.md 20.3)
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
    rep movsb                   ; pool:SI -> caller DI
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
;           per-call by slot 0x0100's wrapper - SPEC.md 20.3), CX = bytes,
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
    jcxz .ok                    ; nothing to copy is trivially fine
    push si
    mov si, di                  ; range-check the DESTINATION
    call sbl_grant_chk
    pop si
    jc .e7
    cld
.copy:                          ; chunked exactly as sbl_v_read: the pool is
    jcxz .ok                    ; movable and this is the caller's task
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
    rep movsb                   ; the source pointer lives THERE since v3
    pop ds                      ; callerseg:SI -> pool:DI
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
; sbl_fill_half - copy the next stretch of grant data into one buffer half
;
; in:       BX = half (0/1); [sbl_str_off]/[sbl_fed]/[sbl_total] current
; out:      nothing; [sbl_fed] advanced; linear: the half's tail is 0x80
;           silence when the data ran short. Ring ([sbl_ring_on]): exactly
;           one WHOLE half from physical offset str_off + (fed & rmask) -
;           the CALLER must have checked total - fed >= SBL_HALF, because
;           a ring fill never pads (SPEC.md 34.5 rule 1)
; clobbers: nothing (flags)
;
; OPEN-PRIMING ONLY since the refill path moved to sbl_fill_guard below:
; this un-guarded body survives solely under sbl_v_open, where the record
; is not yet published (act = 0, gen already bumped - the guard's own
; act+gen check would refuse the priming fills) and the UI-task pinning of
; verb 0 excludes every concurrent open.
;
; Both source and destination live in SND_SEG, so the copy runs with
; DS = ES = SND_SEG for the movs; kernel state is only touched before the
; switch. The caller publishes the half's valid flag afterwards. The ring
; copy needs no seam split: fed stays 2048-aligned and RL is a power of
; two >= 4096, so (fed & rmask) is 2048-aligned and the half ends at or
; before the ring's end (SPEC.md 34.5).
; -----------------------------------------------------------------------------
sbl_fill_half:
    push ax
    push cx
    push dx
    push si
    push di
    push es
    mov ax, [sbl_seg]
    mov es, ax
    xor di, di                  ; DI = half * [sbl_half]: the destination.
    or bx, bx                   ; [sbl_half] ALREADY carries the wide case
    jz .dsh                     ; (verb 0 committed it), so this needs no
    mov di, [sbl_half]          ; second test and no hand-counted shift
.dsh:
    cmp byte [sbl_ring_on], 0
    jne .ring
    mov si, [sbl_str_off]       ; SI = grant + progress: the source
    add si, [sbl_fed]
    mov cx, [sbl_total]
    sub cx, [sbl_fed]           ; CX = data remaining
    cmp cx, [sbl_half]
    jbe .n
    mov cx, [sbl_half]
.n:
    mov dx, [sbl_half]          ; DX = silence pad after the data
    sub dx, cx
    add [sbl_fed], cx
    jmp .copy
.ring:
    mov si, [sbl_fed]           ; physical = str_off + (fed & (RL-1))
    and si, [sbl_rmask]
    add si, [sbl_str_off]
    mov cx, [sbl_half]          ; a whole half, guaranteed by the caller
    add [sbl_fed], cx
    xor dx, dx                  ; and never a pad (SPEC.md 34.5 rule 1)
.copy:
    cld
.chunk:                         ; chunked like every other pool copy (SPEC.md
    jcxz .pad                   ; 66.5.5): the pool is movable, and the
    mov ax, SBL_DCHUNK          ; segment is re-derived per chunk under IF=0
    cmp cx, ax
    jae .csz
    mov ax, cx
.csz:
    sub cx, ax
    pushf
    cli
    push cx
    mov cx, ax
    push ds
    mov ds, [sbl_poolseg]       ; DS = the POOL: the grant is there now, the
    rep movsb                   ; destination half is in the DMA claim (ES)
    pop ds
    pop cx
    popf
    jmp .chunk
.pad:
    mov cx, dx
    jcxz .done
    mov al, 0x80                ; 8-bit unsigned silence
    rep stosb
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sbl_fill_guard - sbl_refill's fill: one buffer half, act+gen-guarded
;
; in:       BX = half (0/1), DH = the stream generation this task serves;
;           [sbl_str_off]/[sbl_fed]/[sbl_total] current
; out:      nothing; on success [sbl_fed] is advanced and the half's valid
;           flag published (linear: 0x80 tail pad; ring: one whole half,
;           never padded - SPEC.md 34.5 rule 1)
; clobbers: nothing (flags)
;
; sbl_fill_half with sbl_drain_half's teardown rule bolted on. The refill
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
    mov ax, [sbl_seg]
    mov es, ax
    xor di, di                  ; DI = half * [sbl_half]: the destination
    or bx, bx                   ; (the sbl_fill_half derivation - [sbl_half]
    jz .dsh                     ; already carries the wide case)
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
    jmp .claimed
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
    rep movsb                   ; destination half is in the DMA claim (ES)
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
.bail:                          ; (first-window refusals fall out the same
.dead:                          ; way: nothing claimed, nothing published)
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
    cmp byte [sbl_str_act], 1   ; re-verify under IF=0: closed since the
    jne .skip                   ; checks above?
    cmp dh, [sbl_gen]           ; superseded by a newer open?
    jne .skip
    mov byte [sbl_paused], 0
    mov byte [sbl_str_state], SBL_ST_PLAY
    mov ax, [sbl_wd_rst]
    mov [sbl_wd], ax
    mov byte [sbl_expect], 1
    call sbl_go_on              ; D4h / re-arm (SPEC.md 34.5)
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
    mov byte [sbl_wtask], 1     ; detach waits on this (SPEC.md 51.7): our
.loop:
    call OSAPI_TASK_PARK        ; ...and the heap compactor waits on THIS
                                ; (SPEC.md 66.5.5). Here, at the top of the
                                ; outer loop, is the one point in this task
                                ; that provably holds no pointer into the
                                ; pool: sbl_refill loads [sbl_poolseg] itself
                                ; and returns before we come back round.
                                ; Without the call no claim this driver owns
                                ; could ever be compacted
    cmp byte [sbl_str_act], 1   ; closed / torn down?
    jne .die
    mov al, [sbl_gen]           ; superseded by a newer stream?
    cmp al, dh
    jne .die
    cmp byte [sbl_str_state], SBL_ST_END
    je .die                     ; watchdog-stopped: nothing left to pace
    call sbl_refill
    mov ax, 1                   ; a block is >= 1.7 ticks at the 22kHz
    call OSAPI_TASK_SLEEP             ; ceiling: one-tick pacing always keeps up
    jmp .loop
.die:
    mov byte [sbl_wtask], 0     ; ...and clears it as its last act but one
    xor ax, ax                  ; AX = 0: this worker is exiting (SPEC.md
    call OSAPI_DRV_TASK         ; 51.7). NEVER RETURNS

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
; Both source and destination live in SND_SEG (sbl_fill_half's trick in
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
; makes even the read-only output copy mutate the successor's [sbl_fed];
; the un-guarded sbl_fill_half survives only under sbl_v_open's priming,
; where act is not yet published.)
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
    rep movsb                   ; one chunk, atomic vs. teardown (IF=0)
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
    cmp byte [sbl_str_act], 1   ; re-verify under IF=0 (the sbl_refill
    jne .fskip                  ; rule): closed since the checks above?
    cmp dh, [sbl_gen]           ; superseded by a newer open?
    jne .fskip
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
    cmp byte [sbl_str_act], 1   ; the same re-verification under IF=0
    jne .rskip
    cmp dh, [sbl_gen]
    jne .rskip
    mov byte [sbl_paused], 0
    mov byte [sbl_str_state], SBL_ST_PLAY
    mov ax, [sbl_wd_rst]
    mov [sbl_wd], ax
    mov byte [sbl_expect], 1
    call sbl_go_on              ; D4h / re-arm (input-aware)
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
    mov byte [sbl_wtask], 1     ; detach waits on this (SPEC.md 51.7): our
.loop:
    cmp byte [sbl_str_act], 1   ; closed / torn down?
    jne .die
    mov al, [sbl_gen]           ; superseded by a newer stream?
    cmp al, dh
    jne .die
    cmp byte [sbl_str_state], SBL_ST_END
    je .die                     ; watchdog-stopped: nothing left to drain
    call sbl_drain
    mov ax, 1
    call OSAPI_TASK_SLEEP
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

SBL_WANT   equ SBL_DMASZ / 1024 ; KB claimed at attach: the DMA-visible part
                                ; and NOTHING ELSE. The 20KB staging pool used
                                ; to be claimed with it and held for the
                                ; session; it is its own claim now, taken when
                                ; an app first asks for a grant. On a 128KB
                                ; machine that is 20KB of a 55KB heap that a
                                ; card nobody is playing no longer holds

; -----------------------------------------------------------------------------
; sbl_attach - find a card and give it somewhere to DMA from
; in:  nothing
; out: CF = 0 attached; CF = 1 and AL = DRVE_HW (no card) or DRVE_MEM (no
;      page-safe memory) - and in either case NOTHING is hooked and NOTHING is
;      held (SPEC.md 51.2)
; clobbers: AX (flags)
;
; The two refusals are told apart because the Control Panel reports them: on a
; tier switch "you have no card" and "close something and try again" are
; different answers, and a page that said only "no" for both would send the
; user looking for the wrong problem (SPEC.md 34.8).
; -----------------------------------------------------------------------------
sbl_attach:
    push bx
    push cx
    push dx
    cmp word [sbl_seg], 0
    jne .twice                  ; ALREADY ATTACHED, so somebody attached twice
                                ; and that is a bug in the CALLER - not a fact
                                ; about the machine, and not something to
                                ; recover from quietly. Refuse before the state
                                ; reset and before the first port write, so the
                                ; live ring, the hooked vector and the running
                                ; DSP are all exactly as they were
    call sbl_state_init         ; before any port write: a reload starts clean
    call sbl_f_probe            ; the reset scan + E1h version gate
    or al, al
    jz .nohw
    call sbl_dma_map            ; ...and somewhere to DMA from
    jc .nomem
    mov byte [sbl_up], 1        ; publish LAST (SPEC.md 29.2): the stream
    clc                         ; verbs test this and nothing else
    jmp short .out
.nohw:
    mov al, DRVE_HW
    jmp short .fail
.nomem:
    mov al, DRVE_MEM
.fail:
    mov word [sbl_seg], 0       ; a refused attach holds nothing
    stc
    jmp short .out
.twice:
    mov al, DRVE_TWICE          ; NOT via .fail, which clears [sbl_seg]: here
    stc                         ; that word is the LIVE ring, and zeroing it
                                ; would turn this refusal into the very orphan
                                ; it exists to report
.out:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sbl_dma_map - claim a buffer the 8237 can actually reach
; in:  nothing
; out: CF = 0 and [sbl_seg]/[sbl_page]/[sbl_dmaoff]/[sbl_poolsz] set;
;      CF = 1 the heap has nothing page-safe
; clobbers: flags
;
; **The 8237 has no register for address bits 16..19.** Port 83h holds them
; for channel 1 and the chip never carries into them, so a transfer that
; would cross a 64KB PHYSICAL boundary wraps to the start of its page and
; plays the wrong memory. While this buffer was a pinned segment the property
; was true by construction; a heap claim has to ASK for it.
;
; And it asks the allocator, which is the only thing that can answer it
; cheaply. This used to claim, test the address it got, and claim AGAIN while
; still HOLDING the block that failed - so finding 32KB could hold 128KB, and
; a 128KB machine with exactly one good hole would be told it had no page-safe
; memory while sitting on the hole. OSAPI_MEM_CLAIM_DMA moves the test inside
; the scan: a candidate whose head would straddle bumps to the next page
; floor, the same way an overlapping one bumps past the claim it hit, and the
; first block that satisfies BOTH is the one returned (SPEC.md 50.3).
;
; CX is this claim and nothing else. The staging pool is a claim of its own
; now, copied by us with rep movsb where the chip never sees it, so it may
; straddle freely - and constraining it too would rule out every base in the
; upper half of a page for nothing. 8KB is also strictly easier to place than
; the 12KB this used to ask for (SPEC.md 34.6.1), which matters because this
; is the one claim in the system that can fail for a reason the user cannot
; see.
; -----------------------------------------------------------------------------
sbl_dma_map:
    push ax
    push bx
    push cx
    push dx
                                ; PRECONDITION: [sbl_seg] is 0. sbl_attach is
                                ; the only caller and refuses before it gets
                                ; here if a ring is already held, because that
                                ; is a double attach and a double attach is a
                                ; BUG - so this does not test, and must not
                                ; start freeing what it finds. A version that
                                ; quietly freed and re-claimed would make the
                                ; caller's bug invisible again, which is how
                                ; the 12KB orphan survived in the first place
    mov word [sbl_seg], 0
    mov ax, SBL_WANT            ; 8KB, ALL of which the chip addresses -
    mov cx, ax                  ; so the page rule covers the whole claim
    call OSAPI_MEM_CLAIM_DMA    ; out CF, DX = base segment
    jc .fail
    mov [sbl_seg], dx
    mov ax, dx
    and ax, 0x0FFF
    mov bx, 4
.shl:
    shl ax, 1                   ; paragraphs into this 64KB page x 16 = bytes
    dec bx                      ; (0..65520, so this cannot overflow)
    jnz .shl
    mov [sbl_dmaoff], ax        ; where we sit inside our page...
    mov ax, dx
    mov bx, 12
.shr:
    shr ax, 1                   ; ...and which page that is
    dec bx
    jnz .shr
    mov [sbl_page], al
    clc                         ; the pool is NOT claimed here: sbl_pool_get
                                ; takes it when an app first wants a grant
    jmp short .out
.fail:
    mov word [sbl_seg], 0
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sbl_pool_get - claim the staging pool, if it is not already held
;
; in:  nothing
; out: CF = 0 and [sbl_poolseg]/[sbl_poolsz] usable; CF = 1 the heap could
;      not fund it and NOTHING changed
; clobbers: nothing (flags)
;
; Separate from the DMA claim on purpose, and the two are as different as
; claims get. The buffer the 8237 reads must not cross a 64KB physical
; boundary, which is easiest to satisfy on the empty heap that exists at
; attach - so it is claimed there and never moves. The pool is copied by us
; with rep movsb and the chip never sees it, so it has NO constraint at all
; and a late claim is an ordinary one. Holding 20KB from boot for a card
; nobody is playing costs 55% of a 128KB machine's heap - which is 36.5KB,
; not the 55KB an earlier comment here claimed: the kernel has grown since,
; and HEAP_SEG is the number that moved.
;
; IT ASKS IN TIERS, for the reason apps/recorder does (SPEC.md 34.6): the
; claim was a flat SBL_POOLKB whatever the machine had, so on a small heap it
; simply refused - and the recorder's own tiering could not help, because it
; tiers the GRANT while this claims all 20KB or nothing. A machine that could
; have recorded a second got zero. The rest of the driver was already written
; for a pool that is not a constant (sbl_grant_alloc bounds against
; [sbl_poolsz], "the pool we actually got"), so this is the only place that
; had to learn it.
;
; A machine with the room is unaffected: the first tier IS the old size, so
; it is claimed on the first try and every offset, bound and refusal is what
; it always was.
;
; Refusal is not an error path anybody new has to handle: verb 7 already
; answers 8 for "no space", and that is what a caller gets - and a request
; bigger than the pool we ended up with answers 7 through the same bound,
; which is the tier the caller should try next.
; -----------------------------------------------------------------------------
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
; The DMA double-buffer is a different claim and is PINNED by construction -
; it carries MC_DMA, so mem_can_move refuses it whatever anyone declares
; (SPEC.md 66.3), which is right twice over: the card may be mid-transfer, and
; the 64KB page rule is a property of the address.
; -----------------------------------------------------------------------------
sbl_reloc:
    cmp bx, [sbl_poolseg]
    jne .out
    mov [sbl_poolseg], dx
.out:
    ret

sbl_pool_get:
    push ax
    push bx
    push cx                     ; CX too since the tier walk shifts KB into
    push dx                     ; bytes - this routine clobbers NOTHING
    cmp word [sbl_poolseg], 0
    jne .ok                     ; already held: grants share one claim
    mov ax, SBL_POOLKB
.try:
    call OSAPI_MEM_CLAIM        ; out CF, DX = base segment
    jnc .got
    sub ax, SBL_POOLSTEP        ; ...the next tier down. AX is KB and the
    cmp ax, SBL_POOLMIN         ; step divides the top, so this cannot step
    jae .try                    ; past the floor onto a negative
    jmp short .fail
.got:
    mov [sbl_poolseg], dx
    push ax                     ; ...and it is MOVABLE (SPEC.md 66.5.5). This
    mov ax, sbl_reloc           ; is the claim docs/FIELD-NOTES.md 2 named as
    call OSAPI_MEM_MOVABLE      ; the fragmenting party: a LATE claim, taken on
    pop ax                      ; the first stream grant, so it lands above
                                ; whatever the app already holds and bounds the
                                ; hole that app's next free leaves behind
    mov cl, 10                  ; KB -> bytes: the pool we ACTUALLY got, which
    shl ax, cl                  ; is what every bound below reads
    mov [sbl_poolsz], ax
.ok:
    clc
    jmp short .out
.fail:
    mov word [sbl_poolseg], 0
    mov word [sbl_poolsz], 0
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
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
    mov word [sbl_poolseg], 0
    mov word [sbl_poolsz], 0
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
    push bx
    push cx
    push dx
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
    cmp byte [sbl_wtask], 0     ; against a worker that sleeps a tick between
    je .gone                    ; passes, and timed out with it still asleep.
    call OSAPI_GET_TICKS        ; This wait is best-effort ordering - the
    mov dx, ax                  ; worker's last card touch before the DSP
.tk:                            ; reset below - not the survival guarantee:
    call OSAPI_TASK_YIELD       ; the KERNEL waits for the death itself
    cmp byte [sbl_wtask], 0     ; ([drv_wcnt], cleared inside task_exit) in
    je .gone                    ; drv_unload, before the image is freed
    call OSAPI_GET_TICKS        ; (SPEC.md 51.7)
    cmp ax, dx
    je .tk                      ; same tick still: yield again
    loop .wait
.gone:
    call sbl_unhook             ; vector, mask and DSP back as we found them
    xor bx, bx                  ; every grant is void once the card is gone,
.gclr:                          ; so the pool can go with it
    mov word [sbl_gr_act+bx], 0
    add bx, 2
    cmp bx, SBL_GMAX*2
    jb .gclr
    call sbl_pool_put
    mov dx, [sbl_seg]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE         ; ...and the buffer LAST, after the vector is
    mov word [sbl_seg], 0       ; gone: freeing it first would leave a live
.out:                           ; IRQ pointing into the next claim
    pop dx
    pop cx
    pop bx
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
    mov dx, [sbl_base]          ; DSP reset: 1 -> 2x6h, >= 3us, 0 -> 2x6h
    add dx, 6
    mov al, 1
    out dx, al
    push dx
    add dx, 8                   ; ~3us of ISA reads from 2xEh
    mov cx, 8
.dly:
    in al, dx
    loop .dly
    pop dx
    xor al, al
    out dx, al
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
; in:       nothing (called from DRVV_ATTACH beside opl_state_init; the
;           probe that fills sbl_base/version runs AFTER this)
; out:      nothing
; clobbers: nothing (flags)
; -----------------------------------------------------------------------------
sbl_state_init:
    push ax
    push bx
    mov word [sbl_base], 0
    mov byte [sbl_verhi], 0
    mov byte [sbl_verlo], 0
    mov byte [sbl_auto], 0
    mov byte [sbl_irq], 0xFF    ; discovery deferred to first use (34.5)
    mov byte [sbl_hooked], 0
    mov byte [sbl_dsc_fired], 0xFF
    mov byte [sbl_expect], 0
    mov word [sbl_under], 0
    mov byte [sbl_str_act], 0
    mov byte [sbl_gen], 0
    mov byte [sbl_str_inst], 0xFF
    mov byte [sbl_str_state], 0
    mov byte [sbl_str_dir], 0
    mov byte [sbl_play], 0
    mov byte [sbl_valid], 0
    mov byte [sbl_valid+1], 0
    mov byte [sbl_drain_h], 0
    mov byte [sbl_paused], 0
    mov byte [sbl_tc], 0
    mov byte [sbl_ring_on], 0
    mov byte [sbl_ropen], 0
    mov word [sbl_rmask], 0
    mov word [sbl_str_off], 0
    mov word [sbl_str_max], 0
    mov word [sbl_total], 0
    mov word [sbl_fed], 0
    mov word [sbl_consumed], 0
    mov word [sbl_wd], 0
    mov word [sbl_wd_rst], 0
    mov word [sbl_half], SBL_HALF
    mov byte [sbl_wide], 0
    mov word [sbl_rate], 0
    mov word [sbl_hopen], SBL_HALF
    xor bx, bx
.g:
    mov word [sbl_gr_act+bx], 0
    add bx, 2
    cmp bx, SBL_GMAX*2
    jb .g
    pop bx
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
    mov byte [sbl_auto], 0
    cmp byte [sbl_verhi], 2     ; >= 2.00: auto-init DMA (SPEC.md 34.5)
    jb .cfg
    mov byte [sbl_auto], 1
.cfg:
    mov ax, [sbl_base]          ; the row's reserved config word (SPEC.md
    mov [sbl_base], ax          ; the base the scan accepted
    mov al, 1
    jmp .out
.no:
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
    push di
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
    je .win
    mov bl, 1                   ; IRQ5 -> candidate 1
    cmp al, 2
    je .win
    mov bl, 0                   ; IRQ7 -> candidate 0
    cmp al, 4
    je .win                     ; IRQ10 (slave PIC) or garbage: fall back
.probe:                         ; to the firing probe below
    xor si, si                  ; hook the 4 candidates onto the stubs
.hook:
    mov bx, [sbl_dsc_vec+si]
    mov di, si
    shl di, 1                   ; DI = candidate * 4: the old-vector slot
    pushf
    cli
    mov ax, [es:bx]
    mov [sbl_dsc_oldv+di], ax
    mov ax, [es:bx+2]
    mov [sbl_dsc_oldv+di+2], ax
    mov ax, [sbl_dsc_stub+si]
    mov [es:bx], ax
    mov word [es:bx+2], cs      ; the stubs are OURS, not the kernel's
    popf
    add si, 2
    cmp si, 8
    jb .hook
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
    xor si, si                  ; ...and every candidate's vector
.unhook:
    mov bx, [sbl_dsc_vec+si]
    mov di, si
    shl di, 1
    pushf
    cli
    mov ax, [sbl_dsc_oldv+di]
    mov [es:bx], ax
    mov ax, [sbl_dsc_oldv+di+2]
    mov [es:bx+2], ax
    popf
    add si, 2
    cmp si, 8
    jb .unhook
    mov bl, [sbl_dsc_fired]
    cmp bl, 0xFF
    je .none
.win:
    xor bh, bh                  ; the winner: BX = candidate index
    mov al, [sbl_dsc_irqn+bx]
    mov [sbl_irq], al
    mov ah, [sbl_dsc_mbit+bx]   ; mouse_init order: mask...
    in al, 0x21
    or al, ah
    out 0x21, al
    mov si, bx                  ; ...save the old vector + install sbl_isr
    shl si, 1                   ; under pushf/cli...
    mov si, [sbl_dsc_vec+si]
    pushf
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
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret                         ; near: .fartext is retired here


; =============================================================================
; state (SPEC.md 34.7) - stored by sbl_state_init from snd_init, before
; snd_live publishes; unreadable until it does. sbl_expect and
; sbl_under keep the names SPEC.md 34.5 pins for them.
; =============================================================================

sbl_base:     times 1 dw 0            ; DSP base port (220h..260h), 0 = none
sbl_verhi:    times 1 db 0            ; DSP version from E1h: major
sbl_verlo:    times 1 db 0            ;   minor
sbl_auto:     times 1 db 0            ; 1 = auto-init strategy (DSP >= 2.00, 34.5)
sbl_irq:      times 1 db 0            ; discovered IRQ line, 0xFF = not yet
sbl_hooked:   times 1 db 0            ; sbl_isr installed on [sbl_irq]'s vector
sbl_oldvec:   times 2 dw 0            ; the winner's saved vector (off, seg)
sbl_dsc_fired: times 1 db 0           ; discovery: confirmed candidate index
sbl_dsc_oldv: times 8 dw 0            ; discovery: 4 saved candidate vectors
sbl_dsc_mask: times 1 db 0            ; discovery: saved 8259 mask
sbl_expect: times 1 db 0           ; a block IRQ is legitimate (SPEC.md 34.5)
sbl_under: times 1 dw 0            ; underrun-pause count (debug, cumulative)

sbl_str_act:  times 1 db 0            ; stream record: 1 = open
sbl_gen:      times 1 db 0            ;   generation = the handle (staleness)
sbl_str_inst: times 1 db 0            ;   owning instance (0xFF = kernel)
sbl_str_state: times 1 db 0           ;   SBL_ST_* (verb 3)
sbl_str_dir:  times 1 db 0            ;   0 = output, 1 = input (SPEC.md 34.6)
sbl_str_off:  times 1 dw 0            ;   grant offset the stream reads from
                                ;   (input: captures into)
sbl_str_max:  times 1 dw 0            ;   bytes from there to the grant's end
sbl_total:    times 1 dw 0            ;   valid bytes (grows via verb 1);
                                ;   input: the capture capacity
sbl_fed:      times 1 dw 0            ;   bytes copied into the double buffer
sbl_consumed: times 1 dw 0            ;   bytes played (capped at sbl_total);
                                ;   input: bytes drained into the grant
sbl_play:     times 1 db 0            ;   the half the DSP sounds (input:
                                ;   captures into) next
sbl_valid:    times 2 db 0            ;   per-half: refilled since last consumed
                                ;   (input: captured, awaiting drain)
sbl_drain_h:  times 1 db 0            ;   input: next half the drain task empties
sbl_paused:   times 1 db 0            ;   D0h issued; sbl_go_on owed
sbl_tc:       times 1 db 0            ;   DSP time constant for the open rate
sbl_ring_on:  times 1 db 0            ;   1 = ring mode (SPEC.md 34.5): the three
                                ;   counters are free-running mod 65536 and
                                ;   fills are whole unpadded halves
sbl_ropen:    times 1 db 0            ;   verb 0's latched open-flags bit 0 - a
                                ;   TEMP, committed into sbl_ring_on only
                                ;   with the rest of the record (a busy
                                ;   refusal must not disturb a live stream)
sbl_rmask:    times 1 dw 0            ;   ring: RL-1 (RL = power of two 4096..
                                ;   32768); physical offset of stream byte
                                ;   n = sbl_str_off + (n & sbl_rmask)
sbl_wd:       times 1 dw 0            ;   watchdog countdown (snd_tick, 34.5)
sbl_wd_rst:   times 1 dw 0            ;   its reload: ~2 block periods in ticks
sbl_half:     times 1 dw 0            ;   the open's half size: 2048, or 4096 in
                                ;   the wide-rate regime (SPEC.md 34.5 -
                                ;   the 2x4KB buffer annexes the record
                                ;   ring's first 4KB, idle by half-duplex)
sbl_wide:     times 1 db 0            ;   1 = wide: rate via 41h, start via C6h
sbl_rate:     times 1 dw 0            ;   wide: the open rate in Hz (41h operand)
sbl_hopen:    times 1 dw 0            ;   verb 0's half width - a TEMP, the
                                ;   sbl_ropen idiom (committed at .irqok)

; --- where our DMA buffer landed (SPEC.md 51.4) ------------------------------
; It was a pinned 64KB segment whose page and offset were constants. A
; driver's is a heap claim, so all three are computed once at attach by
; sbl_dma_map and read by every 8237 programming site.
sbl_seg:      dw 0              ; the DMA claim's segment: double buffer and
                                ;   record ring, page-safe, held from attach
sbl_poolseg:  dw 0              ; the STAGING POOL's own claim, 0 = not held.
                                ;   Taken on the first grant, released with
                                ;   the last (sbl_pool_get/put)
sbl_page:     db 0              ; the 8237 page it landed in (address bits
                                ; 16..19 - the XT's 8237 has no register for
                                ; them, which is what 83h is)
sbl_pad9:     db 0
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
sbl_gr_inst:  times SBL_GMAX dw 0     ;   owning instance (low byte),
sbl_gr_act:   times SBL_GMAX dw 0     ;   in use (occupancy IS these records)

