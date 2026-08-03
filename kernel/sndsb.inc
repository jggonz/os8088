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
; (SPEC.md 34.5): candidates {7,5,3,2} are hooked onto the .text stubs below,
; F2h triggers an 8-bit IRQ, and a candidate is accepted only after its
; stub's 2xEh bit-7 read confirms the DSP asserted it (retried once); the
; losers are unhooked, and the winner is hooked mouse_init-style (save old
; vector, install while masked, unmask) onto sbl_isr, staying hooked until
; snd_unhook.
;
; sbl_isr (mou_isr discipline: IF=0 throughout, DS=KERNEL_SEG, no BIOS,
; never sti, own EOI): the spurious-IRQ-7 guard first - the interrupt is
; SB's only if snd_sb_expect is set AND 2xEh bit 7 confirms; an unexpected
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
SBL_RHALF    equ 4096           ; record ring half size (2 x 4KB = 8KB)
SBL_POOL     equ 0x3000         ; staging pool base offset (SPEC.md 2.2)
SBL_POOLMAX  equ 0xD000         ; pool bytes (0x3000..0xFFFF)
SBL_GMAX     equ 8              ; staging-grant records
SBL_DCHUNK   equ 512            ; drain-copy chunk: one act+gen-guarded
                                ; IF=0 write unit (SPEC.md 34.6, ~2ms of
                                ; rep movsb on a 4.77MHz 8088)

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
    mov cl, 3
    mov ah, al                  ; AX = half * 256
    xor al, al
    shl ax, cl                  ; AX = half * 2048: the buffer offset, which
    mov dx, ax                  ; is the DMA base address (SND_SEG:0 opens
                                ; page 3 at offset 0 - nothing doubled)
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
    mov al, 3
    out 0x83, al                ; page 3 (SND_SEG, SPEC.md 2.2)
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
    inc al                      ; ring offset = (half+1) << 12: 0x1000 or
    mov cl, 4                   ; 0x2000 (SPEC.md 2.2) - lo byte is 0,
    shl al, cl                  ; hi byte = (half+1) * 10h
    mov dh, al
    xor dl, dl
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
    mov al, 3
    out 0x83, al                ; page 3 (SND_SEG, SPEC.md 2.2)
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
    mov al, 0x40                ; time constant
    call sbl_dsp_wr
    mov al, [sbl_tc]
    call sbl_dsp_wr
    mov al, 0xD1                ; speaker (DAC) on
    call sbl_dsp_wr
    cmp byte [sbl_auto], 0
    je .single
    pushf
    cli
    mov al, 0x05
    out 0x0A, al                ; mask channel 1
    out 0x0C, al                ; flip-flop clear
    xor al, al
    out 0x02, al                ; address 0x0000 (SND_SEG:0)
    out 0x02, al
    out 0x0C, al                ; flip-flop clear
    mov al, (2*SBL_HALF-1) & 0xFF
    out 0x03, al                ; count = the whole ring - 1
    mov al, (2*SBL_HALF-1) >> 8
    out 0x03, al
    mov al, 0x59                ; auto-init, read, channel 1
    out 0x0B, al
    mov al, 3
    out 0x83, al                ; page 3
    mov al, 0x01
    out 0x0A, al                ; unmask
    popf
    mov al, 0x48                ; DSP block size = one half
    call sbl_dsp_wr
    mov al, (SBL_HALF-1) & 0xFF
    call sbl_dsp_wr
    mov al, (SBL_HALF-1) >> 8
    call sbl_dsp_wr
    mov al, 0x1C                ; auto-init 8-bit DMA output
    call sbl_dsp_wr
    jmp .out
.single:
    mov al, 0
    call sbl_arm_half           ; half 0; the ISR re-arms from there
