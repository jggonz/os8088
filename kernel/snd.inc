; =============================================================================
; os8088 - snd.inc
;
; The sound layer (SPEC.md 34), and all of it: the tone tier's
; owner/priority/generation router, the PC speaker driver (tone + PWM
; exclusive clips), snd_tick (the one scheduler touch-point), and the three
; os8088 API slot targets - caps, tone, play.
;
; THE PC SPEAKER IS THE ONLY SINK. The Sound Blaster and OPL2 drivers, the
; driver table that chose between them, the per-tier route overrides and the
; Control Panel page that set them are all gone, and with them the 64KB
; SND_SEG they needed for DMA buffers, a record ring and a staging pool. What
; is left needs no buffer at all: osapi_snd_play takes the caller's ES:SI and
; paces it out of the caller's own memory, which is the shape a future
; play-a-WAV package wants - the samples are the app's problem, not the
; kernel's.
;
; Speaker PCM (SPEC.md 34.4): ch2 in mode 0 lobyte-only is the modulator -
; one `out 42h, AL` per sample emits a low pulse of AL PIT-cycles and the
; cone integrates the train; the pacer READS the scheduler's ch0 by
; latching, one atomic latch+read triple per poll (SPEC.md 34.1, binding),
; and ch0 is never written. The clip runs to completion on the caller's
; task under sch_lock (disk_read's contract, SPEC.md 7) with IF=1; a
; mouse click always aborts it (the release-folding baseline below).
;
; Concurrency (SPEC.md 34.3, binding): every grant, steal and release updates
; the owner record (generation, priority, expiry) AND its ports inside one
; pushf/cli..popf window - snd_tick runs at IF=0 between any two task-context
; instructions, and the generation guard on tone expiry is only sound because
; task-side writers are atomic with respect to the tick.
;
; Boot gate (SPEC.md 34.7, binding): int 08h is hooked seconds before
; snd_init runs and nothing clears .bss at boot, so snd_tick's first
; instruction tests snd_live - a db 0 in initialised .text data, set to 1 as
; snd_init's LAST act. Every byte snd_tick can read is either initialised
; .text data or sits behind that gate.
;
; Port ownership (SPEC.md 34.1): PIT channel 2 + port 61h have one owner and
; one mode at a time (snd_ch2mode: 0 free / 1 tone / 2 PWM); every 61h access
; is read-modify-write under pushf/cli..popf because the upper bits belong to
; the XT's keyboard/parity logic. snd_init saves the boot state of bits 0-1;
; snd_unhook restores it. PIT channel 0 is never touched here at all.
; =============================================================================

; --- capability bits (SPEC.md 34.2) ------------------------------------------
; Two survive the sound-card removal, because the PC speaker is the only sink
; there is: a square voice, and the CPU-paced clip contract of SPEC.md 34.4.
; The FM / background-PCM / input bits went with the hardware that had them.
SND_CAP_TONE     equ 01h        ; square voice: freq on/off
SND_CAP_PCM_EXCL equ 08h        ; CPU-paced PCM out - exclusive clip contract

; --- which sound hardware the machine uses (SPEC.md 34.8), a user setting ---
; Three tiers, not two, and the middle one is the point: an AdLib is an OPL2
; and nothing else, and EVERY Sound Blaster carries an OPL2 - so an SB owner
; who picks AdLib gets FM with no DMA buffer, no IRQ and no worker task. On a
; 128KB machine that is 12KB of a 55KB heap back for a tier they were not
; using. It is a CHOICE and not a probe result, which is why it persists.
SND_RT_AUTO equ 0               ; unset (a fresh SYSTEM.CFG): the best tier
                                ; that actually answered at boot
SND_RT_SPK  equ 1               ; the PC speaker, whatever else is loaded
SND_RT_FM   equ 2               ; AdLib: FM only - no streams, no 12KB
SND_RT_SB   equ 3               ; Sound Blaster: FM and streams both
SND_RT_CARD equ SND_RT_FM       ; the old two-value name, kept so a SYSTEM.CFG
                                ; written before the split still reads as "a
                                ; card" - and reads as the CHEAP card, which
                                ; is the safe way to be wrong

SND_PRI_UI  equ 0C0h            ; kernel UI beeps (SPEC.md 34.3)
SND_PRI_PKG equ 040h            ; documented package default priority

; =============================================================================
; Initialised .text data (SPEC.md 34.7): everything snd_tick or an early
; caller could read before snd_init has stored the .bss state.
; =============================================================================

snd_live:      db 0             ; the boot gate: set LAST by snd_init (34.7)
snd_excl_ok:   db 1             ; policy: exclusive clips allowed (34.4)

snd_s_spk:     db 'Speaker', 0  ; the resident sink's name (osapi_snd_caps)
snd_route:     db SND_RT_AUTO   ; where tones go (SPEC.md 34.8). Initialised
                                ; .text, not .bss: snd_tone_out reads it on
                                ; the machine's first beep, and SYSTEM.CFG
                                ; may not exist at all

; =============================================================================
; The PC speaker tone driver (SPEC.md 34.2)
; =============================================================================

; -----------------------------------------------------------------------------
; spk_tone - the speaker row's tone op
;
; in:       AX = frequency in Hz (19..20000), or 0 = off; DL = voice (0 only)
; out:      CF = 0 done, CF = 1 unsupported voice/frequency
; clobbers: nothing else (flags)
;
; On: divisor = 1193182/AX, then the mode-3 quad (0B6h -> 43h, divisor lo/hi
; -> 42h, 61h |= 03h) under pushf/cli..popf. Off: 61h &= 0FCh, same window.
; The 61h upper bits are preserved - they belong to the XT's keyboard and
; parity logic (SPEC.md 34.1).
; -----------------------------------------------------------------------------
spk_tone:
    push ax
    push bx
    push dx
    or dl, dl                   ; the speaker has exactly one voice
    jnz .bad
    or ax, ax
    jz .off
    cmp ax, 19                  ; divisor must fit 16 bits (SPEC.md 34.2)
    jb .bad
    cmp ax, 20000
    ja .bad
    mov bx, ax                  ; divisor = 1193182 / freq
    mov dx, 0x0012              ; 1193182 = 0x1234DE
    mov ax, 0x34DE
    div bx                      ; AX = divisor (freq >= 19 keeps it 16-bit)
    mov bx, ax                  ; BL/BH = divisor lo/hi
    pushf
    cli
    mov al, 0xB6                ; ch2, lobyte/hibyte, mode 3 (square wave)
    out 0x43, al
    mov al, bl
    out 0x42, al
    mov al, bh
    out 0x42, al
    in al, 0x61                 ; gate + speaker data on; upper bits kept
    or al, 0x03
    out 0x61, al
    popf
    clc
    jmp .out
.off:
    pushf
    cli
    in al, 0x61
    and al, 0xFC
    out 0x61, al
    popf
    clc
    jmp .out
.bad:
    stc
.out:
    pop dx
    pop bx
    pop ax
    ret

; =============================================================================
; The PC speaker PWM clip driver (SPEC.md 34.4, Phase 2)
; =============================================================================

; -----------------------------------------------------------------------------
; spk_pcm_op - the speaker row's PCM-out op (SPEC.md 34.2)
;
; in:       AL = verb: 0 start (ES:SI = 8-bit unsigned mono, CX = count,
;           DX = rate Hz; [snd_pcm_n] already validated and the ch2 grant
;           already taken by the router), 1 stop, 2 status
; out:      verb 0: CF = 0 clip completed, CF = 1 aborted with AL = 1 (a
;           mouse click) or 2 (the snd_abort flag); AX clobbered.
;           verb 1: nothing (flags). verb 2: AX = samples emitted so far.
; clobbers: verb 0: AX; others nothing else
;
; Verb 0 is run-to-completion - the exclusive-clip contract (SPEC.md 34.2):
; it returns when the clip ends or aborts, never before.
; -----------------------------------------------------------------------------
spk_pcm_op:
    cmp al, 1
    jb spk_pcm_run              ; verb 0: the clip loop below
    je .stop
    mov ax, [snd_pcm_emitted]   ; verb 2: status
    ret
.stop:
    call snd_stop               ; verb 1: flag the running clip down
    ret

; -----------------------------------------------------------------------------
; spk_pcm_run - play one PWM clip, run-to-completion (SPEC.md 34.4, binding)
;
; in:       ES:SI = samples, CX = count, [snd_pcm_n] = N (74..255),
;           [snd_xlat] built for this N, [snd_btn0] latched, ch2 granted
;           (snd_ch2mode = 2, snd_pcm_busy = 1) by osapi_snd_play
; out:      CF = 0 completed; CF = 1 aborted, AL = 1 click / 2 flag.
;           The release - ports back to tone-idle AND the ownership bytes -
;           happens in spk_pcm_idle's single IF=0 window before returning.
; clobbers: AX
;
; Modulator: ch2 mode 0 lobyte-only (90h -> 43h once), 61h bits 0-1 held
; high; one `out 42h, AL` per sample. Pacer: latch ch0 (0 -> 43h), read two
; bytes, negate -> a free-running 16-bit phase at 1,193,182 Hz; deadlines
; advance by N and the compare is wrap-safe signed subtraction. Every
; latch+read triple sits in its own pushf/cli..popf window (SPEC.md 34.1,
; binding - the 8254 ignores a second latch while one is unread), IF = 1
; between polls. THE RESYNC RULE: a sample late by more than one period
; resets the deadline to now and drops samples - inc [snd_pcm_resync] -
; never burst catch-up writes, so a long interrupt becomes dropped samples,
; never a buzz. snd_pcm_emitted counts every pulse; both counters are the
; Phase 2 gate's instrument, surfaced on the Control Panel caption.
;
; Scheduling: executes on the caller's task with IF=1 throughout, inside
; inc/dec [sch_lock] - the second sanctioned raiser (SPEC.md 7), with
; exactly disk_read's meaning: ticks advance, the BIOS chain feeds the
; floppy motor, sch_account and snd_tick run, sleepers mark ready; only
; involuntary switching pauses. The mouse ISR keeps mouse_x/y/btn fresh,
; which is what the abort baseline reads.
;
; Abort - a click always skips the clip: each emitted sample first FOLDS
; RELEASES INTO THE BASELINE (mov al,[mouse_btn] / and [snd_btn0],al), then
; a bit set now but clear in the baseline aborts. The fold is load-bearing:
; clips start from W_ONCLICK handlers - dispatched on EVT_MDOWN with the
; button still held - so the baseline starts with bit 0 set; the release
; retires it and the next press differs. [snd_abort] (snd_stop /
; snd_release_inst) is checked in the same window. No code in mou_isr.
; -----------------------------------------------------------------------------
spk_pcm_run:
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    inc byte [sch_lock]         ; the clip owns the CPU (SPEC.md 34.4/7)
    cld                         ; lodsb below runs forward

    mov al, [sch_fast]          ; an fsx sub-tick (SPEC.md 53.2.1) divides
    mov [snd_pcm_fast], al      ; ch0's period, and 65536 is the radix this
    or al, al                   ; pacer's deadline arithmetic wraps on - so
    jz .nofast                  ; park it for the clip and put it back at
    call sch_fast_off           ; .out. sch_lock is up either way, so a finer
.nofast:                        ; quantum buys nothing here to give up

    pushf                       ; ch2 -> mode 0 lobyte-only; gate + data
    cli                         ; high. 61h is RMW under IF=0 - the upper
    mov al, 0x90                ; bits belong to the XT's keyboard/parity
    out 0x43, al                ; logic (SPEC.md 34.1)
    in al, 0x61
    or al, 0x03
    out 0x61, al
    popf

    mov bp, [snd_pcm_n]         ; BP = N, PIT-cycles per sample (a value,
                                ; never a [bp] operand - SS != DS)
    mov bx, snd_xlat            ; BX = the rescale table, for xlat
    jcxz .done                  ; zero samples: restore idle and report ok

    pushf                       ; prime the deadline: one atomic ch0
    cli                         ; latch+read triple (SPEC.md 34.1, binding)
    mov al, 0
    out 0x43, al
    in al, 0x40
    mov dl, al
    in al, 0x40
    mov dh, al
    popf
    neg dx                      ; DX = phase; first sample is due at once
    mov di, dx                  ; DI = deadline

.next:
    es lodsb                    ; AL = sample from ES:SI (SPEC.md 20.3)
    xlat                        ; AL -> 1..N-1, pre-clamped by the table
    mov [snd_pcm_smp], al       ; park it across the pacer polls
.wait:
    pushf                       ; } one atomic ch0 latch+read triple
    cli                         ; } (SPEC.md 34.1, binding)
    mov al, 0
    out 0x43, al
    in al, 0x40
    mov dl, al
    in al, 0x40
    mov dh, al
    popf                        ; IF = 1 between polls
    neg dx                      ; DX = phase, incrementing at 1,193,182 Hz
    mov ax, dx
    sub ax, di                  ; wrap-safe signed: phase - deadline
    js .wait                    ; early: poll again
    cmp ax, bp                  ; late by more than one period?
    jbe .emit
    mov di, dx                  ; RESYNC: drop samples, never a burst
    inc word [snd_pcm_resync]
