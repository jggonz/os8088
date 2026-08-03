; =============================================================================
; os8088 - cpudet.inc
;
; CPU tier detection and the A20 line (SPEC.md 41.1-41.3). Two published
; bytes and three routines, and on the target machine - an 8086 with 256KB -
; all three reduce to a stored byte and two immediate returns.
;
; [cpu_tier] is one of CPU_8086 / CPU_286 / CPU_386 and [cpu_feat] carries
; the three feature bits. Both are INITIALIZED DATA IN .text, not .bss:
; -f bin zeroes nothing at boot, and the answer a machine reads when the
; probe never ran has to be the safe one - the bb_avail / snd_live idiom of
; SPEC.md 32/34.7. cpu_detect writes the tier on every path.
;
; The tier is information, not permission (SPEC.md 41.1). Nothing above this
; module branches on [cpu_tier] to decide whether memory above 1MB is usable:
; it branches on [cpu_feat]. A 386 whose A20 gate never answers is still
; tier 2 and still has no store, and code keyed off the tier alone would walk
; straight into it. The one legitimate use of the tier is choosing an
; instruction encoding - which transport xm_copy runs, whether xm_arm may
; execute its island - and that is exactly what SPEC.md 41.9 rule 2 means by
; "an island is assembly-time permission only".
;
; Verification is the contract (SPEC.md 41.2). Both A20 enable methods are
; advisory: a machine may have neither, may decode port 0x92 to something
; else entirely, may have a keyboard controller that accepts D1h/DFh and
; does nothing. So CPU_F_A20 is set by cpu_a20_probe and by NOTHING else -
; never by "we wrote to the gate".
;
; Tier 0 (SPEC.md 41.9 rule 1): there is no A20 line on an 8088. The wrap at
; 1MB is the width of the address bus, not a gate, and port 0x92 on an XT
; decodes to whatever that machine happens to put there. cpu_a20_enable
; returns having touched no port at all, and cpu_hma_claim refuses.
;
; This module is .text only. cpu_detect runs from kmain immediately after
; far_init and before anything else may key off the tier (SPEC.md 15), and
; every routine here is plain 8086 code - cpu_detect by necessity, since it
; runs before the tier it publishes is known, and the other two because
; nothing they do needs more.
; =============================================================================

; Feature bits in [cpu_feat]. Mirrored in the osapi_cpu_info contract
; (SPEC.md 20.3): bit 0 A20 verified open, bit 1 HMA claimed, bit 2 unreal
; mode armed. Module-private - SPEC.md 3 does not carry them.
CPU_F_A20       equ 0x01
CPU_F_HMA       equ 0x02
CPU_F_UNREAL    equ 0x04

; The A20 wraparound probe's scratch. Linear 0x00500..0x005FF is the 256
; free bytes of SPEC.md 2 (the boot stack that grew down from 0x7C00 is dead
; by kmain), which is why the probe goes there and not at 0000:0000 - the
; kernel hooks int 08h and IRQ4 and the IVT is live. The alias is arithmetic:
; HMA_SEG:0510 = 0xFFFF0 + 0x510 = linear 0x100500, the same byte with A20
; shut and 1MB higher with it open.
CPU_A20_OFF     equ 0x0500
CPU_A20_ALIAS   equ HMA_MIN_OFF + CPU_A20_OFF    ; = 0x0510
CPU_A20_PAT1    equ 0x1234              ; written low
CPU_A20_PAT2    equ 0x4321              ; written through the alias
CPU_A20_SETTLE  equ 512                 ; probes to allow a gate to take

cpu_tier        db CPU_8086     ; the published tier (SPEC.md 41.1). .text,
                                ; not .bss: the floor machine's answer must
                                ; be what a machine reads even if the probe
                                ; never ran.

cpu_feat        db 0            ; CPU_F_* - what actually verified, as
                                ; opposed to what the CPU could in principle
                                ; do. Initialized data for the same reason.