.out:
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
    mov al, SBL_RING & 0xFF
    out 0x02, al                ; address = SBL_RING (0x1000, page 3)
    mov al, SBL_RING >> 8
    out 0x02, al
    out 0x0C, al                ; flip-flop clear
    mov al, (2*SBL_RHALF-1) & 0xFF
    out 0x03, al                ; count = the whole ring - 1
    mov al, (2*SBL_RHALF-1) >> 8
    out 0x03, al
    mov al, 0x55                ; auto-init, write (card -> memory), ch 1
    out 0x0B, al
    mov al, 3
    out 0x83, al                ; page 3
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
; mou_isr discipline: push used regs + DS + ES, DS = KERNEL_SEG, IF=0
; throughout, no BIOS, never sti, own EOI (out 20h, 20h - the BIOS does not
; handle this IRQ). Never touches VGA, gfx_lock or the switch path.
;
; Guard first: the interrupt is SB's only if snd_sb_expect is set AND 2xEh
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
; single-cycle DSP simply is not re-armed), bump snd_sb_under, mark the
; stream underrun-paused and stop expecting IRQs; the refill task resumes
; with sbl_go_on once data reappears (SPEC.md 34.5, the underrun contract).
; =============================================================================
sbl_isr:
    push ax
    push bx
    push dx
    push ds
    push es
    mov ax, KERNEL_SEG
    mov ds, ax
    cld

    cmp byte [snd_sb_expect], 0 ; the spurious guard (SPEC.md 34.5, binding)
    je .spur
    mov dx, [sbl_base]
    add dx, 0x0E
    in al, dx                   ; confirm AND ack: the 2xEh read
    test al, 0x80
    jz .spur
    cmp byte [sbl_str_dir], 0   ; input streams take the mirror half-swap
    jne .input

    mov ax, [sbl_consumed]      ; one block played (cap at the valid length:
    add ax, SBL_HALF            ; the tail of the last half is silence pad)
    cmp ax, [sbl_total]
    jbe .cons
    mov ax, [sbl_total]
.cons:
    mov [sbl_consumed], ax

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
    mov byte [snd_sb_expect], 0
    inc word [snd_sb_under]
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
    mov byte [snd_sb_expect], 0
    inc word [snd_sb_under]
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
    mov ah, al                  ; AH = candidate index (AL becomes the port)
    mov dx, KERNEL_SEG
    mov ds, dx
    mov dx, [sbl_base]
    add dx, 0x0E
    in al, dx                   ; confirm (and ack) via 2xEh bit 7
    test al, 0x80
    jz .no                      ; not the DSP
    mov [sbl_dsc_fired], ah     ; accepted: the discovery loop reads this
    jmp short .eoi