.emit:
    mov al, [snd_pcm_smp]
    out 0x42, al                ; the pulse: AL PIT-cycles low
    add di, bp                  ; next deadline
    inc word [snd_pcm_emitted]
    mov al, [mouse_btn]         ; abort checks, emit path only: FOLD
    and [snd_btn0], al          ; RELEASES INTO THE BASELINE first...
    mov ah, [snd_btn0]
    not ah
    and al, ah                  ; ...then: pressed now, not in the baseline?
    jnz .click
    cmp byte [snd_abort], 0     ; snd_stop / snd_release_inst
    jne .flag
    loop .next

.done:
    call spk_pcm_idle           ; ports + ownership, one IF=0 window
    dec byte [sch_lock]
    clc
    jmp .out
.click:
    call spk_pcm_idle
    dec byte [sch_lock]
    mov al, 1                   ; aborted by a mouse click
    stc
    jmp .out
.flag:
    call spk_pcm_idle
    dec byte [sch_lock]
    mov al, 2                   ; aborted by the flag
    stc
.out:
    pushf                       ; re-arm the sub-tick. The bank is taken
    push ax                     ; UNCONDITIONALLY - CF is this routine's
    mov al, [snd_pcm_fast]      ; result and AL its abort code, and the test
    or al, al                   ; of the parked byte would clobber CF before
    jz .nore                    ; the branch that skips the restore
    call sch_fast_on
