; =============================================================================
; os8088 - snd.inc
;
; The sound core (SPEC.md 34): driver table, capability bits, per-tier route
; selection, the tone tier's owner/priority/generation router, the PC speaker
; driver (tone + PWM exclusive clips), snd_tick (the one scheduler
; touch-point), and the five os8088 API slot targets at 0x0078..0x0088.
; Phases 1-2 of docs/SOUND-PLAN.md: tones and speaker PCM; Phase 3 wires
; the FM slot through to sndfm.inc's OPL2 driver; Phases 4-5 hand the
; STREAM slot to sndsb.inc's Sound Blaster driver (playback + recording).
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
SND_CAP_TONE     equ 01h        ; square voice(s): freq on/off
SND_CAP_FM       equ 02h        ; 9-ch 2-op patch + note-on/off (OPL2 model)
SND_CAP_PCM_BG   equ 04h        ; self-clocked block PCM out - background-safe
SND_CAP_PCM_EXCL equ 08h        ; CPU-paced PCM out - exclusive clip contract
SND_CAP_PCM_IN   equ 10h        ; block PCM input (recording)

; --- driver table record (SPEC.md 34.2, the inst_kinds idiom) -----------------
SDRV_CAPS   equ 0               ; db: capability bits the hardware class has
SDRV_FLAGS  equ 1               ; db: bit0 = present (published LAST, 29.2)
SDRV_TONE   equ 2               ; dw: near ptr, tone op
SDRV_NOTE   equ 4               ; dw: near ptr, FM op       (0 = unsupported)
SDRV_PCM    equ 6               ; dw: near ptr, PCM-out op  (0 = unsupported)
SDRV_PCMIN  equ 8               ; dw: near ptr, PCM-in op   (0 = unsupported)
SDRV_PROBE  equ 10              ; dw: near ptr to the probe's FARSHIM stub in
                                ;     .text (0 = presence comes from the
                                ;     row's initialised data, not a probe)
SDRV_NAME   equ 12              ; dw: .text name string
SDRV_CFG    equ 14              ; dw: reserved (SB: packed base/IRQ/DMA)
SDRV_SIZE   equ 16

DRV_SPK     equ 0               ; row indices (SPEC.md 34.2)
DRV_OPL     equ 1
DRV_SB      equ 2
SND_NDRV    equ 3

SND_TIER_TONE equ 0             ; snd_route indices, one override per tier
SND_TIER_FM   equ 1
SND_TIER_PCM  equ 2

SND_PRI_UI  equ 0C0h            ; kernel UI beeps (SPEC.md 34.3)
SND_PRI_PKG equ 040h            ; documented package default priority

; =============================================================================
; Initialised .text data (SPEC.md 34.7): everything snd_tick or an early
; caller could read before snd_init has stored the .bss state.
; =============================================================================

snd_live:      db 0             ; the boot gate: set LAST by snd_init (34.7)
snd_excl_ok:   db 1             ; user policy: exclusive clips allowed (34.4)
snd_route:     db 0FFh, 0FFh, 0FFh  ; per-tier override, 0FFh = auto (34.2)
snd_pref_tone: db DRV_OPL, DRV_SPK, 0FFh   ; first PRESENT driver wins
snd_pref_pcm:  db DRV_SB, DRV_SPK, 0FFh

snd_s_spk:     db 'Speaker', 0
snd_s_opl:     db 'AdLib', 0
snd_s_sb:      db 'Sound Blaster', 0

snd_drv:                        ; row 0: PC speaker - always present, as data
    db SND_CAP_TONE | SND_CAP_PCM_EXCL
    db 1                        ; present
    dw spk_tone                 ; tone op
    dw 0                        ; no FM
    dw spk_pcm_op               ; PCM-out op (SPEC.md 34.4, Phase 2)
    dw 0                        ; no PCM-in
    dw 0                        ; no probe needed
    dw snd_s_spk
    dw 0
                                ; row 1: OPL2 (sndfm.inc, Phase 3)
    db SND_CAP_TONE | SND_CAP_FM
    db 0                        ; absent until opl_probe publishes (34.2)
    dw opl_tone                 ; tone op: the reserved channel 8 (34.3)
    dw opl_fm_op                ; FM op - slot 0x0084's live body
    dw 0, 0                     ; no PCM out / in
    dw opl_probe                ; the timer-flag dance, far behind its shim
    dw snd_s_opl
    dw 0
                                ; row 2: Sound Blaster (sndsb.inc, Phase 4-5)
    db SND_CAP_PCM_BG | SND_CAP_PCM_IN
                                ; no PCM_EXCL op, so exclusive clips keep
                                ; routing to the speaker (snd_pcm_row
                                ; wants a PCM op)
    db 0                        ; absent until sbl_probe publishes (34.2)
    dw 0, 0, 0, 0               ; streams go through slot 0x0088, not the
                                ; row ops (SPEC.md 34.5)
    dw sbl_probe                ; reset scan + E1h gate, far behind its shim
    dw snd_s_sb
    dw 0                        ; SDRV_CFG: the probe parks the base here

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
    pop ax
    ret

; =============================================================================
; Route selection (SPEC.md 34.2): policy is data, not code
; =============================================================================

; -----------------------------------------------------------------------------
; snd_drv_row - driver index to table row
; in:  BL = driver index
; out: SI = snd_drv + BL*16
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
snd_drv_row:
    push ax
    push cx
    mov al, bl
    mov ah, 0
    mov cl, 4
    shl ax, cl
    add ax, snd_drv
    mov si, ax
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; snd_tone_route - resolve the tone tier's sink
;
; out: BL = driver index, SI = its snd_drv row
; clobbers: nothing else (flags)
;
; The snd_route override wins if it names a PRESENT driver; otherwise the
; preference list, first present driver. The speaker row is present as
; initialised data, so resolution cannot fail.
; -----------------------------------------------------------------------------
snd_tone_route:
    push ax
    push cx
    mov bl, [snd_route + SND_TIER_TONE]
    cmp bl, 0FFh
    je .auto
    call snd_drv_row
    test byte [si+SDRV_FLAGS], 1
    jnz .done                   ; pinned to a present sink: use it
.auto:
    mov si, snd_pref_tone
.scan:
    mov bl, [si]
    inc si
    cmp bl, 0FFh
    je .fall                    ; list exhausted (unreachable: speaker row)
    push si
    call snd_drv_row
    test byte [si+SDRV_FLAGS], 1
    pop si
    jz .scan
    call snd_drv_row            ; found: SI = the winning row
    jmp .done
.fall:
    mov bl, DRV_SPK
    call snd_drv_row
.done:
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; snd_pcm_route - resolve the PCM_EXCL tier's sink (SPEC.md 34.2)
;
; out: CF = 0, BL = driver index, DI = its snd_drv row - the first PRESENT
;      driver with SND_CAP_PCM_EXCL and a PCM op, the snd_route override
;      checked before the preference list; CF = 1 = no PCM sink at all
; clobbers: nothing else (flags)
;
; Returns the row in DI, not SI, because the play path holds its sample
; pointer in SI.
; -----------------------------------------------------------------------------
snd_pcm_route:
    push ax
    push si
    mov bl, [snd_route + SND_TIER_PCM]
    cmp bl, 0FFh
    je .auto
    call snd_pcm_row
    jnc .done                   ; pinned to a usable sink: use it
.auto:
    mov si, snd_pref_pcm
.scan:
    mov bl, [si]
    inc si
    cmp bl, 0FFh
    je .none                    ; list exhausted: no PCM sink
    push si
    call snd_pcm_row
    pop si
    jc .scan
.done:
    pop si                      ; CF = 0 from snd_pcm_row survives the pops
    pop ax
    ret
.none:
    stc
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; snd_pcm_row - is driver row BL a usable PCM_EXCL sink? (internal)
; in:  BL = driver index
; out: CF = 0 and DI = row ptr if present with the cap and an op, else CF = 1
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
snd_pcm_row:
    push si
    call snd_drv_row            ; SI = row
    mov di, si
    pop si
    test byte [di+SDRV_FLAGS], 1
    jz .no
    test byte [di+SDRV_CAPS], SND_CAP_PCM_EXCL
    jz .no
    cmp word [di+SDRV_PCM], 0
    je .no
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; snd_route_set - pin (or unpin) a tier's route override (SPEC.md 34.2)
;
; in:       BL = tier (SND_TIER_*), AL = driver row index, or 0FFh = auto
; out:      CF = 0 stored; CF = 1 refused - the named driver is absent.
;           This is the setter layer of the bb_avail idiom (SPEC.md 31.4):
;           the Control Panel's caption and click are the other two.
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
snd_route_set:
    push bx
    push si
    cmp al, 0FFh                ; auto never names a driver: always legal
    je .store
    xchg bl, al                 ; snd_drv_row wants the index in BL
    call snd_drv_row
    xchg bl, al
    test byte [si+SDRV_FLAGS], 1
    jz .refuse
.store:
    xor bh, bh                  ; BL = tier; a single byte store needs no
    mov [snd_route+bx], al      ; window - readers take it whole
    clc
    jmp .out
.refuse:
    stc
.out:
    pop si
    pop bx
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
    call snd_tone_route         ; BL = driver index, SI = row
    cmp bl, DRV_SPK
    jne .prog
    cmp byte [snd_ch2mode], 2   ; a PWM clip holds ch2: tone refused (34.1)
    je .refuse
.prog:
    push dx
    xor dl, dl                  ; voice 0
    call [si+SDRV_TONE]         ; program the ports; CF = bad frequency
    pop dx
    jc .refuse                  ; nothing changed: the old owner still sounds
    cmp byte [snd_town_act], 0  ; a tone left sounding on a DIFFERENT sink
    je .stamp                   ; (route changed since its grant) stops now
    cmp bl, [snd_town_drv]
    je .stamp
    call snd_town_off
.stamp:
    inc byte [snd_gen]          ; stamp the owner record - same IF=0 window
    mov al, [snd_gen]           ; as the port writes (SPEC.md 34.3)
    mov [snd_town_gen], al
    mov [snd_texp_gen], al
    mov [snd_town_pri], dl
    mov [snd_town_inst], dh
    mov [snd_town_drv], bl
    mov [snd_town_exp], cx
    mov byte [snd_town_act], 1
    cmp bl, DRV_SPK
    jne .granted
    mov byte [snd_ch2mode], 1
.granted:
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
    mov bl, [snd_town_drv]      ; silence on the sink that is sounding, which
    call snd_drv_row            ; need not be the tier's CURRENT route
    xor ax, ax
    xor dl, dl
    call [si+SDRV_TONE]
    cmp bl, DRV_SPK
    jne .clear
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
    call sbl_tick               ; the SB stream watchdog (SPEC.md 34.5):
                                ; ~2 compares when no stream is armed;
                                ; clobbers AX/DX, both free here
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
; the instance owns; FM channel bits, stream ownership and staging grants
; join at their phases. The release ignores priority - the owner is gone.
; -----------------------------------------------------------------------------
snd_release_inst:
    pushf
    cli
    cmp byte [snd_live], 0
    je .done
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
    cmp byte [snd_live], 0      ; the FM leg (SPEC.md 34.3): at most 9
    je .out                     ; single-write key-offs, run OUTSIDE the
    call opl_release_inst       ; tone window - the dying instance can make
                                ; no new claims, so no race to close
    call sbl_release_inst       ; the SB leg (SPEC.md 34.5): close the dying
                                ; instance's stream (its refill task exits
                                ; at the next wake) + free its grants