.no:                            ; confirm failed - but a real stray from an
    or ah, ah                   ; unrelated device (the lines are force-
    jnz .eoi                    ; unmasked ~330ms) still needs its EOI, or
                                ; its whole priority level wedges. Only
                                ; candidate 0 (IRQ 7) can be spurious:
    mov al, 0x0B                ; OCW3 - the next 0x20 read returns the
    out 0x20, al                ; in-service register
    in al, 0x20
    test al, 0x80               ; ISR bit 7 clear = spurious: no EOI
    jz .out                     ; (sbl_isr's split, SPEC.md 34.5)
.eoi:
    mov al, 0x20
    out 0x20, al
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
    jcxz .e7
    cmp cx, SBL_POOLMAX
    ja .e7
    pushf
    cli
    mov si, SBL_POOL            ; SI = candidate base
.rescan:
    mov ax, cx
    dec ax
    add ax, si                  ; AX = candidate's last byte
    jc .e8f                     ; ran off the top of the segment
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
    mov ax, 8
    jmp .err
.e7:
    mov ax, 7
.err:
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
    cmp byte [snd_live], 0      ; not initialised: nothing can stream
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
    mov ax, 7                   ; anything higher: bad verb
    stc
    ret
.e4:
    mov ax, 4
    stc
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
;           explicitly accepting progressive-feed risk - the staged rule)
; out:      AL = 0 ok with AH = handle, else AX = 1 busy / 2 rate / 4 no
;           sink or no IRQ found / 6 no task slot / 7 bad grant; CF mirrors
;           the error
; clobbers: nothing else (flags)
;
; First use runs IRQ discovery (sbl_irq_disc, far) and hooks sbl_isr onto
; the winner. The record publishes act=1 before the refill task spawns,
; so neither the tick nor the ISR can see a half-built stream (the
; publish-last idiom) - but snd_sb_expect arms BEFORE sbl_hw_start, the
; same order sbl_refill's resume uses: the first block IRQ can beat the
; opener's next instruction, and an unexpected real IRQ never reads 2xEh,
; leaving the DSP INT latched and the stream permanently silent.
;
; One serialization assumption, pinned (SPEC.md 20.3): stream verbs run
; only from window callbacks on the single UI task, so the busy check
; here and the eventual act=1 publish - ~60 IF=1 instructions apart, and
; IRQ discovery can even sleep on ticks in between - never race a second
; open. A future background-task caller would have to claim the record
; under pushf/cli first (the sbl_grant_alloc single-window standard).
; -----------------------------------------------------------------------------
sbl_v_open:
    push bx
    push cx
    push dx
    push si
    test byte [snd_drv + SDRV_SIZE*DRV_SB + SDRV_FLAGS], 1
    jz .e4                      ; no card: the documented clean failure
    cmp byte [sbl_str_act], 0
    jne .e1                     ; one stream per card (SPEC.md 34.3)
    cmp byte [snd_pcm_busy], 0
    jne .e1                     ; an exclusive clip holds the machine
    cmp dx, 4000                ; TC = 256 - 1e6/rate must fit a byte...
    jb .e2
    cmp dx, 22222               ; ...and the 8-bit-out ceiling is honoured
    ja .e2                      ; (SPEC.md 34.5) - err 2, never a clamp
    or cx, cx                   ; nothing staged at all? (8086 conditional
    jnz .args                   ; jumps are short-only: .e7 sits past reach,
    jmp .e7                     ; so the refusals trampoline)
.args:
    mov bx, dx                  ; BX = rate (snd_req_inst wants DH)
    call snd_req_inst           ; DH = the requesting instance (SPEC.md 34.3)
    call sbl_grant_chk          ; the range must sit in the caller's grant;
    jnc .grantok                ; out AX = bytes to the grant's end
    jmp .e7
.grantok:
    mov [sbl_str_max], ax       ; the feed bound
    mov [sbl_str_inst], dh
    cmp byte [sbl_irq], 0xFF    ; IRQ discovery, deferred to first use
    jne .irqok                  ; (SPEC.md 34.5)
    call sbl_irq_disc           ; far orchestration; hooks the winner
    or al, al
    jz .e4                      ; no candidate confirmed: no usable card
.irqok:
    mov ax, 0x4240              ; TC = 256 - 1,000,000/rate (SPEC.md 34.5)
    mov dx, 0x000F
    div bx                      ; AX = 1e6/rate (45..250 by the range check)
    neg al                      ; AL = 256 - AX (mod 256)
    mov [sbl_tc], al
    mov ax, 9011                ; watchdog: 2 block periods in ticks + 3 =
    mov dx, 1                   ; (2*2048*18.2)/rate + 3 = 74547/rate + 3
    div bx
    add ax, 3
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
    mov byte [sbl_str_state], SBL_ST_PLAY
    inc byte [sbl_gen]
    xor bx, bx                  ; prime the double buffer: half 0 always
    call sbl_fill_half          ; has data (CX > 0)...
    mov byte [sbl_valid], 1
    mov ax, [sbl_total]
    sub ax, [sbl_fed]
    jz .primed                  ; ...half 1 only if any data remains
    mov bx, 1
    call sbl_fill_half
    mov byte [sbl_valid+1], 1