.nore:
    pop ax
    popf
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; spk_pcm_idle - restore ch2 to tone-idle and release the PWM grant
;
; in:       nothing
; out:      nothing; ch2 gate low, mode 3 latched, snd_ch2mode = 0,
;           snd_pcm_busy = 0
; clobbers: nothing (flags)
;
; The whole release - the ports AND the ownership record - sits in a single
; pushf/cli..popf window (SPEC.md 34.3, binding: a release is one unit, so
; a tick can never see idle ports with a still-armed mode byte or the
; reverse). The bare mode-3 control word leaves ch2 programmed but silent,
; awaiting a divisor from the next spk_tone; the gate is already low.
; -----------------------------------------------------------------------------
spk_pcm_idle:
    push ax
    pushf
    cli
    in al, 0x61                 ; gate + speaker data low, upper bits kept
    and al, 0xFC
    out 0x61, al
    mov al, 0xB6                ; ch2 back to mode-3 idle (SPEC.md 34.4)
    out 0x43, al
    mov byte [snd_ch2mode], 0
    mov byte [snd_pcm_busy], 0
    popf
    push dx                     ; ...and tell the other tier the machine is
    xor dl, dl                  ; its own again (SPEC.md 34.9). Outside the
    call snd_str_busy           ; IF=0 window: this is a far call into a
    pop dx                      ; driver, not a port write
    pop ax
    ret

; =============================================================================
; The tone tier router (SPEC.md 34.3): one logical channel, single owner,
; priority steal, generation-guarded expiry.
; =============================================================================

; -----------------------------------------------------------------------------
; snd_tone_req - request (or release) the tone channel
;
; in:       AX = frequency Hz (0 = off), CX = duration ticks (0 = until off),
;           DL = priority, DH = requesting instance slot (0xFF = kernel)
; out:      CF = 1 refused (higher-priority owner, bad frequency, or ch2 held
;           by a PWM clip); CF = 0 granted, AL = owner generation
; clobbers: nothing else (flags)
;
; The whole request - priority compare, port programming, record stamp - runs
; in a single pushf/cli..popf window (SPEC.md 34.3, binding): a tick landing
; between the generation stamp and the expiry store could otherwise silence a
; just-granted tone. Tone-off obeys the same priority compare, so background
; audio cannot silence an alert.
; -----------------------------------------------------------------------------
snd_tone_req:
    push bx
    push cx
    push dx
    push si
    pushf
    cli
    cmp byte [snd_live], 0      ; not initialised: .bss is garbage - refuse
    je .refuse
    cmp byte [snd_town_act], 0  ; steal policy: >= takes the channel,
    je .free                    ; < is refused (SPEC.md 34.3)
    cmp dl, [snd_town_pri]
    jb .refuse
.free:
    or ax, ax
    jz .off

    ; --- tone on ------------------------------------------------------------
    cmp byte [snd_ch2mode], 2   ; a PWM clip holds ch2: tone refused (34.1).
    je .refuse                  ; Asked even when the tone is going to an
                                ; OPL2 - the clip owns the machine's one
                                ; exclusive tier and a tone under it would
                                ; sound over a frozen desktop
    push dx
    xor dl, dl                  ; voice 0
    call snd_tone_out           ; the tier's sink: a loaded driver's, else
    pop dx                      ; the speaker's. CF = bad frequency
    jc .refuse                  ; nothing changed: the old owner still sounds
    inc byte [snd_gen]          ; stamp the owner record - same IF=0 window
    mov al, [snd_gen]           ; as the port writes (SPEC.md 34.3)
    mov [snd_town_gen], al
    mov [snd_texp_gen], al
    mov [snd_town_pri], dl
    mov [snd_town_inst], dh
    mov [snd_town_exp], cx
    mov byte [snd_town_act], 1
    mov byte [snd_ch2mode], 1
    popf
    clc
    jmp .out

.off:                           ; --- tone off: release the channel ----------
    call snd_town_off
    inc byte [snd_gen]          ; a release updates the record too (34.3)
    mov al, [snd_gen]
    popf
    clc
    jmp .out

.refuse:
    popf
    stc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; snd_town_off - silence the owning tone and free the channel (internal)