; -----------------------------------------------------------------------------
; cpu_detect - publish the CPU tier (SPEC.md 41.1)
;
; in:       -
; out:      [cpu_tier] = CPU_8086 / CPU_286 / CPU_386, written on EVERY path
; clobbers: nothing (flags restored - the probe writes FLAGS itself)
;
; The discriminator is the FLAGS register's top nibble, which the three
; generations answer differently in real mode, and the order of the two
; tests is binding:
;
;   1. Clear bits 12..15 and read them back. Still set = 8086/8088: its
;      pushf writes ones there unconditionally and they cannot be cleared.
;   2. Otherwise set bits 12..15 and read them back. A 286 in real mode
;      forces all four to zero (IOPL and NT are not writable there, and bit
;      15 is reserved zero from the 286 on); a 386+ keeps IOPL and NT, so a
;      non-zero read means tier 2.
;
; This routine is the one place in either module that runs before the tier
; is known, so there is no island in it and there cannot be one.
; -----------------------------------------------------------------------------
cpu_detect:
    push ax
    push cx
    pushf                       ; the caller's FLAGS - the probe overwrites
                                ; them and popf at the end puts them back
    pushf
    pop cx                      ; CX = FLAGS as they came in
    mov ax, cx
    and ax, 0x0FFF              ; try to clear bits 12..15
    push ax
    popf
    pushf
    pop ax
    and ax, 0xF000
    cmp ax, 0xF000
    je .is8086                  ; they came back set: nothing can clear them
    mov ax, cx
    or ax, 0xF000               ; try to set bits 12..15
    push ax
    popf
    pushf
    pop ax
    and ax, 0xF000
    jz .is286                   ; all four forced to zero: 286 in real mode
    mov byte [cpu_tier], CPU_386
    jmp short .out
.is286:
    mov byte [cpu_tier], CPU_286
    jmp short .out
.is8086:
    mov byte [cpu_tier], CPU_8086
.out:
    popf
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cpu_a20_probe - is the A20 line open RIGHT NOW? (SPEC.md 41.2)
;
; in:       -
; out:      CF = 0 open, CF = 1 shut; CPU_F_A20 in [cpu_feat] set to match.
;           THIS ROUTINE IS THE ONLY WRITER OF THAT BIT.
; clobbers: nothing else (flags)
;
; Write a known word to 0000:0500, a different word through the alias at
; HMA_SEG:0510, then read 0000:0500 back. Unchanged means the second write
; landed a megabyte up and the line is open; mutated means it wrapped.
;
; The saved words are restored in the opposite order to the writes, which is
; correct in BOTH cases: with the line shut the two addresses are the same
; word, and putting back the alias value (which is then just the pattern we
; wrote low) before the low value leaves the original byte pair intact.
;
; The whole thing runs inside one pushf/cli ... popf - an interrupt that
; touched conventional memory between the write and the read-back would make
; the probe lie - and every register including ES is restored.
; -----------------------------------------------------------------------------
cpu_a20_probe:
    push ax
    push bx
    push dx
    push es
    pushf
    cli                         ; --- the probe window ---------------------
    xor ax, ax
    mov es, ax
    mov bx, [es:CPU_A20_OFF]            ; save the low word
    mov word [es:CPU_A20_OFF], CPU_A20_PAT1
    mov ax, HMA_SEG
    mov es, ax
    mov dx, [es:CPU_A20_ALIAS]          ; save the aliased word (= PAT1 if
                                        ; the line is shut - see above)
    mov word [es:CPU_A20_ALIAS], CPU_A20_PAT2
    xor ax, ax
    mov es, ax
    mov ax, [es:CPU_A20_OFF]            ; did the high write land down here?
    push ax
    mov ax, HMA_SEG
    mov es, ax
    mov [es:CPU_A20_ALIAS], dx          ; restore, alias first
    xor ax, ax
    mov es, ax
    mov [es:CPU_A20_OFF], bx
    pop ax
    popf                        ; --- probe window closed ------------------
    and byte [cpu_feat], 0xFF - CPU_F_A20
    cmp ax, CPU_A20_PAT1
    jne .shut                   ; the low word was overwritten: it wrapped
    or byte [cpu_feat], CPU_F_A20
    pop es
    pop dx
    pop bx
    pop ax
    clc
    ret
.shut:
    pop es
    pop dx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; cpu_a20_settle - re-probe a few times, to let a gate take effect