.primed:
    mov byte [sbl_str_act], 1   ; publish, then pace: the refill task
    mov dl, [sbl_str_inst]      ; (SPEC.md 34.5 - the kernel owns pacing);
    mov dh, [sbl_gen]           ; DL -> T_INST, DH = its stream generation
    mov ax, sbl_refill_task
    mov cx, KERNEL_SEG          ; a kernel task (SPEC.md 8: CX = the frame's
                                ; DS/ES/CS; the caller's CX is banked on the
                                ; stack by the prologue)
    call task_spawn
    jc .nospawn
    mov byte [snd_sb_expect], 1 ; arm BEFORE the hardware starts (the
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
; in:       AH = handle, CX = new total valid length (>= the old, and within
;           the grant)
; out:      AX = 0 ok / 7 bad length or an input stream (SPEC.md 34.6) /
;           0FFFFh stale; CF set on both errors
; clobbers: nothing else (flags)
;
; A single word store: the ISR and the refill task read [sbl_total] whole.
; An underrun-paused stream resumes on the refill task's next wake; a
; watchdog-stopped stream does not (its task is gone - the owner closes).
; -----------------------------------------------------------------------------
sbl_v_feed:
    call sbl_hcheck
    jc .stale
    cmp byte [sbl_str_dir], 0   ; a record stream has no valid length to
    jne .e7                     ; extend (SPEC.md 34.6): err 7
    cmp cx, [sbl_total]
    jb .e7                      ; the valid length never shrinks
    cmp cx, [sbl_str_max]
    ja .e7                      ; past the grant's end
    mov [sbl_total], cx
    xor ax, ax
    clc
    ret
.e7:
    mov ax, 7
    stc
    ret
.stale:
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
; out:      nothing; the DSP is silent, snd_sb_expect clear, act = 0
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
    mov byte [snd_sb_expect], 0
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
;           with DX = bytes consumed; or AX = 0FFFFh stale (CF = 1, DX = 0)
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
sbl_v_status:
    call sbl_hcheck
    jc .stale
    mov al, [sbl_str_state]
    mov ah, 0
    mov dx, [sbl_consumed]
    clc
    ret
.stale:
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
; drain task spawns, snd_sb_expect armed BEFORE sbl_hw_start_in for the
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
    test byte [snd_drv + SDRV_SIZE*DRV_SB + SDRV_FLAGS], 1
    jz .e4                      ; no card: the documented clean failure
    cmp byte [sbl_str_act], 0
    jne .e1                     ; half-duplex: one stream record, either
                                ; direction (SPEC.md 34.3/34.6)
    cmp byte [snd_pcm_busy], 0
    jne .e1                     ; an exclusive clip holds the machine
    cmp dx, 4000                ; TC = 256 - 1e6/rate must fit a byte...
    jb .e2
    mov ax, 13000               ; ...and the INPUT ceiling is honoured:
    cmp byte [sbl_auto], 0      ; 13k on 1.x, 15k on >= 2.00 normal mode
    je .ceil                    ; (SPEC.md 34.5) - err 2, never a clamp
    mov ax, 15000
.ceil:
    cmp dx, ax
    ja .e2
    or cx, cx                   ; zero capacity captures nothing (8086
    jnz .args                   ; conditionals are short-only: trampoline)
    jmp .e7
.args:
    mov bx, dx                  ; BX = rate (snd_req_inst wants DH)
    call snd_req_inst           ; DH = the requesting instance (SPEC.md 34.3)
    call sbl_grant_chk          ; the capture range must sit in the caller's
    jnc .grantok                ; grant; out AX = bytes to the grant's end
    jmp .e7
.grantok:
    mov [sbl_str_max], ax
    mov [sbl_str_inst], dh
    cmp byte [sbl_irq], 0xFF    ; IRQ discovery, deferred to first use
    jne .irqok                  ; (SPEC.md 34.5)
    call sbl_irq_disc           ; far orchestration; hooks the winner
    or al, al
    jz .e4                      ; no candidate confirmed: no usable card
.irqok:
    mov ax, 0x4240              ; TC = 256 - 1,000,000/rate (SPEC.md 34.5)
    mov dx, 0x000F
    div bx                      ; AX = 1e6/rate (66..250 by the range check)
    neg al                      ; AL = 256 - AX (mod 256)
    mov [sbl_tc], al
    mov ax, 18023               ; watchdog: 2 ring-half periods in ticks + 3
    mov dx, 2                   ; = (2*4096*18.2)/rate + 3 = 149095/rate + 3
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
    mov byte [sbl_str_state], SBL_ST_PLAY
    inc byte [sbl_gen]
    mov byte [sbl_str_act], 1   ; publish, then pace: the drain task
    mov dl, [sbl_str_inst]      ; (SPEC.md 34.6 - the kernel owns pacing);
    mov dh, [sbl_gen]           ; DL -> T_INST, DH = its stream generation
    mov ax, sbl_drain_task
    mov cx, KERNEL_SEG          ; a kernel task (SPEC.md 8: CX = the frame's
                                ; DS/ES/CS; the caller's CX is banked on the
                                ; stack by the prologue)
    call task_spawn
    jc .nospawn
    mov byte [snd_sb_expect], 1 ; arm BEFORE the hardware starts - the
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
;           ([osapi_dseg], recorded by slot 0x0100's wrapper - SPEC.md 20.3)
; out:      AX = 0 ok / 7 refused (range not inside the caller's grant);
;           CF mirrors
; clobbers: nothing else (flags; ES restored per SPEC.md 1)
;
; The kernel-staged copy OUT - verb 6's exact mirror (SPEC.md 34.6):
; packages never hold an ES pointer into SND_SEG. No handle: the grant
; owns the bytes, so captured data stays readable after the stream that
; recorded it closes, until the grant is freed. Works on every machine,
; like verbs 6-7 - the pool exists wherever SND_SEG does.
; -----------------------------------------------------------------------------
sbl_v_read:
    push cx
    push dx
    push si
    push di
    push es
    jcxz .ok                    ; nothing to copy is trivially fine
    call snd_req_inst           ; DH = calling instance
    call sbl_grant_chk          ; range-check the SOURCE
    jc .e7
    mov es, [osapi_dseg]        ; ES:DI = the caller's destination, in the
                                ; segment the slot wrapper recorded - the
                                ; staging copy IS the v3 boundary (SPEC.md
                                ; 20.3/34.6; a package's DI is an offset in
                                ; its OWN segment, not the kernel's)
    mov ax, SND_SEG
    push ds
    mov ds, ax                  ; DS:SI = the grant source
    cld
    rep movsb                   ; SND_SEG:SI -> caller DI, interruptible
    pop ds
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
; in:       SI = source offset in the CALLER's segment ([osapi_dseg],
;           recorded by slot 0x0100's wrapper - SPEC.md 20.3), CX = bytes,
;           DI = destination offset in SND_SEG (inside a grant the caller
;           owns)
; out:      AX = 0 ok / 7 refused (range not inside the caller's grant);
;           CF mirrors
; clobbers: nothing else (flags; ES restored per SPEC.md 1)
;
; The kernel-staged copy in: packages never hold an ES pointer into
; SND_SEG (SPEC.md 2.2, the dsk_get_dir idiom).
; -----------------------------------------------------------------------------
sbl_v_stage:
    push cx
    push dx
    push si
    push di
    push es
    jcxz .ok                    ; nothing to copy is trivially fine
    call snd_req_inst           ; DH = calling instance
    push si
    mov si, di                  ; range-check the DESTINATION
    call sbl_grant_chk
    pop si
    jc .e7
    mov ax, SND_SEG
    mov es, ax
    mov ax, [osapi_dseg]        ; the caller's segment (slot wrapper): the
    cld                         ; source pointer lives THERE since v3
    push ds
    mov ds, ax
    rep movsb                   ; callerseg:SI -> SND_SEG:DI, interruptible
    pop ds
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
; -----------------------------------------------------------------------------
sbl_v_grant:
    push bx
    push dx
    or ah, ah
    jz .alloc
    cmp ah, 1
    jne .e7
    call snd_req_inst           ; free: only the owner's own grant
    call sbl_grant_find
    jc .e7
    cmp byte [sbl_str_act], 1   ; refused while an open stream reads from
    jne .fok                    ; this grant (SPEC.md 20.3 verb 7): the
    mov ax, [sbl_str_off]       ; refill task would copy unowned bytes -
    cmp ax, [sbl_gr_off+bx]     ; and a later alloc's, once re-granted
    jb .fok
    mov dx, [sbl_gr_off+bx]
    add dx, [sbl_gr_size+bx]
    dec dx                      ; DX = the grant's last byte (off+size can
    cmp ax, dx                  ; be 0x10000 exactly: compare last bytes)
    jbe .e7                     ; live stream inside: close it first
.fok:
    mov word [sbl_gr_act+bx], 0
    xor ax, ax
    clc
    jmp .out
.alloc:
    call snd_req_inst           ; DH = the instance the grant is stamped
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
; out:      nothing; [sbl_fed] advanced; the half's tail is 0x80 silence
;           when the data ran short
; clobbers: nothing (flags)
;
; Both source and destination live in SND_SEG, so the copy runs with
; DS = ES = SND_SEG for the movs; kernel state is only touched before the
; switch. The caller publishes the half's valid flag afterwards.
; -----------------------------------------------------------------------------
sbl_fill_half:
    push ax
    push cx
    push dx
    push si
    push di
    push es
    mov ax, SND_SEG
    mov es, ax
    mov di, bx                  ; DI = half * 2048: the destination
    mov cl, 11
    shl di, cl
    mov si, [sbl_str_off]       ; SI = grant + progress: the source
    add si, [sbl_fed]
    mov cx, [sbl_total]
    sub cx, [sbl_fed]           ; CX = data remaining
    cmp cx, SBL_HALF
    jbe .n
    mov cx, SBL_HALF
.n:
    mov dx, SBL_HALF            ; DX = silence pad after the data
    sub dx, cx
    add [sbl_fed], cx
    cld
    jcxz .pad
    push ds
    push es
    pop ds                      ; DS = SND_SEG for the grant -> buffer copy
    rep movsb
    pop ds
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
; sbl_refill - one refill pass (task context)
;
; in:       DH = the stream generation this task serves
; out:      nothing
; clobbers: nothing (flags)
;
; Fills the half the DSP needs next first, then the other; publishes each
; valid flag only after its content is in place. Then the resume check: an
; underrun-paused stream whose next half is now valid continues - flags,
; expectation, watchdog and ports in ONE pushf/cli window (SPEC.md 34.3),
; which RE-CHECKS act and generation first: a close (or teardown) that ran
; between the checks above and the window must not see D4h/14h re-arm a
; stream it already stopped, nor the ISR scribble a released record.
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
    mov byte [snd_sb_expect], 1
    call sbl_go_on              ; D4h / re-arm (SPEC.md 34.5)
.skip:
    popf
.done:
    pop bx
    pop ax
    ret
.half:                          ; fill half BX if invalid and data exists
    cmp byte [sbl_valid+bx], 0
    jne .hout
    mov ax, [sbl_total]
    sub ax, [sbl_fed]
    jz .hout                    ; nothing staged beyond what was copied
    call sbl_fill_half
    mov byte [sbl_valid+bx], 1  ; publish after the content is in place
.hout:
    ret

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
.loop:
    cmp byte [sbl_str_act], 1   ; closed / torn down?
    jne .die
    mov al, [sbl_gen]           ; superseded by a newer stream?
    cmp al, dh
    jne .die
    cmp byte [sbl_str_state], SBL_ST_END
    je .die                     ; watchdog-stopped: nothing left to pace
    call sbl_refill
    mov ax, 1                   ; a block is >= 1.7 ticks at the 22kHz
    call task_sleep             ; ceiling: one-tick pacing always keeps up
    jmp .loop
.die:
    xor bx, bx                  ; no release byte: the instance record is
    jmp task_exit               ; not this task's to free (SPEC.md 29)

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
; (sbl_fill_half's pre-copy sbl_fed advance stays as is: that field is
; internal refill bookkeeping, and the output copy only READS the grant -
; a stale read at teardown is the documented benign glitch, SPEC.md 34.6.)
; The caller clears the half's valid flag afterwards.
; -----------------------------------------------------------------------------
sbl_drain_half:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov ax, SND_SEG
    mov es, ax
    mov si, bx                  ; SI = SBL_RING + half * 4096: the source
    mov cl, 12
    shl si, cl
    add si, SBL_RING
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
    push es
    pop ds                      ; DS = SND_SEG for the ring -> grant copy
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
    mov byte [snd_sb_expect], 0
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
    mov byte [snd_sb_expect], 1
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
    call task_sleep
    jmp .loop
.die:
    xor bx, bx                  ; no release byte: the instance record is
    jmp task_exit               ; not this task's to free (SPEC.md 29)

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
    cmp byte [snd_sb_expect], 0 ; only while a transfer is armed (paused /
    je .out                     ; ended streams expect nothing)
    cmp byte [sbl_str_state], SBL_ST_PLAY
    jne .out
    dec word [sbl_wd]
    jnz .out
    call sbl_halt               ; the IRQ never came: stop the stream
    mov byte [snd_sb_expect], 0
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
    pop bx
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
; sbl_state_init - store the module's .bss before snd_live publishes
;
; in:       nothing (called by snd_init beside opl_state_init; the probe
;           that fills sbl_base/version runs AFTER this)
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
    mov byte [snd_sb_expect], 0
    mov word [snd_sb_under], 0
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
    mov word [sbl_str_off], 0
    mov word [sbl_str_max], 0
    mov word [sbl_total], 0
    mov word [sbl_fed], 0
    mov word [sbl_consumed], 0
    mov word [sbl_wd], 0
    mov word [sbl_wd_rst], 0
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
; Detection and IRQ discovery: cold far code (SPEC.md 33/34.7) behind their
; FARSHIMs. DSP handshakes go through KCALL sbl_dsp_wr / sbl_dsp_rd; the
; polls and vector writes are direct. Far code keeps DS = KERNEL_SEG, so
; the tables above and all sbl_ state stay readable.
; =============================================================================
    FARSHIM sbl_probe,    sbl_f_probe
    FARSHIM sbl_irq_disc, sbl_f_irqdisc

section .fartext

; -----------------------------------------------------------------------------
; sbl_f_probe - the SB reset scan + version gate (far body; SPEC.md 34.2)
;
; in:       nothing (DS = KERNEL_SEG; boot-time, from snd_init's probe loop)
; out:      AL = 1 present - [sbl_base] set, DSP version read via E1h,
;           [sbl_auto] decided (>= 2.00), the base parked in the row's
;           SDRV_CFG word; AL = 0 absent
; clobbers: AH (snd_init's probe loop holds nothing in it); nothing else
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
    KCALL sbl_dsp_wr
    jc .no
    KCALL sbl_dsp_rd
    jc .no
    mov [sbl_verhi], al
    KCALL sbl_dsp_rd
    jc .no
    mov [sbl_verlo], al
    mov byte [sbl_auto], 0
    cmp byte [sbl_verhi], 2     ; >= 2.00: auto-init DMA (SPEC.md 34.5)
    jb .cfg
    mov byte [sbl_auto], 1
.cfg:
    mov ax, [sbl_base]          ; the row's reserved config word (SPEC.md
    mov [snd_drv + SDRV_SIZE*DRV_SB + SDRV_CFG], ax     ; 34.2)
    mov al, 1
    jmp .out
.no:
    xor al, al
.out:
    pop si
    pop dx
    pop cx
    pop bx
    retf                        ; far body: reached through the shim

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
    mov word [es:bx+2], KERNEL_SEG
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
    KCALL sbl_dsp_wr
    jc .settle                  ; DSP wedged: nothing will fire
    mov bx, [ticks]
.wait:
    cmp byte [sbl_dsc_fired], 0xFF
    jne .settle
    mov ax, [ticks]
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
    mov word [es:si+2], KERNEL_SEG
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
    retf                        ; far body: reached through the shim

section .text

; =============================================================================
; state (SPEC.md 34.7) - stored by sbl_state_init from snd_init, before
; snd_live publishes; unreadable until it does. snd_sb_expect and
; snd_sb_under keep the names SPEC.md 34.5 pins for them.
; =============================================================================
section .bss

sbl_base:     resw 1            ; DSP base port (220h..260h), 0 = none
sbl_verhi:    resb 1            ; DSP version from E1h: major
sbl_verlo:    resb 1            ;   minor
sbl_auto:     resb 1            ; 1 = auto-init strategy (DSP >= 2.00, 34.5)
sbl_irq:      resb 1            ; discovered IRQ line, 0xFF = not yet
sbl_hooked:   resb 1            ; sbl_isr installed on [sbl_irq]'s vector
sbl_oldvec:   resw 2            ; the winner's saved vector (off, seg)
sbl_dsc_fired: resb 1           ; discovery: confirmed candidate index
sbl_dsc_oldv: resw 8            ; discovery: 4 saved candidate vectors
sbl_dsc_mask: resb 1            ; discovery: saved 8259 mask
snd_sb_expect: resb 1           ; a block IRQ is legitimate (SPEC.md 34.5)
snd_sb_under: resw 1            ; underrun-pause count (debug, cumulative)

sbl_str_act:  resb 1            ; stream record: 1 = open
sbl_gen:      resb 1            ;   generation = the handle (staleness)
sbl_str_inst: resb 1            ;   owning instance (0xFF = kernel)
sbl_str_state: resb 1           ;   SBL_ST_* (verb 3)
sbl_str_dir:  resb 1            ;   0 = output, 1 = input (SPEC.md 34.6)
sbl_str_off:  resw 1            ;   grant offset the stream reads from
                                ;   (input: captures into)
sbl_str_max:  resw 1            ;   bytes from there to the grant's end
sbl_total:    resw 1            ;   valid bytes (grows via verb 1);
                                ;   input: the capture capacity
sbl_fed:      resw 1            ;   bytes copied into the double buffer
sbl_consumed: resw 1            ;   bytes played (capped at sbl_total);
                                ;   input: bytes drained into the grant
sbl_play:     resb 1            ;   the half the DSP sounds (input:
                                ;   captures into) next
sbl_valid:    resb 2            ;   per-half: refilled since last consumed
                                ;   (input: captured, awaiting drain)
sbl_drain_h:  resb 1            ;   input: next half the drain task empties
sbl_paused:   resb 1            ;   D0h issued; sbl_go_on owed
sbl_tc:       resb 1            ;   DSP time constant for the open rate
sbl_wd:       resw 1            ;   watchdog countdown (snd_tick, 34.5)
sbl_wd_rst:   resw 1            ;   its reload: ~2 block periods in ticks

sbl_gr_off:   resw SBL_GMAX     ; staging grants (SPEC.md 34.6): offset,
sbl_gr_size:  resw SBL_GMAX     ;   size in bytes,
sbl_gr_inst:  resw SBL_GMAX     ;   owning instance (low byte),
sbl_gr_act:   resw SBL_GMAX     ;   in use (occupancy IS these records)

section .text