;
; in:  IF = 0 (the caller's grant/release window - this is part of it)
; out: nothing; owner inactive, expiry cleared, ch2 mode free if it was tone
; clobbers: nothing (flags)
; -----------------------------------------------------------------------------
snd_town_off:
    push ax
    push bx
    push dx
    push si
    cmp byte [snd_town_act], 0
    je .clear
    xor ax, ax
    xor dl, dl
    call snd_tone_out           ; whichever sink granted it, silences it
    mov byte [snd_ch2mode], 0
.clear:
    mov byte [snd_town_act], 0
    mov word [snd_town_exp], 0
    pop si
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; snd_tone_out - the tone tier's current sink (SPEC.md 34.2/51.4)
;
; in:       AX = frequency Hz (0 = off), DL = voice
; out:      CF = 0 done, CF = 1 unsupported voice/frequency
; clobbers: nothing else (flags)
;
; One test decides where a tone goes. A loaded driver that publishes DSV_TONE
; TAKES the tier - which is what an OPL2 does, and the reason it does is that
; an FM note costs two register writes and then no CPU at all, so tones move
; off the speaker and leave it for the exclusive-clip tier that has nowhere
; else to go (SPEC.md 34.4). With no driver, or one that publishes no tone op
; (a Sound Blaster), the answer is the speaker exactly as before.
;
; Reached from snd_tick's expiry path, so this can run INSIDE IRQ0 at IF=0.
; That is sanctioned and bounded: the driver's key-off is one register write
; (SPEC.md 8.2).
; -----------------------------------------------------------------------------
snd_tone_out:
    call snd_rt_card
    jc spk_tone                 ; tail call: the speaker, and CF is its own
    push bp
    mov bp, [drv_svc+DSV_TONE]
    call drv_svc_call
    pop bp                      ; POP leaves the driver's CF alone
    ret

; -----------------------------------------------------------------------------
; snd_rt_card - do tones go to the driver's hardware right now? (SPEC.md 34.8)
; out:      CF = 0 yes; CF = 1 no, the speaker has the tier
; clobbers: nothing (flags)
;
; One answer, so the router, osapi_snd_caps and the Control Panel's Sound page
; cannot disagree about where a tone is about to come out. SND_RT_CARD with no
; driver FALLS BACK to the speaker rather than going silent - the page refuses
; that selection and says why, but a setting kept on a disk that later moves
; to a machine with no card must not take the beep away with it.
; -----------------------------------------------------------------------------
snd_rt_card:
    cmp byte [snd_route], SND_RT_SPK
    je .no
    cmp word [drv_svc+DSV_TONE], 0
    je .no
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; snd_beep - the kernel UI beep (SPEC.md 34.3): menu error, refused clicks
;
; in:       nothing
; out:      nothing (a refusal by a higher-priority owner is fine - the alert
;           that outranks a beep is already sounding)
; clobbers: nothing (flags)
; -----------------------------------------------------------------------------
snd_beep:
    push ax
    push cx
    push dx
    cmp byte [snd_live], 0
    je .out
    mov ax, 880                 ; 3 ticks of 880 Hz at kernel priority
    mov cx, 3
    mov dx, 0FF00h | SND_PRI_UI ; DH = 0xFF (kernel), DL = priority
    call snd_tone_req
.out:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; snd_stop - abort the running PCM_EXCL clip, if any (SPEC.md 34.4)
;
; in:       nothing
; out:      nothing
; clobbers: nothing (flags)
;
; Sets [snd_abort]; the clip loop sees it on its next emitted sample and
; exits err 5. A single byte store: safe from any context, including under
; IF=0 (snd_release_inst calls this pattern inline).
; -----------------------------------------------------------------------------
snd_stop:
    cmp byte [snd_pcm_busy], 0
    je .out
    mov byte [snd_abort], 1
.out:
    ret

; -----------------------------------------------------------------------------
; snd_tick - per-tick sound duties (SPEC.md 8.2/34, Phase 1: tone expiry)
;
; Called from sch_isr immediately after sch_account - a leaf, never part of
; the sch_isr -> sch_switch fall-through. Entered at IF=0 with DS=KERNEL_SEG
; and a complete saved frame, so AX/CX/DX are free. First instruction is the
; snd_live gate (SPEC.md 34.7, binding): until snd_init publishes, every
; .bss byte below is garbage and this routine reads none of it.
;
; Duty: count a duration-limited tone down and, at zero WITH the grant's
; generation still current, silence it on the sink it is sounding on. The
; generation guard means a tick can never silence a tone granted after the
; one whose expiry it is retiring (SPEC.md 34.3).
; -----------------------------------------------------------------------------
snd_tick:
    cmp byte [snd_live], 0      ; the boot gate - nothing else is readable
    je .done
    cmp word [drv_svc+DSV_TICK], 0
    je .tone                    ; a loaded driver may want the tick too; it
    push ax                     ; runs here, inside IRQ0, at IF=0
    push bp
    mov bp, [drv_svc+DSV_TICK]
    call drv_svc_call
    pop bp
    pop ax
.tone:
    cmp word [snd_town_exp], 0
    je .done
    dec word [snd_town_exp]
    jnz .done
    mov al, [snd_texp_gen]      ; expiry generation-guarded (SPEC.md 34.3)
    cmp al, [snd_town_gen]
    jne .done
    push bx
    push si
    call snd_town_off           ; IF is already 0: the tick IS the window
    pop si
    pop bx
.done:
    ret

; -----------------------------------------------------------------------------
; snd_release_inst - force-release every sound grant an instance holds
;
; in:       AL = instance slot
; out:      nothing
; clobbers: nothing (flags)
;
; Called from both SPEC.md 29.4 teardown paths (app_close_win's task-less
; path and inst_task_die). Releases the tone channel and flags down a clip
; the instance owns - the whole of what a speaker-only layer can hold. The
; release ignores priority: the owner is gone.
; -----------------------------------------------------------------------------
snd_release_inst:
    pushf
    cli
    cmp byte [snd_live], 0
    je .done
    cmp word [drv_svc+DSV_RELINST], 0
    je .spk                     ; a loaded driver's grants go first: its FM
    push ax                     ; channels and streams are stamped with the
    push bp                     ; same instance slot ours are (SPEC.md 34.3)
    push dx
    mov bp, [drv_svc+DSV_RELINST]
    call drv_svc_call
    pop dx
    pop bp
    pop ax
.spk:
    cmp byte [snd_pcm_busy], 0  ; a clip owned by the dying instance stops
    je .tone                    ; at its next emitted sample (SPEC.md 34.4)
    cmp al, [snd_pcm_inst]
    jne .tone
    mov byte [snd_abort], 1
.tone:
    cmp byte [snd_town_act], 0
    je .done
    cmp al, [snd_town_inst]
    jne .done
    push bx
    push si
    call snd_town_off
    pop si
    pop bx
    inc byte [snd_gen]          ; a release updates the record (SPEC.md 34.3)
.done:
    popf
    ret

; -----------------------------------------------------------------------------
; snd_release_rec - snd_release_inst for a record pointer (internal helper
;                   for the two SPEC.md 29.4 teardown call sites)
; in:  DI = instance record ptr
; out: nothing
; clobbers: nothing (flags)
; -----------------------------------------------------------------------------
snd_release_rec:
    push ax
    push cx
    mov ax, di
    sub ax, inst_tab
    mov cl, 5                   ; I_RECSZ = 32
    shr ax, cl
    call snd_release_inst       ; in: AL = instance slot
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; snd_disp_set - stamp the instance a window callback is dispatched for
;
; in:  DI = instance record ptr, or 0 = none (kernel)
; out: nothing; [snd_inst] = the instance slot (0xFF for none) in the low
;      byte, the stamping task in the high byte
; clobbers: nothing (flags)
;
; Called at the same dispatch sites that bill I_CYC (SPEC.md 34.3): the site
; brackets the callback with `push word [snd_inst]` / call snd_disp_set /
; callback / `pop word [snd_inst]`, so nested dispatches (a click handler
; that triggers repaints) restore the outer stamp correctly. Sound grants
; made inside the callback are stamped with this slot, which is what lets
; snd_release_inst find them at teardown.
; -----------------------------------------------------------------------------
snd_disp_set:
    push ax
    push cx
    mov ax, di
    or ax, ax
    jz .none
    sub ax, inst_tab
    mov cl, 5                   ; I_RECSZ = 32
    shr ax, cl
    jmp .store
.none:
    mov al, 0xFF
.store:
    mov ah, [sch_cur]           ; ...and WHICH TASK stamped it, so that a
    mov [snd_inst], ax          ; worker pre-empting this callback mid-flight
    pop cx                      ; cannot inherit the stamp (SPEC.md 34.3)
    pop ax
    ret

; =============================================================================
; Boot and teardown (SPEC.md 34.7)
; =============================================================================

; -----------------------------------------------------------------------------
; snd_init - initialise the sound layer (kmain, after drv_init)
;
; in:       nothing
; out:      nothing
; clobbers: flags
;
; Saves the boot state of 61h bits 0-1, stores every .bss byte the layer
; reads (nothing clears .bss at boot, SPEC.md 8), runs the probes the driver
; table declares (the speaker's presence is initialised data; the OPL2 row's
; probe fully initialises the chip before reporting present, SPEC.md 34.2),
; and sets snd_live LAST: the publish that arms snd_tick and the public
; entry points (SPEC.md 34.7, the 29.2 publish-last idiom).

section .ovl                    ; the boot overlay (SPEC.md 2.5): saving the boot
                                ; 61h bits and publishing snd_live happens
                                ; once. snd_unhook below is the shutdown
                                ; path and stays resident. Nothing here
                                ; calls out of the overlay at all
; -----------------------------------------------------------------------------
snd_init:
    push ax
    push cx

    in al, 0x61                 ; the boot state of the gate/data bits -
    and al, 0x03                ; snd_unhook restores exactly these
    mov [snd_61boot], al

    mov byte [snd_ch2mode], 0
    mov word [snd_inst], 0xFFFF ; no stamp, owned by no task: a word because
    mov byte [snd_gen], 0
    mov byte [snd_town_act], 0
    mov byte [snd_town_inst], 0xFF
    mov byte [snd_town_pri], 0
    mov byte [snd_town_gen], 0
    mov byte [snd_texp_gen], 0
    mov word [snd_town_exp], 0

    mov byte [snd_pcm_busy], 0  ; the PWM clip state (SPEC.md 34.4); the
    mov byte [snd_abort], 0     ; 256-byte xlat table needs no init - it is
    mov byte [snd_btn0], 0      ; rebuilt before every clip
    mov byte [snd_pcm_inst], 0xFF
    mov byte [snd_pcm_smp], 0
    mov word [snd_pcm_n], 149   ; the default-rate divisor (8,008 Hz)
    mov word [snd_pcm_emitted], 0
    mov word [snd_pcm_resync], 0

    mov byte [snd_live], 1      ; publish-last (SPEC.md 34.7, binding)

    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; snd_unhook - quiesce the sound hardware for reboot (SPEC.md 34.7)
;
; in:       nothing
; out:      nothing
; clobbers: nothing (flags)
;
; Called by sched_unhook beside mouse_unhook: 61h bits 0-1 go back to their
; saved boot state (upper bits preserved), leaving ch2 quiescent. That is the
; whole of it - the speaker owns no vector, no IRQ and no DMA channel.

section .text
; -----------------------------------------------------------------------------
snd_unhook:
    push ax
    cmp byte [snd_live], 0      ; never initialised: nothing to restore
    je .out
    pushf
    cli
    in al, 0x61
    and al, 0xFC
    or al, [snd_61boot]
    out 0x61, al
    popf
.out:
    pop ax
    ret

; =============================================================================
; The three API slot targets (SPEC.md 20.3) - caps, tone and the exclusive
; clip. There is one sink, so there is nothing to select and nothing to probe.
; =============================================================================

; -----------------------------------------------------------------------------
; osapi_snd_caps - what can this machine's sound hardware do?
;
; out:      AX = capability bits (SND_CAP_*), BL = 0 (the speaker; the value
;           is kept so a package written against the old ABI still reads
;           "not FM"), DX = 1 (bit 0 = the speaker is present)
; clobbers: nothing else (flags)
;
; A constant answer since the sound-card tiers were retired: every machine
; this OS boots on has a PC speaker and nothing else it can drive.
; -----------------------------------------------------------------------------
osapi_snd_caps:
    mov ax, SND_CAP_TONE | SND_CAP_PCM_EXCL
    or ax, [drv_svc+DSV_CAPS]   ; ...plus whatever a loaded driver adds
    mov bl, 0                   ; BL = where a tone would go RIGHT NOW: 0 the
    call snd_rt_card            ; speaker, 1 the driver's own sink - the
    jc .route                   ; setting and the hardware, folded (34.8)
    mov bl, 1
.route:
    mov dx, 1                   ; DX bit 0 = the speaker, always present
    cmp word [drv_svc+DSV_CAPS], 0
    je .out
    or dx, 2                    ; bit 1 = a sound driver is loaded
.out:
    ret

; -----------------------------------------------------------------------------
; osapi_snd_fm - the public FM request (slot 0x00F8, SPEC.md 20.3/34.2)
;
; in:       AL = verb, and the verb's own registers. Verb 2's patch pointer
;           has ALREADY been staged into the kernel by the slot's X stub, so
;           what reaches the driver is ES:SI in kernel memory.
; out:      CF = 1 no FM sink, or the driver refused
; clobbers: nothing else (flags)
;
; The requesting instance is stamped here, not in the driver: snd_req_inst
; reads [snd_inst] and the running task's T_INST, both of which are kernel
; state, and a driver that had to ask for them would need an API slot of its
; own to do it. DH is the grant-stamping rule (SPEC.md 34.3) crossing the
; segment boundary as an argument.
; -----------------------------------------------------------------------------
osapi_snd_fm_x:
    cmp al, 2
    jne osapi_snd_fm            ; every verb but patch-load carries no pointer
    push cx                     ; verb 2: stage the caller's 11 bytes into
    push si                     ; the kernel, the dsk_get_dir idiom - the
    push di                     ; driver runs with DS = its own segment and
    mov di, snd_patch           ; cannot reach the caller's
    mov cx, 11
.stage:
    mov ah, [es:si]
    mov [di], ah
    inc si
    inc di
    loop .stage
    mov si, snd_patch
    pop di
    call osapi_snd_fm
    pop si
    pop cx
    ret

osapi_snd_fm:
    push bp
    push dx
    push es
    push ds
    pop es                      ; the driver reads a staged patch through ES
    call snd_req_inst           ; DH = the requester
    mov bp, [drv_svc+DSV_FM]
    call drv_svc_call
    pop es
    pop dx
    pop bp
    ret

; -----------------------------------------------------------------------------
; osapi_snd_stream - the public stream request (slot 0x0100, SPEC.md 34.5)
; in:       AL = verb, and the verb's own registers
; out:      AX = the verb's answer; DX = bytes consumed ON VERB 3 ONLY;
;           CF = 1 no streaming sink, or the driver refused
; clobbers: nothing else (flags)
;
; DX IS BOTH AN INPUT AND AN OUTPUT OF THIS SLOT, on different verbs, and the
; stub has to know which. On the two OPEN verbs it is the RATE coming in, and
; `snd_req_inst` writes DH - the top half of a 4,000..44,100 Hz rate - so the
; kernel banks it into BX before stamping. On verb 3 it is the CONSUMED COUNT
; going out, and banking it means restoring it, which threw the answer away:
; every poller got its own stale DX back, read it as "consumed < total", and
; sat on a finished stream forever saying PLAYING. Recorder is what found it
; (sbtest showed the same garbage in its c: field and it read as plausible).
;
; So verb 3 takes a path that never pushes DX at all, rather than a test
; after the call - a `cmp` there would eat the CF the call just answered
; with, and the verb is known before the call anyway.
; -----------------------------------------------------------------------------
osapi_snd_stream:
    cmp word [drv_svc+DSV_STREAM], 0
    jne .have
    mov ax, 4                   ; NO STREAMING SINK, said in the stream verbs'
    stc                         ; own vocabulary. drv_svc_call's generic
    ret                         ; refusal answers AX = 0 - which is a fine
                                ; "nothing happened" for the FM and tone
                                ; slots, whose contract is CF alone, and is
                                ; exactly WRONG here, because AX = 0 is how
                                ; every stream verb says OK. A driverless
                                ; machine handed Recorder a phantom grant
                                ; that way: verb 7 reported success, SI was
                                ; never written, and the app staged a demo
                                ; into a buffer that did not exist.
.have:
    push bx
    push bp
    cmp al, 3
    je .status
    push dx                     ; every other verb gets its DX back
    mov bx, es                  ; BX = the CALLER'S SEGMENT, which verbs 5 and
                                ; 6 copy to and from: the X stub put the
                                ; caller's DS in ES, and ES is KERNEL_SEG by
                                ; the time the driver runs (drv_call), so BX
                                ; is the only way it can reach them
    cmp al, 0
    je .rate
    cmp al, 4
    jne .go
.rate:                          ; ...except on the two OPEN verbs, where BX is
    mov bx, dx                  ; unused and the RATE needs somewhere to be
                                ; (see above). The kernel is what stamps,
                                ; so the kernel is what banks DX
                                ; (SPEC.md 34.5).
.go:
    call snd_req_inst           ; DH = the requester
    mov bp, [drv_svc+DSV_STREAM]
    call drv_svc_call
    pop dx
    pop bp
    pop bx
    ret
.status:                        ; verb 3: DX is the ANSWER, so nothing banks
    mov bx, es                  ; it and nothing puts it back
    call snd_req_inst
    mov bp, [drv_svc+DSV_STREAM]
    call drv_svc_call
    pop bp
    pop bx
    ret

; -----------------------------------------------------------------------------
; osapi_snd_tone - the public tone request (slot 0x00E8, SPEC.md 20.3)
;
; in:       AX = frequency Hz (0 = off), CX = duration ticks (0 = until off),
;           DL = priority
; out:      CF = 1 refused (higher-priority owner), else CF = 0 and
;           AL = owner generation
; clobbers: nothing else (flags)
;
; The requesting instance is the one whose window callback is being
; dispatched ([snd_inst], stamped at the I_CYC billing sites, SPEC.md 34.3);
; outside a dispatch it falls back to the running task's T_INST, so a
; task-owned caller is stamped correctly too. 0xFF = kernel.
; -----------------------------------------------------------------------------
osapi_snd_tone:
    push dx
    call snd_req_inst           ; DH = the requesting instance (SPEC.md 34.3)
    call snd_tone_req
    pop dx
    ret

; -----------------------------------------------------------------------------
; snd_req_inst - who is asking? (SPEC.md 34.3, the grant-stamping rule)
;
; out:      DH = requesting instance slot: the dispatched callback's stamp
;           ([snd_inst], set at the I_CYC billing sites), else the running
;           task's T_INST. 0xFF = kernel.
; clobbers: nothing else (flags)
;
; The stamp is only the answer for a caller running INSIDE that callback, and
; the callback runs on one task. A worker that pre-empts it is a different
; task, and honouring the stamp there bills its tone to - and releases it at
; the teardown of - whichever app happened to be dispatching. So the stamp
; carries the task that wrote it and is spent only by that task; everyone
; else falls through to their own T_INST. Arkanoid's worker calls SND_TONE
; while UI callbacks run, which is what makes this reachable rather than
; theoretical.
;
; **The body is inst_caller's now** (instance.inc). Nothing about this
; contract changed - the reasoning above is still exactly why it is written
; the way it is - but the question stopped being the sound tier's alone when
; the file API needed the same answer to stand the volume in the calling
; app's own directory (SPEC.md 19.2.1). Two copies of "who is asking?" is
; the kind of pair that drifts silently, so there is one.
; -----------------------------------------------------------------------------
snd_req_inst:
    jmp inst_caller

; -----------------------------------------------------------------------------
; osapi_snd_play - PCM_EXCL clip, run-to-completion (slot 0x00F0, SPEC.md
;                  20.3/34.4 - live from Phase 2)
;
; in:       ES:SI = 8-bit unsigned mono samples, CX = count (<= 65535),
;           DX = rate Hz - N = 1193182/rate must land in 74..255
; out:      AX = 0 ok / 1 busy (incl. a PCM_BG stream open) / 2 rate /
;           3 disabled-by-user / 4 no sink / 5 aborted; CF mirrors AX != 0.
;           ES restored per SPEC.md 1; BLOCKS for the whole clip.
; clobbers: nothing else (flags)
;
; Takes ES:SI, not DS:SI, so a clip staged in SND_SEG plays in place;
; callers keeping clips in their own image just set ES = DS. A rate whose
; N falls outside 74..255 is ERR 2, NEVER A SILENT CLAMP (SPEC.md 34.4) -
; a clamped clip would play slow and flat; refusal is honest, resampling
; is the caller's job. The requesting instance is the dispatched
; callback's stamp, else the running task's (the osapi_snd_tone rule).
;
; The grant - busy test, tone steal, ch2 mode (speaker route only, SPEC.md
; 34.3), generation, the abort and click-baseline latches, the counter
; reset - is one pushf/cli..popf window (SPEC.md 34.3, binding). On a
; click-abort the aborting EVT_MDOWN (and its EVT_MUP) are drained from
; the event queue before err 5 returns, so the skip gesture cannot fire a
; menu, close box or icon under the cursor; everything queued while the
; screen was frozen predates the abort and drains with it. The drain sits
; inside this routine's own sch_lock raise, held from before the op call:
; the guarantee is structural, not an artifact of the caller being the
; only queue consumer (SPEC.md 34.4).
; -----------------------------------------------------------------------------
osapi_snd_play:
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    cmp byte [snd_live], 0      ; not initialised: nothing can sound
    je .e4
    cmp byte [snd_excl_ok], 0   ; the user said no (SPEC.md 34.4)
    je .e3

    ; rate -> divisor N = 1193182/rate, hard range 74..255 (SPEC.md 34.4).
    ; The range checks double as the div guard: a tiny rate would overflow
    ; the 32/16 divide (quotient > 16 bits traps to int 0).
    cmp dx, 4679                ; N <= 255  <=>  rate >= 4,679 Hz
    jb .e2
    cmp dx, 16124               ; N >= 74   <=>  rate <= 16,124 Hz
    ja .e2
    push cx
    push dx
    mov cx, dx
    mov dx, 0x0012              ; 1193182 = 0x1234DE
    mov ax, 0x34DE
    div cx                      ; AX = N, 74..255 by the checks above
    mov [snd_pcm_n], ax
    pop dx
    pop cx

    push dx                     ; requesting instance (SPEC.md 34.3) - the one
    call snd_req_inst           ; routine, so the task qualification is not
    mov bh, dh                  ; open-coded a second time and cannot drift
    pop dx

    mov dl, 1                   ; the OTHER tier, asked BEFORE the grant
    call snd_str_busy           ; window opens - so this refusal exits by
    jnc .grant                  ; the plain path, with no popf owed
    mov ax, 1                   ; busy: a stream is open (SPEC.md 34.9)
    jmp .err
.grant:
    pushf                       ; --- the grant window (SPEC.md 34.3) ------
    cli
    cmp byte [snd_pcm_busy], 0  ; one clip at a time, no queue
    jne .busyw
    cmp byte [snd_town_act], 0  ; the clip and the tone share channel 2
    je .own
    mov al, SND_PRI_PKG         ; clip priority: the package default;
    cmp bh, 0xFF                ; kernel callers steal at UI priority
    jne .pcmp
    mov al, SND_PRI_UI
.pcmp:
    cmp al, [snd_town_pri]      ; PWM start steals + silences a lower-
    jb .busyw                   ; priority tone; a higher one refuses the
    call snd_town_off           ; clip (SPEC.md 34.1/34.3)
.own:
    inc byte [snd_gen]          ; ch2 mode, generation and the silencing
    mov byte [snd_ch2mode], 2   ; above are one unit (SPEC.md 34.3)
    mov byte [snd_pcm_busy], 1
    mov byte [snd_abort], 0
    mov [snd_pcm_inst], bh
    mov al, [mouse_btn]         ; the click-abort baseline (SPEC.md 34.4)
    mov [snd_btn0], al
    mov word [snd_pcm_emitted], 0
    mov word [snd_pcm_resync], 0
    popf                        ; --- granted ------------------------------

    mov ax, [snd_pcm_n]         ; build the per-clip rescale table: far
    call snd_xlat_build         ; code, ~8ms, non-destructive (SPEC.md 34.4)

    inc byte [sch_lock]         ; the router's own raise, held across the
                                ; op AND the abort drain: without it a tick
                                ; between the op's internal release and the
                                ; drain could switch to a task that pops
                                ; the aborting click first (SPEC.md 34.4)
    mov al, 0                   ; verb 0: run the clip to completion; the
    call spk_pcm_op             ; op's epilogue releases ch2 + busy. Out:
    jc .aborted                 ; CF=1 aborted, AL = 1 click / 2 flag
    dec byte [sch_lock]
    xor ax, ax
    clc
    jmp .out

.aborted:
    cmp al, 1
    jne .e5                     ; flag abort: nothing queued to drain
.drain:                         ; click abort: drain the aborting mouse
    mov di, snd_evtmp           ; events - and everything queued while the
    call evq_pop                ; screen was frozen - before returning
    jnc .drain                  ; (SPEC.md 34.4, binding)
.e5:
    dec byte [sch_lock]         ; the router's raise ends here on every
    mov ax, 5                   ; abort path (drained or flag)
    jmp .err
.busyw:
    popf
    mov ax, 1
    jmp .err
.e2:
    mov ax, 2
    jmp .err
.e3:
    mov ax, 3
    jmp .err
.e4:
    mov ax, 4
.err:
    stc
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; The PWM rescale-table builder (SPEC.md 34.4): cold per-clip work. It used
; to live in .fartext behind a shim; SPEC.md 33 records why that mechanism
; was retired, and it is an ordinary near proc now.
; =============================================================================

section .text

; -----------------------------------------------------------------------------
; snd_str_busy - the two tiers that must never overlap (SPEC.md 34.9)
;
; in:  DL = 1 a PCM_EXCL clip is about to start, 0 it has finished
; out: CF = 1 a background stream is open, so the clip must be refused
; clobbers: nothing (flags)
;
; **The kernel is the only side that can see both.** An exclusive clip raises
; sch_lock for its whole duration, which would freeze a loaded driver's refill
; worker and force its stream through the underrun path; and a stream's block
; IRQ arriving mid-clip is a DMA transfer the clip's busy loop cannot pace
; around. But the clip's state is ours and the stream's is the DRIVER'S, so
; neither module can answer alone. Stream verb 8 is the question and the
; answer in one call: it takes our half and returns theirs.
;
; With no driver loaded there is no stream to collide with, and drv_svc_call
; says so by refusing - which is the same answer, arrived at for free.
; -----------------------------------------------------------------------------
snd_str_busy:
    push ax
    push bp
    mov al, 8
    mov bp, [drv_svc+DSV_STREAM]
    call drv_svc_call
    jc .free                    ; no driver, no stream
    or ax, ax
    jnz .busy
.free:
    pop bp
    pop ax
    clc
    ret
.busy:
    pop bp
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; snd_xlat_build - build the 256-byte PWM rescale table (far body)
;
; in:       AX = N, the clip divisor (74..255, validated by the caller)
; out:      snd_xlat[s] = 1 + s*(N-2)/255 for s = 0..255
; clobbers: nothing (flags)
;
; Maps 8-bit unsigned samples onto pulse widths 1..N-1: never 0 (a mode-0
; count of 0 means 65536) and never N or more (the pulse must end inside
; its own period). ~8ms once per clip; non-destructive - the caller's clip
; buffer is never modified, so replay and re-rate work (SPEC.md 34.4;
; per-sample mul scaling is rejected there, the table saves ~70
; cycles/sample).
; -----------------------------------------------------------------------------
snd_xlat_build:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, ax
    sub bx, 2                   ; BL = N-2 (N <= 255, so it fits a byte)
    mov di, snd_xlat
    xor si, si                  ; SI = s, 0..255
.s:
    mov ax, si
    mul bl                      ; AX = s * (N-2)   (<= 255*253 = 64515)
    mov cl, 255
    div cl                      ; AL = AX/255 (<= 253), AH = remainder
    inc al                      ; 1..N-1
    mov [di], al
    inc di
    inc si
    cmp si, 256
    jb .s
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

section .text

; =============================================================================
; state (SPEC.md 34.7) - all stored by snd_init, unreadable until snd_live
; =============================================================================
section .bss

snd_61boot:    resb 1           ; boot state of 61h bits 0-1 (34.1)
snd_ch2mode:   resb 1           ; PIT ch2 owner mode: 0 free / 1 tone / 2 PWM
snd_gen:       resb 1           ; grant generation counter (34.3)
snd_town_act:  resb 1           ; tone owner record: 1 = channel owned
snd_town_inst: resb 1           ;   owning instance slot (0xFF = kernel)
snd_town_pri:  resb 1           ;   owner priority
snd_town_gen:  resb 1           ;   generation stamped at grant
snd_texp_gen:  resb 1           ;   generation the pending expiry belongs to
snd_town_exp:  resw 1           ;   remaining ticks, 0 = no self-expiry
snd_inst:      resw 1           ; low byte: instance slot being dispatched
                                ; (0xFF = none). HIGH byte: the task that
                                ; stamped it, so snd_req_inst can tell a
                                ; caller inside that callback from a worker
                                ; that pre-empted it. A word for both reasons:
                                ; the dispatch sites push/pop it whole

snd_xlat:      resb 256         ; per-clip PWM rescale table (34.4), built
                                ; by snd_xlat_build before every clip -
                                ; write-before-read, needs no boot init
snd_btn0:      resb 1           ; click-abort baseline; releases fold in
snd_abort:     resb 1           ; set by snd_stop / snd_release_inst
snd_pcm_busy:  resb 1           ; a PCM_EXCL clip is running (34.3)
snd_pcm_inst:  resb 1           ;   the instance that owns it (0xFF kernel)
snd_pcm_smp:   resb 1           ;   the sample parked across pacer polls
snd_pcm_fast:  resb 1           ;   the fsx sub-tick parked across the clip
                                ;   (SPEC.md 53.2.1); written before every
                                ;   read, so an unzeroed .bss cannot bite
snd_pcm_n:     resw 1           ;   the clip divisor N (74..255)
snd_pcm_emitted: resw 1         ; debug counters (34.4), reset per clip:
snd_pcm_resync:  resw 1         ;   pulses emitted / deadline resyncs
snd_patch:     resb 12          ; a package's 11-byte FM patch, staged into
                                ; the kernel so the driver can read it (34.2)
snd_evtmp:     resb 8           ; click-abort event drain scratch (evq_pop
                                ; copies into DS memory)

section .text