;
; in:       -
; out:      CF = 0 open (and CPU_F_A20 set), CF = 1 still shut
; clobbers: nothing else (flags)
;
; The keyboard controller's gate command in particular is not instantaneous:
; the controller acknowledges long before the line moves. A bounded retry is
; the difference between "this machine has no A20" and "we asked too early".
; -----------------------------------------------------------------------------
cpu_a20_settle:
    push cx
    mov cx, CPU_A20_SETTLE
.spin:
    call cpu_a20_probe
    jnc .open
    loop .spin
    pop cx
    stc
    ret
.open:
    pop cx
    clc
    ret

; -----------------------------------------------------------------------------
; cpu_kbc_wait - wait for the keyboard controller's input buffer to empty
;
; in:       -
; out:      CF = 0 empty, CF = 1 timed out
; clobbers: nothing else (flags)
;
; The timeout is what stops a wedged controller from hanging the machine at
; boot (SPEC.md 41.2). 65,536 polls of an ISA port is on the order of 60ms -
; paid at most three times, and only on a machine whose controller is dead.
; -----------------------------------------------------------------------------
cpu_kbc_wait:
    push ax
    push cx
    xor cx, cx                  ; 65,536 polls
.poll:
    in al, 0x64
    test al, 0x02               ; input buffer full?
    jz .empty
    loop .poll
    pop cx
    pop ax
    stc
    ret
.empty:
    pop cx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; cpu_fast_a20 - the fast gate at port 0x92
;
; in:       -
; out:      nothing (the PROBE decides whether it worked)
; clobbers: nothing (flags)
;
; Read the port, set bit 1, write it back. NEVER WRITE BIT 0: that is the
; fast-reset line and a 1 there reboots the machine (SPEC.md 41.2). A read
; of 0xFF - "no such port", the common answer on a machine that has none -
; already has bit 1 set, so this declines to write at all rather than
; guessing; the keyboard controller is the next step either way.
;
; Read-modify-write of a port shared with nothing else in the kernel, but
; still under pushf/cli: an ISR that touched 0x92 between the in and the out
; would have its write silently undone.
; -----------------------------------------------------------------------------
cpu_fast_a20:
    push ax
    pushf
    cli
    in al, 0x92
    test al, 0x02
    jnz .out                    ; already set, and the line is still shut:
                                ; 0x92 is not the gate on this machine
    or al, 0x02
    and al, 0xFE                ; mask bit 0 - fast reset - unconditionally
    out 0x92, al
.out:
    popf
    pop ax
    ret

; -----------------------------------------------------------------------------
; cpu_kbc_a20 - ask the keyboard controller to open the gate
;
; in:       -
; out:      nothing (the PROBE decides whether it worked)
; clobbers: nothing (flags)
;
; Command D1h "write output port" to 0x64, then the output-port value DFh to
; 0x60 - bit 1 of that port is A20, and DFh is the standard value that sets
; it while leaving the reset line (bit 0) alone. Each write waits for the
; input buffer first, bounded.
;
; THE WHOLE SEQUENCE IS ONE pushf/cli ... popf WINDOW (SPEC.md 41.2), and
; that is not tidiness: between the D1h and the DFh the controller is armed
; to take the NEXT byte written to 0x60 as its output port. The kernel does
; not hook int 09h, so the BIOS keyboard ISR is live here; a key down or
; repeating at that instant makes it read 0x60 and write a keyboard command
; back - and the 8042 would consume THAT as the output port value. Bit 0 of
; that port is the active-low CPU RESET line, so 0xF4 or 0xF6 (both perfectly
; ordinary bytes for the BIOS to send) reboot the machine mid-boot. The mild
; version of the same race is the ISR's own 0x64 traffic satisfying the
; pending D1h with something benign, leaving A20 shut and the machine with no
; extended memory it can prove it has.
;
; The cost is a bounded IF=0 window: cpu_kbc_wait times out after 65,536
; polls, so a dead controller stalls boot by ~180ms once, before sched_init
; installs any kernel ISR at all. A dropped BIOS tick there is nothing.
; -----------------------------------------------------------------------------
cpu_kbc_a20:
    push ax
    pushf
    cli                         ; --- the gate window ----------------------
    call cpu_kbc_wait
    jc .out
    mov al, 0xD1
    out 0x64, al
    call cpu_kbc_wait
    jc .out
    mov al, 0xDF
    out 0x60, al
    call cpu_kbc_wait           ; let the command drain before probing
