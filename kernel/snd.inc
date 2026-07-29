; =============================================================================
; os8088 - snd.inc
;
; The sound core (SPEC.md 34): driver table, capability bits, per-tier route
; selection, the tone tier's owner/priority/generation router, the PC speaker
; tone driver, snd_tick (the one scheduler touch-point), and the five os8088
; API slot targets at 0x0078..0x0088. Phase 1 of docs/SOUND-PLAN.md: tones
; only - the PCM, FM and STREAM surfaces ship as clean error stubs so the
; jump-table ABI is whole from day one.
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
    dw spk_tone                 ; tone op (SDRV_PCM arrives in Phase 2)
    dw 0, 0, 0
    dw 0                        ; no probe needed
    dw snd_s_spk
    dw 0
                                ; row 1: OPL2 - probe + ops land in Phase 3
    db SND_CAP_TONE | SND_CAP_FM
    db 0                        ; absent until its probe exists
    dw 0, 0, 0, 0
    dw 0
    dw snd_s_opl
    dw 0
                                ; row 2: Sound Blaster - lands in Phases 4-5
    db SND_CAP_PCM_BG | SND_CAP_PCM_EXCL | SND_CAP_PCM_IN
    db 0                        ; absent until its detect exists
    dw 0, 0, 0, 0
    dw 0
    dw snd_s_sb
    dw 0

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
; path and inst_task_die). Phase 1 releases the tone channel; FM channel
; bits, stream ownership and staging grants join at their phases. The
; release ignores priority - the owner is gone.
; -----------------------------------------------------------------------------
snd_release_inst:
    pushf
    cli
    cmp byte [snd_live], 0
    je .done
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
; reads (nothing clears .bss at boot, SPEC.md 8), runs any probes the driver
; table declares (none until Phase 3 - the speaker's presence is initialised
; data), and sets snd_live LAST: the publish that arms snd_tick and the
; public entry points (SPEC.md 34.7, the 29.2 publish-last idiom).
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
; saved boot state (upper bits preserved), leaving ch2 quiescent. The SB
; unhook (mask IRQ, reset DSP, restore vector) joins in Phase 4.
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
    push bx
    push dx
    mov dh, [snd_inst]
    cmp dh, 0xFF
    jne .go
    mov bl, [sch_cur]           ; not in a callback: the running task's
    xor bh, bh                  ; instance (0xFF for the UI task)
    shl bx, 1                   ; T_SIZE = 8
    shl bx, 1
    shl bx, 1
    mov dh, [bx+sch_tasks+T_INST]
.go:
    call snd_tone_req
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; osapi_snd_play / osapi_snd_fm / osapi_snd_stream - error stubs until their
; phases land (SPEC.md 20.3): a package built against the full ABI gets the
; documented "no sink" refusal, never a jump past the table end.
; -----------------------------------------------------------------------------
osapi_snd_play:                 ; live in Phase 2 (SPEC.md 34.4)
    mov ax, 4                   ; err 4: no sink
    stc
    ret

osapi_snd_fm:                   ; live in Phase 3 (SPEC.md 34.2)
    stc                         ; CF = 1: no FM sink
    ret

osapi_snd_stream:               ; live in Phases 4-5 (SPEC.md 34.5/34.6)
    mov ax, 4                   ; err 4: no sink
    stc
    ret

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

section .text