.out:
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
; out: nothing; [snd_inst] = the instance slot (0xFF for none)
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
    mov [snd_inst], al
    pop cx
    pop ax
    ret

; =============================================================================
; Boot and teardown (SPEC.md 34.7)
; =============================================================================

; -----------------------------------------------------------------------------
; snd_init - initialise the sound layer (kmain, after tm_init)
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
; -----------------------------------------------------------------------------
snd_init:
    push ax
    push cx
    push si

    in al, 0x61                 ; the boot state of the gate/data bits -
    and al, 0x03                ; snd_unhook restores exactly these
    mov [snd_61boot], al

    mov byte [snd_ch2mode], 0
    mov word [snd_inst], 0x00FF ; word: the dispatch sites push/pop it whole
    mov byte [snd_gen], 0
    mov byte [snd_town_act], 0
    mov byte [snd_town_inst], 0xFF
    mov byte [snd_town_pri], 0
    mov byte [snd_town_gen], 0
    mov byte [snd_texp_gen], 0
    mov byte [snd_town_drv], DRV_SPK
    mov word [snd_town_exp], 0

    mov byte [snd_pcm_busy], 0  ; the PWM clip state (SPEC.md 34.4); the
    mov byte [snd_abort], 0     ; 256-byte xlat table needs no init - it is
    mov byte [snd_btn0], 0      ; rebuilt before every clip
    mov byte [snd_pcm_inst], 0xFF
    mov byte [snd_pcm_smp], 0
    mov word [snd_pcm_n], 149   ; the default-rate divisor (8,008 Hz)
    mov word [snd_pcm_emitted], 0
    mov word [snd_pcm_resync], 0

    call opl_state_init         ; the FM allocator's .bss (sndfm.inc) -
                                ; stored ahead of the probe that could
                                ; publish its row (SPEC.md 34.7)
    call sbl_state_init         ; likewise the SB driver's (sndsb.inc):
                                ; its probe fills sbl_base/version on top

    mov si, snd_drv             ; probe rows that declare one (SPEC.md 34.2);
    mov cx, SND_NDRV            ; SDRV_PROBE = 0 rows keep their initialised
