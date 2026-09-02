; =============================================================================
; os8088 - cpudet.inc
;
; WHICH CPU IS THIS? (SPEC.md 60) - two published bytes and two routines, and
; that is deliberately the whole of it.
;
; This module used to carry the A20 gate and the HMA claim as well, and was
; documented under SPEC.md 41 as half of "CPU tiers and memory above 1MB",
; because it was born as that feature's prerequisite: the commit that first
; reached above 1MB had to know it was on a 286+ before it could look. That
; has been backwards for a while. The tier is read across the tree by things
; with no interest in memory at all - Note Pad's typing path (SPEC.md 27.3),
; Missile Command's explosion ramp, Tracker, ModPlug, Paint, the Task Manager,
; both benchmarks, and the hard-disk driver, which gates its rung-1 IDE task
; file on CPU_286 because an 8088's `in ax, dx` loses the drive's high byte -
; while memory above 1MB is the tier's LEAST active reader, nothing in the
; tree allocating out of that pool at all.
;
; The kern_small split (SPEC.md 41.11) is what made it visible: the seam ran
; straight through this file, half of it in both builds and half kern_big's,
; which is the shape that says one file is holding two subjects. So the gate
; and the claim moved to xmem.inc, beside the only two callers they have, and
; THIS MODULE HAS NO %ifdef IN IT - it is in both builds, entire.
;
; [cpu_tier] is one of CPU_8086 / CPU_286 / CPU_386 and [cpu_feat] carries the
; four feature bits, with two writers that each own their bits (SPEC.md
; 84.6.2): xmem.inc writes the three memory bits, this module's probe writes
; CPU_F_X87, and this module publishes the byte. Both are INITIALIZED DATA IN
; .text, not .bss: -f bin zeroes nothing at boot, and the answer a machine
; reads when the probe never ran has to be the safe one - the sw_avail /
; snd_live idiom of SPEC.md 32/34.7. cpu_detect writes the tier on every
; path. On kern_small nothing ever writes the three memory bits and they read
; 0, which is true - no gate verified, no HMA, no unreal mode - and is
; exactly what tier 0 answers; the x87 probe has no %ifdef, so CPU_F_X87 is
; live on kern_small too.
;
; THE TIER IS INFORMATION, NOT PERMISSION (SPEC.md 60.2), and that is this
; module's whole contract. Nothing branches on [cpu_tier] to decide whether
; memory above 1MB is usable: it branches on the KB figure from xm_caps. A 386
; whose A20 gate never answers is still tier 2 and still has no store, and
; code keyed off the tier alone would walk straight into it. The legitimate
; uses are choosing an instruction encoding, or a code path whose cost the CPU
; decides - which is what every reader listed above wants it for.
;
; cpu_detect is BOOT OVERLAY (SPEC.md 2.5): it runs once from kmain, first
; thing, before sched_init hooks an ISR and before anything may key off the
; tier, and is then written over by the first mount's FAT. Only cpu_info stays
; in .text: it is API slot 0x0188 and answers all session. Both are plain 8086
; code - cpu_detect by necessity, since it runs before the tier it publishes
; is known.
; =============================================================================

; Feature bits in [cpu_feat]. Mirrored in the osapi_cpu_info contract
; (SPEC.md 20.3): bit 0 A20 verified open, bit 1 HMA claimed, bit 2 unreal
; mode armed, bit 3 a coprocessor socketed (SPEC.md 84.6). Module-private -
; SPEC.md 3 does not carry them.
CPU_F_A20       equ 0x01
CPU_F_HMA       equ 0x02
CPU_F_UNREAL    equ 0x04
CPU_F_X87       equ 0x08


cpu_tier        db CPU_8086     ; the published tier (SPEC.md 41.1). .text,
                                ; not .bss: the floor machine's answer must
                                ; be what a machine reads even if the probe
                                ; never ran.

cpu_feat        db 0            ; CPU_F_* - what actually verified, as
                                ; opposed to what the CPU could in principle
                                ; do. Initialized data for the same reason.

cpu_x87sw       dw 0            ; the x87 probe's landing pad. In .text for
                                ; cpu_tier's reason and because cpu_detect is
                                ; boot overlay - it cannot own bss that
                                ; outlives it.

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

section .ovl                    ; the boot overlay (SPEC.md 2.5): runs once from
                                ; kmain and is written over by the first
                                ; mount. cpu_info stays resident below: it is
                                ; API slot 0x0188 and answers all session
                                ; long. Nothing here calls out of the overlay
                                ; and nothing here has data of its own -
                                ; cpu_tier and cpu_feat are .text, written
                                ; through DS like any kernel byte
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
    jmp short .tierdone
.is286:
    mov byte [cpu_tier], CPU_286
    jmp short .tierdone
.is8086:
    mov byte [cpu_tier], CPU_8086
.tierdone:

    ; --- is a coprocessor socketed? (SPEC.md 41.1) ---------------------------
    ; A 5150 shipped with an EMPTY 8087 socket, and plenty were filled later,
    ; so this is a per-machine fact and not a tier fact - which is why it is a
    ; feature bit rather than a new CPU_* level.
    ;
    ; The probe is FNINIT then read the status word back: on a part that is
    ; there, FNINIT clears it to zero, and the control word comes back with
    ; every exception masked. A sentinel goes in BETWEEN the two - it still
    ; precedes the read-back, which is all it needs to make "nothing wrote
    ; here" distinguishable from "an FPU wrote a zero", and the store's own
    ; bus cycles give a real 8087 time to finish FNINIT before FNSTSW reaches
    ; it, which is Intel's own canonical detection order - the no-wait forms
    ; carry no BUSY interlock, so the spacing has to come from the code.
    ;
    ; EVERY FORM USED HERE IS A NO-WAIT ONE - FNINIT, FNSTSW, FNSTCW, never
    ; FINIT or FSTSW. The waiting forms assemble to a WAIT ahead of the ESC,
    ; and on a machine with no coprocessor to lower BUSY that WAIT is a hang
    ; rather than a wrong answer. An ESC with nothing listening is simply
    ; ignored by an 8086/8088, which is what makes running this unconditionally
    ; safe on the floor machine.
    ;
    ; This beats INT 11h's equipment word, whose bit 1 on a 5150 is a DIP
    ; SWITCH someone set by hand - it reports what the owner claimed, not what
    ; is in the socket.
    fninit
    mov word [cpu_x87sw], 0x5A5A
    fnstsw word [cpu_x87sw]
    cmp word [cpu_x87sw], 0
    jne .out
    fnstcw word [cpu_x87sw]
    mov ax, [cpu_x87sw]
    and ax, 0x103F
    cmp ax, 0x003F              ; a just-initialised control word: all
    jne .out                    ; exceptions masked, extended precision
    or byte [cpu_feat], CPU_F_X87
.out:
    popf
    pop cx
    pop ax
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

section .text
; -----------------------------------------------------------------------------
cpu_info:
    mov al, [cpu_tier]
    mov ah, [cpu_feat]
    ret
