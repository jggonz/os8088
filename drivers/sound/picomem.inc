; =============================================================================
; os8088 - SOUND.DRV, the PicoMEM tier (SPEC.md 34.10)
;
; Built only by `make PICOMEM=1`. Without the knob not one byte of this file
; is assembled and SOUND.DRV is byte-identical to the shipped one.
;
; WHAT THIS IS FOR. The PicoMEM is a Raspberry Pi Pico on an ISA card that
; emulates, among much else, an AdLib at 388h and a Sound Blaster 2.0. Neither
; answers until something tells the card to install them, and on DOS that
; something is PMINIT.EXE - which os8088 is not going to run. So a PicoMEM
; machine booted os8088 with a sound card in it that could not be found: the
; Drivers page said `No Hardware`, and it was telling the truth about a
; machine that had one.
;
; THE SOUND BLASTER IS OFF AFTER EVERY RESET, AND THAT IS THE FIRMWARE'S
; DELIBERATE CHOICE, NOT A BUG TO WORK AROUND. PM_Audio_OnOff - which the
; PicoMEM BIOS runs just before it boots the machine - restores the AdLib,
; CMS, Tandy and Mindscape devices from the card's saved config and then
; calls PM_sbdsp_OnOff(0) unconditionally. It has to: an emulated SB needs an
; IRQ and a DMA channel, the card cannot know which lines are free on the
; machine it is plugged into, and on DOS the answer comes from the BLASTER
; environment variable. So the SB is a per-boot negotiation by design, and
; this file is os8088's side of it. The AdLib is NOT - it persists - which is
; why a PicoMEM machine with FM enabled in the card's own setup menu already
; worked here and only the DSP tier was missing.
;
; WHY IT LIVES IN SOUND.DRV RATHER THAN IN A PACKAGE OR A DRIVER OF ITS OWN.
; The enable has to happen BEFORE anything probes 388h or 220h, and
; snd_entry's attach is the only place in the system that is defined to be
; exactly there. A package cannot be: it runs long after drv_boot, so the
; sound driver has already probed and failed, and the user would have to go
; and re-tick the Drivers page by hand every session. A driver of its own
; cannot either - drv_boot walks drv_tab in order, so it would have to sit
; ahead of the sound row, and the Drivers page holds exactly four rows (see
; DRV_MAX in kernel/driver.inc) with no fifth to give it. Here the ordering
; is not arranged, it is structural: pm_init runs three instructions before
; opl_probe, in the same routine, and nothing can get between them.
;
; THE CONFIGURATION IS VERY NEARLY FORCED, which is why this asks the user
; nothing. DMA is channel 1 because sb.inc programs channel 1 and no other
; (SPEC.md 34.5, binding - channel 2 is the floppy's). The port is 220h
; because that is the head of sbl_bases and every later entry costs another
; reset scan. That leaves the IRQ, and the card is the only thing that knows
; which lines are free: dev_sbdsp_set_irq_dma refuses its own line, so the
; candidates are offered in sb.inc's own order and the first the firmware
; accepts is the one taken. Nothing here is a guess that could have been a
; question.
;
; SAFETY, and the reason this may ship enabled on a machine with no PicoMEM
; in it at all. Detection is READS ONLY. pm_porttest reads 2A3h a hundred
; times and requires every read to be exactly one more than the last, which
; is the card's test register counting; an empty ISA bus floats to FFh and
; fails on the second read, and no other card in the period answers a
; hundred-long ramp by accident. Not one byte is WRITTEN anywhere until that
; has passed, so on a machine without the card this file costs a few hundred
; microseconds of port reads at attach and changes nothing.
;
; There is deliberately no int 13h AH=60h detect, which is how PMINIT finds
; the card. It is the documented way to learn a base port this code would
; otherwise have to assume - but pmbios/pm_hw.asm assigns PM_BasePort 0x2A0
; and nothing makes it a variable, so the call buys one constant we already
; know in exchange for issuing an undefined int 13h function on every machine
; that does NOT have a PicoMEM, which is every machine that builds this knob
; by mistake. -DPM_BASE covers the day the firmware makes it configurable.
;
; EVERY LOOP IS BOUNDED AND EVERY DEADLINE IS IN TICKS. The status poll is
; the one place a wedged card could hang the boot, and PMINIT's own version
; of it is a `while(true)`. A poll count is not a deadline here for the
; reason NET.DRV had to learn (SPEC.md 62.10): a poll is ~15us on a 4.77MHz
; 8088 and ~1us on anything newer, so a count tuned on one is the wrong
; timeout on the other. The clock is 0040:006C read directly rather than
; OSAPI_GET_TICKS, because a far call is 46.7us (PERFORMANCE.md Part 2) and
; this is an inner loop; the kernel chains the BIOS tick, so the two agree.
; =============================================================================

%ifndef PM_BASE
  %define PM_BASE 0x2A0         ; pmbios/pm_hw.asm's PM_BasePort
%endif
%ifndef PM_SB_PORT
  %define PM_SB_PORT 0x220      ; the head of sbl_bases (SPEC.md 34.2)
%endif

PM_SB_DMA    equ 1              ; NOT a choice - see the header

; Command status, read at base+0 (src/drivers/PMTOOLS/pm_lib.c)
PM_ST_READY  equ 0x00           ; ready for a command
PM_ST_BUSY   equ 0x01           ; one in progress
PM_ST_ERR    equ 0x02           ; it failed; base+1 says why
PM_ST_NOCMD  equ 0x03           ; the firmware does not know that command
PM_ST_INIT   equ 0x04           ; still starting up
PM_ST_WAIT   equ 0x05           ; waiting for its USB serial console

; Commands (src/drivers/PMTOOLS/pm_lib.h)
PMC_RESET    equ 0x00           ; clear an error and go back to READY
PMC_ADLIB    equ 0x73           ; 0 = off, 1 = on at 388h
PMC_SBIRQDMA equ 0x77           ; low byte = IRQ, high byte = DMA
PMC_SBONOFF  equ 0x78           ; 0 = off, 1 = default port, else the port

PM_TESTN     equ 100            ; ramp reads the test register must answer
PM_POLLN     equ 256            ; status reads between two clock checks
PM_TMO       equ 9              ; ...and the deadline, in ticks (~0.5s)
PM_OUTERN    equ 512            ; ...under a hard cap on the batches themselves
PM_RESETS    equ 4              ; how many wedged-status resets we will spend

; -----------------------------------------------------------------------------
; pm_ticks - the BIOS 18.2Hz counter (0040:006C), low word
; out: AX; clobbers nothing else
;
; ES is restored because the caller's is KERNEL_SEG and the whole driver
; contract downstream of here assumes it (drivers/os88drv.inc).
; -----------------------------------------------------------------------------
pm_ticks:
    push es
    push bx
    mov bx, 0x40
    mov es, bx
    mov ax, [es:0x6C]
    pop bx
    pop es
    ret

; -----------------------------------------------------------------------------
; pm_porttest - is a PicoMEM answering at DX?
; in:  DX = the candidate base port
; out: CF = 0 yes; CF = 1 no. DX unchanged, nothing clobbered but flags
;
; base+3 is the card's test register: every read answers one more than the
; last one. THIS IS THE ONLY THING THAT RUNS ON A MACHINE THAT HAS NO PICOMEM,
; and it is why nothing above writes to a port before it has passed.
;
; It runs with interrupts off. The register is stateful - reading it IS what
; advances it - so an interrupt handler that touched the same port mid-ramp
; would break a good card's test, and on a machine where the PicoMEM BIOS has
; its own IRQ installed that is not hypothetical. The window is ~100 ISA
; reads, well under a millisecond, against a 55ms tick.
; -----------------------------------------------------------------------------
pm_porttest:
    push ax
    push bx
    push cx
    push dx
    pushf
    cli
    add dx, 3
    mov cx, PM_TESTN
    in al, dx
    mov bh, al                  ; the ramp's starting point, whatever it is
.ramp:
    in al, dx
    inc bh
    cmp al, bh
    jne .no
    loop .ramp
    popf
    clc
    jmp short .out
.no:
    popf
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pm_wait - poll the command status until the card will take a command
; in:  [pm_base] set
; out: CF = 0 ready; CF = 1 timed out, wedged, or still initialising
; clobbers: flags
;
; An ERROR or an unknown-command status is cleared with PMC_RESET and looked
; at again, which is what PMINIT does - a stale error from whatever ran before
; us is not our failure. It is spent from a budget rather than retried freely,
; because a card that answers ERROR to the reset itself would otherwise be an
; infinite loop that the tick deadline could only ever end the slow way.
;
; INIT and WAITCOM are refusals and not waits, deliberately. Both mean the
; card has not finished starting, WAITCOM means it is blocked on a USB serial
; console being attached, and neither is a state half a second is going to
; change - the machine can boot without sound and the user can retick the row.
;
; THERE ARE TWO BOUNDS AND THE SECOND IS NOT BELT AND BRACES. The tick
; deadline is the real one and it is the only one that fires on a healthy
; machine - but it is a deadline measured on a clock, and a clock that is not
; running makes `elapsed` zero for ever, which turns the loop below into the
; hang it exists to prevent.
;
; Interrupts ARE on here, and that is measured rather than assumed: an I/O
; breakpoint on 2A3h stops the guest inside pm_porttest's ramp, and while the
; live flags there are our own cli, the word pushf just pushed is the caller's
; and is still on the stack - FLAGS=F246, IF=1, return address inside pm_init
; (cycle-accurate 5150-class 8088, os8088_xt_vga_sb). So the tick advances and
; the deadline works.
;
; PM_OUTERN caps the batches anyway, because "interrupts are on at this call"
; is a fact about today's drv_boot and not an invariant anything enforces -
; and the cost of a later change breaking it silently would be the boot of
; every machine this driver loads on. It is sized to fire ONLY when the clock
; is dead: about 1.3s of polling on a 4.77MHz 8088, well past the tick
; deadline it sits under, and still four orders of magnitude more than a card
; that answers in microseconds has ever needed.
; -----------------------------------------------------------------------------
pm_wait:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call pm_ticks
    mov bx, ax                  ; BX = the tick the deadline runs from
    mov di, PM_OUTERN           ; ...and the cap that outlives a stopped clock
    mov si, PM_RESETS
    mov dx, [pm_base]
.outer:
    mov cx, PM_POLLN
.poll:
    in al, dx
    cmp al, PM_ST_READY
    je .ok
    cmp al, PM_ST_BUSY
    je .again
    cmp al, PM_ST_ERR
    je .reset
    cmp al, PM_ST_NOCMD
    jne .no                     ; INIT, WAITCOM, or a value no firmware defines
.reset:
    dec si
    jz .no
    mov al, PMC_RESET
    out dx, al
.again:
    loop .poll
    dec di                      ; PM_POLLN reads gone. The cap first, because
    jz .no                      ; it is the one that still works with no clock
    call pm_ticks               ; ...then the deadline that normally ends this
    sub ax, bx
    cmp ax, PM_TMO
    jb .outer
.no:
    stc
    jmp short .out
.ok:
    clc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pm_cmd - run one PicoMEM command and bring its answer back
; in:  AL = PMC_*, BX = the argument word; [pm_base] set
; out: CF = 0 and AX = the answer (0 = the firmware said OK, else a CMDERR_*)
;      CF = 1 the card never came ready
; clobbers: AX, flags
;
; CF = 1 does NOT mean nothing was issued, and the difference matters to
; pm_undo. There are two waits: one before the command, where a refusal really
; does mean nothing happened, and one after it, where the command went out and
; the card simply never came back to READY. A caller cannot tell them apart
; and does not need to - it gives up either way - but it means [pm_up] can be
; 0 with the DSP actually installed. That is the harmless direction: the probe
; below then finds a card that is really there and attaches to it, which is
; the outcome we wanted, reached without our knowing it worked.
;
; The argument and the answer are TWO EXPLICIT BYTE ACCESSES rather than one
; `out dx, ax`. Word I/O splits differently on the two CPUs this OS runs on -
; an 8088 always makes two byte cycles at dx and dx+1, an 8086 makes a single
; 16-bit cycle at an even port and splits at an odd one - and while base+1 is
; odd today and would therefore split on both, that is a property of 2A0h
; being the base rather than of anything this code controls. -DPM_BASE can
; move it. base+1 and base+2 are two separate 8-bit registers on the card
; (PM_CmdDataL and PM_CmdDataH), so addressing them as two is also simply the
; truth. This is the same distinction that costs the hard-disk driver its
; rung-1 gate on CPU_286 (SPEC.md 52.1).
; -----------------------------------------------------------------------------
pm_cmd:
    push bx
    push dx
    push si
    mov si, ax                  ; bank the command - AL is about to be data
    call pm_wait
    jc .no
    mov dx, [pm_base]
    inc dx
    mov al, bl                  ; base+1: the argument's low byte...
    out dx, al
    inc dx
    mov al, bh                  ; base+2: ...and its high byte
    out dx, al
    mov dx, [pm_base]
    mov ax, si
    out dx, al                  ; base+0: the command, which starts it
    call pm_wait
    jc .no
    mov dx, [pm_base]
    inc dx
    in al, dx                   ; the answer comes back the same way
    mov bl, al
    inc dx
    in al, dx
    mov bh, al
    mov ax, bx                  ; ...into AX before BX is given back
    clc
    jmp short .out
.no:
    stc
.out:
    pop si
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; pm_sb_off - put the emulated Sound Blaster back the way the card booted
; clobbers: AX, flags
; -----------------------------------------------------------------------------
pm_sb_off:
    push bx
    mov al, PMC_SBONOFF
    xor bx, bx
    call pm_cmd
    pop bx
    ret

; -----------------------------------------------------------------------------
; pm_snd_on - install the AdLib and the Sound Blaster on a card we have found
; in:  [pm_base] set
; out: CF = 0 the DSP is up at PM_SB_PORT; CF = 1 it is not
; clobbers: AX, flags
;
; The AdLib goes first and its answer is not read. The firmware's verb returns
; nothing to read - PM_Adlib_OnOff is void and the handler zeroes both data
; registers - and enabling the DSP installs an OPL2 anyway (PM_sbdsp_OnOff
; calls dev_adlib_install on success), so this call is what covers the case
; where the DSP is refused and FM is all we are going to get.
;
; Then the IRQ, and only then the port: dev_sbdsp_set_irq_dma writes the
; values into the emulation before it checks them, so the order that leaves a
; refused line behind is the order that sets the port first.
; -----------------------------------------------------------------------------
pm_snd_on:
    push bx
    push si
    mov al, PMC_ADLIB
    mov bx, 1
    call pm_cmd

    mov si, pm_irqs
.irq:
    mov bl, [si]
    or bl, bl
    jz .no                      ; the card refused every line we can use
    mov bh, PM_SB_DMA
    mov al, PMC_SBIRQDMA
    call pm_cmd
    jc .no
    or ax, ax
    jz .got                     ; 0 = taken. Anything else is the firmware
    inc si                      ; refusing this line - in practice because it
    jmp short .irq              ; is the PicoMEM's own multiplexed IRQ
.got:
    mov [pm_irq], bl            ; ...remembered for the report, not for sb.inc,
                                ; which discovers the line itself by firing F2h
                                ; at it (SPEC.md 34.5) and would be within its
                                ; rights to disagree with us
    mov al, PMC_SBONOFF
    mov bx, PM_SB_PORT
    call pm_cmd
    jc .no
    or ax, ax
    jnz .no                     ; CMDERR_PORTUSED, _NOIRQ, _MEM_ERR, _AMIX_FAIL
    pop si
    pop bx
    clc
    ret
.no:
    pop si
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; pm_init - find a PicoMEM and turn its sound hardware on
; in:  nothing
; out: nothing, and CF is UNDEFINED rather than an answer - no caller branches
;      on one. The whole point is that opl_probe and sbl_attach then run
;      exactly as they always have and answer for themselves: a card that came
;      up is found by them because it is now really there, and one that did
;      not is not there, which is the state they already handle. Reporting
;      success here would be a second opinion about a question they ask
;      better, and something would eventually have to reconcile the two.
; clobbers: flags
; -----------------------------------------------------------------------------
pm_init:
    push ax
    push bx
    push dx
    mov word [pm_base], 0
    mov byte [pm_irq], 0
    mov byte [pm_up], 0
    mov dx, PM_BASE
    call pm_porttest
    jc .out                     ; no card: nothing has been written anywhere
    mov [pm_base], dx
    call pm_snd_on
    jc .out
    mov byte [pm_up], 1
.out:
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pm_undo - the attach failed after all; put the card back
; clobbers: flags
;
; SPEC.md 51.2's all-or-nothing rule: an attach that returns CF=1 must leave
; no port in a state the machine did not boot in. Reaching here at all means
; the card took the DSP and then neither opl_probe nor sbl_attach found
; anything, which should be impossible - installing the DSP installs an OPL2
; beside it - so this is a guard against a firmware that changes, not a path
; with a known way in.
;
; It puts back the Sound Blaster and NOT the AdLib, and the asymmetry is the
; honest one rather than an oversight. The DSP is off after every reset, so
; turning it off is exactly restoring what we found. The AdLib is not - it
; comes back from the card's own saved config - so there is no way to ask what
; its state was before pm_snd_on, and switching it off could as easily be the
; change as the undo. An OPL2 nothing has keyed is silent either way.
; -----------------------------------------------------------------------------
pm_undo:
    cmp byte [pm_up], 0
    je .out
    push ax
    mov byte [pm_up], 0
    call pm_sb_off
    pop ax
.out:
    ret

; --- state and tables (a driver has no .bss - drivers/os88drv.inc) -----------
pm_irqs:    db 7, 5, 3, 0       ; sb.inc's own candidate order, less IRQ 2:
                                ; PMINIT will not offer it and an XT's line 2
                                ; is the one a card is least likely to own
pm_base:    dw 0                ; the card's command port, 0 = no card found
pm_irq:     db 0                ; the line the firmware accepted, 0 = none
pm_up:      db 0                ; 1 = we turned the DSP on and owe an undo