.probe:                         ; presence (speaker: 1; OPL2/SB: 0 until
    mov ax, [si+SDRV_PROBE]     ; their phases bring probes)
    or ax, ax
    jz .next
    call ax                     ; out AL = 1 present / 0 absent
    and al, 1
    mov ah, [si+SDRV_FLAGS]     ; presence published after the device is
    and ah, 0xFE                ; fully configured (SPEC.md 34.2)
    or al, ah
    mov [si+SDRV_FLAGS], al
.next:
    add si, SDRV_SIZE
    loop .probe

    mov byte [snd_live], 1      ; publish-last (SPEC.md 34.7, binding)

    pop si
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
; saved boot state (upper bits preserved), leaving ch2 quiescent; then the
; SB mirror image (sbl_unhook: mask the IRQ, reset the DSP, restore the
; vector - SPEC.md 34.7).
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
    call sbl_unhook             ; no-op unless sbl_isr was ever hooked
.out:
    pop ax
    ret

; =============================================================================
; The five API slot targets (SPEC.md 20.3, 0x0078..0x0088) - the whole sound
; ABI ships in Phase 1; the unbuilt tiers return their error paths.
; =============================================================================

; -----------------------------------------------------------------------------
; osapi_snd_caps - what can this machine's sound hardware do?
;
; out:      AX = merged caps word (OR of every present row's SDRV_CAPS),
;           BL = tone route (0 = speaker, 1 = OPL2),
;           DX = present-driver bits (bit n = snd_drv row n present)
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
osapi_snd_caps:
    push cx
    push si
    xor ax, ax
    xor dx, dx
    mov cl, 1                   ; CL = this row's presence bit
    mov si, snd_drv
.row:
    test byte [si+SDRV_FLAGS], 1
    jz .next
    or al, [si+SDRV_CAPS]
    or dl, cl
.next:
    shl cl, 1
    add si, SDRV_SIZE
    cmp si, snd_drv + SND_NDRV * SDRV_SIZE
    jb .row
    call snd_tone_route         ; BL = winning driver index (SI clobbered -
    cmp bl, DRV_OPL             ; it is restored below)
    mov bl, 1                   ; flags survive the mov
    je .have
    mov bl, 0
.have:
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; osapi_snd_tone - the public tone request (slot 0x007C, SPEC.md 20.3)
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
; -----------------------------------------------------------------------------
snd_req_inst:
    push bx
    mov dh, [snd_inst]
    cmp dh, 0xFF
    jne .out
    mov bl, [sch_cur]           ; not in a callback: the running task's
    xor bh, bh                  ; instance (0xFF for the UI task)
    shl bx, 1                   ; T_SIZE = 8
    shl bx, 1
    shl bx, 1
    mov dh, [bx+sch_tasks+T_INST]
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; osapi_snd_play - PCM_EXCL clip, run-to-completion (slot 0x0080, SPEC.md
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

    call snd_pcm_route          ; BL = row index, DI = row ptr
    jc .e4                      ; no PCM_EXCL sink on this machine

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

    mov bh, [snd_inst]          ; requesting instance: the dispatched
    cmp bh, 0xFF                ; callback's stamp, else the running
    jne .grant                  ; task's T_INST (SPEC.md 34.3)
    push bx
    mov bl, [sch_cur]
    xor bh, bh
    shl bx, 1                   ; T_SIZE = 8
    shl bx, 1
    shl bx, 1
    mov ah, [bx+sch_tasks+T_INST]
    pop bx
    mov bh, ah

.grant:
    pushf                       ; --- the grant window (SPEC.md 34.3) ------
    cli
    cmp byte [snd_pcm_busy], 0  ; one clip at a time, no queue
    jne .busyw
    cmp byte [sbl_str_act], 0   ; refused while a PCM_BG stream is open
    jne .busyw                  ; (SPEC.md 34.3): the clip's sch_lock would
                                ; freeze the refill task into its underrun
                                ; path - refuse the combination instead
    cmp bl, DRV_SPK             ; stealing ch2 only matters on the speaker
    jne .own
    cmp byte [snd_town_act], 0
    je .own
    cmp byte [snd_town_drv], DRV_SPK
    jne .own                    ; tone sounding elsewhere: it coexists
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
                                ; above are one unit (SPEC.md 34.3)...
    cmp bl, DRV_SPK             ; ...but ch2 belongs to the grant ONLY on
    jne .noch2                  ; the speaker route: only spk_pcm_idle ever
    mov byte [snd_ch2mode], 2   ; clears mode 2, so an SB/Covox clip that
.noch2:                         ; stamped it would wedge tones forever
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
    call [di+SDRV_PCM]          ; op's epilogue releases ch2 + busy. Out:
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

; -----------------------------------------------------------------------------
; osapi_snd_fm - the public FM surface (slot 0x0084, SPEC.md 20.3 - live
;                from Phase 3)
;
; in:       AL = verb: 0 note-on / 1 note-off / 2 patch-load / 3 all-off;
;           CL = channel 0..8, BX = frequency Hz, DS:SI = 11-byte patch
;           (per verb, SPEC.md 34.2)
; out:      CF = 1 no FM sink, or the op's refusal (bad verb/channel/
;           frequency, a reserved or another requester's channel -
;           SPEC.md 34.2/34.3); CF = 0 done
; clobbers: nothing else (flags)
;
; Dispatches through the FM-capable row (DRV_OPL) when present; ownership
; is the op's job (opl_fm_op stamps claims with the requesting instance,
; the snd_req_inst rule). The dispatch uses absolute memory operands -
; no register may be borrowed here, because every caller register is op
; input (verb 2's patch pointer arrives in SI).
; -----------------------------------------------------------------------------
osapi_snd_fm:
    cmp byte [snd_live], 0      ; not initialised: no sink can be present
    je .no
    test byte [snd_drv + SDRV_SIZE*DRV_OPL + SDRV_FLAGS], 1
    jz .no                      ; no AdLib: the documented refusal
    jmp [snd_drv + SDRV_SIZE*DRV_OPL + SDRV_NOTE]  ; tail: the op's CF/regs
.no:                            ; are the public contract
    stc
    ret

; -----------------------------------------------------------------------------
; osapi_snd_stream - the stream + staging surface (slot 0x0088, SPEC.md
; 20.3 - live from Phase 4; recording from Phase 5). AL = verb, AH =
; handle or sub-op; the verb bodies live with the SB driver
; (sbl_stream_op, SPEC.md 34.5/34.6). The staging verbs (5/6/7) work on
; every machine - the pool exists wherever SND_SEG does; the stream verbs
; (0-4) refuse cleanly (AX = 4) without a card.
; -----------------------------------------------------------------------------
osapi_snd_stream:
    jmp sbl_stream_op

; =============================================================================
; The PWM rescale-table builder (SPEC.md 34.4): cold per-clip work, so it
; lives in .fartext behind a FARSHIM (SPEC.md 33/34.7). Far code keeps
; DS = KERNEL_SEG, so it writes the .bss table directly.
; =============================================================================
    FARSHIM snd_xlat_build, snd_f_xlat

section .fartext

; -----------------------------------------------------------------------------
; snd_f_xlat - build the 256-byte PWM rescale table (far body)
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
snd_f_xlat:
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
    retf                        ; far body: reached through the shim

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
snd_town_drv:  resb 1           ;   driver row the tone is sounding on
snd_town_exp:  resw 1           ;   remaining ticks, 0 = no self-expiry
snd_inst:      resw 1           ; low byte: instance slot being dispatched
                                ; (0xFF = none); a word so the dispatch
                                ; sites can push/pop it around callbacks

snd_xlat:      resb 256         ; per-clip PWM rescale table (34.4), built
                                ; by snd_xlat_build before every clip -
                                ; write-before-read, needs no boot init
snd_btn0:      resb 1           ; click-abort baseline; releases fold in
snd_abort:     resb 1           ; set by snd_stop / snd_release_inst
snd_pcm_busy:  resb 1           ; a PCM_EXCL clip is running (34.3)
snd_pcm_inst:  resb 1           ;   the instance that owns it (0xFF kernel)
snd_pcm_smp:   resb 1           ;   the sample parked across pacer polls
snd_pcm_n:     resw 1           ;   the clip divisor N (74..255)
snd_pcm_emitted: resw 1         ; debug counters (34.4), reset per clip:
snd_pcm_resync:  resw 1         ;   pulses emitted / deadline resyncs -
                                ;   the Phase 2 gate's instrument, shown
                                ;   on the Control Panel Sound caption
snd_evtmp:     resb 8           ; click-abort event drain scratch (evq_pop
                                ; copies into DS memory)

section .text