.out:
    popf                        ; --- window closed ------------------------
    pop ax
    ret

; -----------------------------------------------------------------------------
; cpu_a20_enable - open the A20 line, and VERIFY it (SPEC.md 41.2)
;
; in:       [cpu_tier] published (cpu_detect has run)
; out:      CF = 0 and CPU_F_A20 set: the line is verified open.
;           CF = 1 and CPU_F_A20 clear: it is not, on any tier.
; clobbers: nothing else (flags)
;
; Three steps in a binding order:
;
;   1. Test before enabling. Every machine the QMP harness boots comes up
;      with A20 already open (SeaBIOS opens it), and so do many later AT
;      BIOSes. If the probe says open, no gate is touched at all.
;   2. The fast gate at port 0x92 (cpu_fast_a20), then re-probe.
;   3. The keyboard controller (cpu_kbc_a20), then re-probe.
;
; On tier 0 the routine returns immediately having touched no port: there is
; no gate on an 8088 and 0x92 belongs to something else there (41.9 rule 1).
; -----------------------------------------------------------------------------
cpu_a20_enable:
    push ax
    and byte [cpu_feat], 0xFF - CPU_F_A20
    cmp byte [cpu_tier], CPU_8086
    je .done                    ; tier 0: no gate exists. Touch nothing.
    call cpu_a20_probe
    jnc .done                   ; already open - do not poke a working line
    call cpu_fast_a20           ; step 2
    call cpu_a20_settle
    jnc .done
    call cpu_kbc_a20            ; step 3
    call cpu_a20_settle
.done:
    pop ax
    test byte [cpu_feat], CPU_F_A20     ; the PROBE decides, never the poke
    jz .fail
    clc
    ret
.fail:
    stc
    ret

; -----------------------------------------------------------------------------
; cpu_hma_claim - claim the High Memory Area for one named owner (SPEC.md 41.3)
;
; in:       AX = extended-memory KB as int 15h AH=88h reported it (0 = none)
; out:      CF = 0 claimed, CPU_F_HMA set: HMA_SEG:HMA_MIN_OFF .. 0xFFFF,
;           HMA_BYTES = 65,520 bytes at linear 0x100000, are the claimant's.
;           CF = 1 refused, CPU_F_HMA clear.
; clobbers: nothing else (flags)
;
; Refuses on tier 0, on a machine where A20 did not VERIFY open, and when
; the BIOS reports fewer than XM_HMA_KB. The last case is not hypothetical:
; a 1MB AT has zero bytes above 0x0FFFFF, its BIOS answers 0, and claiming
; an HMA there would hand out 64KB of nothing.
;
; There is no HMA allocator and there will not be one. A 65,520-byte region
; with two implicit owners is the bug factory SPEC.md 2.2 refuses to build,
; so the claim is all-or-nothing and the claimant names itself in the source.
; It is not exposed to packages either: a real-mode segment above 1MB is a
; DATA segment here (the near model pins CS = DS = KERNEL_SEG), and a package
; that wants bytes up there gets them from xm_alloc, which works on both
; tiers. NOTHING IS CLAIMING IT TODAY - the bit exists so that xm_init knows
; where its pool starts (SPEC.md 2.4) and so the first claimant is a
; two-line change rather than an allocator.
; -----------------------------------------------------------------------------
cpu_hma_claim:
    and byte [cpu_feat], 0xFF - CPU_F_HMA
    cmp byte [cpu_tier], CPU_8086
    je .no
    test byte [cpu_feat], CPU_F_A20     ; the verified bit, not the poke
    jz .no
    cmp ax, XM_HMA_KB
    jb .no                      ; less than 64KB up there: nothing to claim
    or byte [cpu_feat], CPU_F_HMA
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; cpu_info - the published tier and feature bits (osapi slot 0x0188's body)
;
; in:       -
; out:      AL = CPU_8086 / CPU_286 / CPU_386, AH = feature bits (CPU_F_*)
; clobbers: nothing else (flags)
;
; Callable from any context, gfx lock held or not - it reads two bytes and
; touches no hardware. A package must branch on the KB figure from
; xm_caps, never on this tier byte (SPEC.md 41.8).
; -----------------------------------------------------------------------------
cpu_info:
    mov al, [cpu_tier]
    mov ah, [cpu_feat]
    ret
