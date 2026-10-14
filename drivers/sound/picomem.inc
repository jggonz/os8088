; =============================================================================
; os8088 - SOUND.DRV, the PicoMEM tier (SPEC.md 34.10)
;
; In every SOUND.DRV since SPEC.md 34.10.3; `make NOPICOMEM=1` builds the
; driver without one byte of this file. On a machine with no card it costs two
; reads of 2A3h at attach and no heap (the driver claims 7 KB either way).
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
; reset scan - unless the card's own CMS emulation already holds 220h-22Fh,
; when the firmware answers PORTUSED and the next of sbl_bases' order is
; asked instead (SPEC.md 34.10.2). That leaves the IRQ, and the card is the
; only thing that knows which lines are free: dev_sbdsp_set_irq_dma refuses
; its own line, so the candidates are offered in sb.inc's own order and the
; first the firmware accepts is the one taken. Nothing here is a guess that could have been a
; question.
;
; SAFETY, and the reason this may ship enabled on a machine with no PicoMEM
; in it at all. Detection is READS ONLY. pm_init's ramp reads 2A3h a hundred
; times and requires every read to be exactly one more than the last, which
; is the card's test register counting; an empty ISA bus floats to FFh and
; fails on the second read, and no other card in the period answers a
; hundred-long ramp by accident. Not one byte is WRITTEN anywhere until that
; has passed, so on a machine without the card this file costs a few hundred
; microseconds of port reads at attach and changes nothing.
;
; THE CARD'S OWN IRQ IS THE ONE LINE NOTHING ELSE MAY HAVE (SPEC.md 34.10.1).
; A PicoMEM 1.x has ONE interrupt jumper, and its BIOS hooks that line with a
; MULTIPLEXER: on it the card asks the CPU to copy each 128-byte DMA block
; (a 1.x has no DMA line - "DMA emulation" is that copy), and from it the
; card's other IRQs - the Sound Blaster's among them - are raised as SOFTWARE
; interrupts, `int 8+n`, and only when line n's 8259 mask bit is clear.
; sb.inc's IRQ discovery used to swap every candidate vector 7/5/3/2 for a
; stub and fire F2h, so on a PicoMEM the multiplexer's own vector was
; replaced, the stub took the card's request as the SB's, and sbl_isr was
; hooked onto the PicoMEM's line for good: no block was ever copied and the
; card was never acknowledged. That is the "it does not work" of the first
; attempt. So the line is LEARNED here and discovery is SKIPPED on a card we
; configured - we chose its IRQ, so there is nothing to discover.
;
; It is learned through int 13h AX=6000h DX=1234h, PMINIT's detect - which
; this file refused while it was the DETECTION, because on a machine with no
; PicoMEM it is an undefined int 13h function. It is not the detection now:
; it runs only after pm_init's ramp has passed, on a machine that has the
; card. The call also puts the multiplexer BACK if anything took its vector
; (pmbios's PM BIOS function 0 re-hooks and unmasks the line when the vector
; no longer names its segment). The base port stays 0x2A0 by assumption -
; pmbios/pm_hw.asm assigns PM_BasePort 0x2A0 and nothing makes it a variable -
; and -DPM_BASE covers the day the firmware makes it configurable.
;
; EVERY LOOP IS BOUNDED AND EVERY DEADLINE IS IN TICKS. The status poll is
; the one place a wedged card could hang the boot, and PMINIT's own version
; of it is a `while(true)`. A poll count is not a deadline here for the
; reason NET.DRV had to learn (SPEC.md 62.10): a poll is ~15us on a 4.77MHz
; 8088 and ~1us on anything newer, so a count tuned on one is the wrong
; timeout on the other. The clock is OSAPI_GET_TICKS, asked once per 256
; status reads (pm_wait), so its 46.7us far call is ~1% of a batch.
; =============================================================================

%ifndef PM_BASE
  %define PM_BASE 0x2A0         ; pmbios/pm_hw.asm's PM_BasePort
%endif
%ifndef PM_SB_PORT
  %define PM_SB_PORT 0x220      ; the head of sbl_bases (SPEC.md 34.2)
%endif

PM_SB_DMA    equ 1              ; NOT a choice - see the header. On a
                                ; PicoMEM 1.x it is not one for the card
                                ; either: the BIOS's copy handler rewrites
                                ; channel 1's address and count (ports 02h/
                                ; 03h) and no other. A 2.x does real DMA on
                                ; the channel its DMA JUMPER names, so that
                                ; jumper must be on 1 (SPEC.md 34.10.1)

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
PM_E_PORTUSED equ 0x10          ; CMDERR_PORTUSED: another emulated device
                                ; holds that port block (pm_lib.h)

PM_TESTN     equ 100            ; ramp reads the test register must answer
PM_POLLN     equ 256            ; status reads between two clock checks
PM_TMO       equ 9              ; ...and the deadline, in ticks (~0.5s)
PM_OUTERN    equ 512            ; ...under a hard cap on the batches themselves
PM_RESETS    equ 4              ; how many wedged-status resets we will spend

; -----------------------------------------------------------------------------
; pm_wait - poll the command status until the card will take a command
; in:  [pm_base] set
; out: CF = 0 ready; CF = 1 timed out, wedged, or still initialising
; clobbers: AX, CX, DX, DI, flags - BX and SI are pm_cmd's caller's and kept
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
; So the dispatch is three compares: READY leaves (the equal compare is CF =
; 0), BUSY polls again, ERR and NOCMD (2 and 3) reset, and anything above 3 -
; INIT, WAITCOM, a value no firmware defines - refuses.
;
; THERE ARE TWO BOUNDS AND THE SECOND IS NOT BELT AND BRACES. The tick
; deadline is the real one and it is the only one that fires on a healthy
; machine - but it is a deadline measured on a clock, and a clock that is not
; running makes `elapsed` zero for ever, which turns the loop below into the
; hang it exists to prevent.
;
; Interrupts ARE on here, and that is measured rather than assumed: an I/O
; breakpoint on 2A3h stops the guest inside pm_init's ramp, and while the
; live flags there are our own cli, the word pushf just pushed is the caller's
; and is still on the stack - FLAGS=F246, IF=1 (cycle-accurate 5150-class
; 8088, os8088_xt_vga_sb, measured when the ramp was a routine of its own;
; its flags are snd_entry's either way). So the tick
; advances and the deadline works. The clock is OSAPI_GET_TICKS, once per
; PM_POLLN reads - its cell is `mov ax, [cs:ticks]` / `retf`, which the
; kernel's IRQ0 advances, and sbl_f_irqdisc already times its F2h wait on it
; in this same attach (kernel size pass 11 took pm_ticks' 0040:006C reader
; out for it: one far call per 256 polls, -10 bytes).
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
    push bx
    push si
    call OSAPI_GET_TICKS
    xchg ax, bx                 ; BX = the tick the deadline runs from
    mov di, PM_OUTERN           ; ...and the cap that outlives a stopped clock
    mov si, PM_RESETS
    mov dx, [pm_base]
.outer:
    mov cx, PM_POLLN
.poll:
    in al, dx
    cmp al, PM_ST_READY
    je .out                     ; CF = 0: the compare was equal
    cmp al, PM_ST_BUSY
    je .again
    cmp al, PM_ST_NOCMD
    ja .no                      ; INIT, WAITCOM, or a value no firmware defines
    dec si                      ; ERR or NOCMD: clear it, from the budget
    jz .no
    mov al, PMC_RESET
    out dx, al
.again:
    loop .poll
    dec di                      ; PM_POLLN reads gone. The cap first, because
    jz .no                      ; it is the one that still works with no clock
    call OSAPI_GET_TICKS        ; ...then the deadline that normally ends this
    sub ax, bx
    cmp ax, PM_TMO
    jb .outer
.no:
    stc
.out:
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; pm_cmd - run one PicoMEM command and bring its answer back
; in:  AL = PMC_*, BX = the argument word; [pm_base] set
; out: CF = 0 and AX = the answer (0 = the firmware said OK, else a CMDERR_*)
;      CF = 1 the card never came ready
; clobbers: AX, CX, DX, DI, flags (BX and SI kept: pm_init's loops ride them)
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
    push ax                     ; bank the command - AL is about to be data
    call pm_wait
    pop ax                      ; (pop writes no flag)
    jc .out
    push ax
    inc dx                      ; DX = base+0, as pm_wait left it
    mov al, bl                  ; base+1: the argument's low byte...
    out dx, al
    inc dx
    mov al, bh                  ; base+2: ...and its high byte
    out dx, al
    pop ax
    dec dx
    dec dx
    out dx, al                  ; base+0: the command, which starts it
    call pm_wait
    jc .out
    inc dx                      ; (inc writes no CF: pm_wait's 0 stands)
    in al, dx                   ; the answer comes back the same way
    mov ah, al
    inc dx
    in al, dx
    xchg al, ah                 ; AX = high:low
.out:
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
;
; THREE STEPS IN ONE BODY, each only reached if the one before answered - they
; were pm_porttest, pm_bios and pm_snd_on, three single-caller routines that
; banked registers three times over (kernel size pass 11 wrote them here).
; [pm_base], [pm_pmirq], [pm_irq] and [pm_up] are 0 without a store: the image
; was loaded a moment ago and ATTACH is the first verb it gets, the same
; guarantee snd_entry's [drv_up] rests on (os88drv.inc's OS88_STATE) - and a
; tier change re-runs sbl_attach, never this.
;
; 1. THE RAMP. base+3 is the card's test register: every read answers one more
; than the last one. THIS IS THE ONLY THING THAT RUNS ON A MACHINE THAT HAS NO
; PICOMEM, and it is why nothing below writes to a port before it has passed.
; It runs with interrupts off. The register is stateful - reading it IS what
; advances it - so an interrupt handler that touched the same port mid-ramp
; would break a good card's test, and on a machine where the PicoMEM BIOS has
; its own IRQ installed that is not hypothetical. The window is ~100 ISA
; reads, well under a millisecond, against a 55ms tick.
;
; 2. THE CARD'S OWN IRQ LINE (the header's multiplexer). PM BIOS function 0
; answers DX = AA55h, BX = the BIOS segment and CH = BV_IRQ (pmbios.asm's
; PM_InstallIRQ13 table, checked against the shipped ROM's disassembly). On
; the way it re-hooks the line if its vector no longer names that segment, and
; unmasks it when it does. We then unmask it again ourselves, but ONLY if the
; vector is the BIOS's: opening a line whose vector is somebody else's is how
; a stray becomes a crash. Lines 3, 5 and 7 only - a 1.x jumpers nothing else,
; and a 2.x's line 6 is the floppy's. NEVER asked before the ramp: on a machine
; with no PicoMEM AX=6000h is undefined (the header).
;
; 3. THE AdLib AND THE SOUND BLASTER. The AdLib goes first and its answer is
; not read. The firmware's verb returns nothing to read - PM_Adlib_OnOff is
; void and the handler zeroes both data registers - and enabling the DSP
; installs an OPL2 anyway (PM_sbdsp_OnOff calls dev_adlib_install on
; success), so this call is what covers the case where the DSP is refused and
; FM is all we are going to get. Then the IRQ, and only then the port:
; dev_sbdsp_set_irq_dma writes the values into the emulation before it checks
; them, so the order that leaves a refused line behind is the order that sets
; the port first.
; -----------------------------------------------------------------------------
pm_init:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov dx, PM_BASE + 3
    mov cx, PM_TESTN
    pushf
    cli
    in al, dx
    mov bh, al                  ; the ramp's starting point, whatever it is
.ramp:
    in al, dx
    inc bh
    cmp al, bh
    jne .noramp
    loop .ramp
    popf
    mov word [pm_base], PM_BASE

    mov ax, 0x6000              ; --- 2: the card's own line ---------------
    mov dx, 0x1234
    int 0x13
    cmp dx, 0xAA55
    jne .snd                    ; no PM BIOS answering: line unknown
    mov al, ch                  ; AL = CL = the line
    test al, 0xF8               ; 0..7 only - and `test` clears CF, which
    jnz .snd                    ; is what n = 0 reads below, shifting nothing
    mov cl, al
    mov ah, 0x54                ; bits 2, 4 and 6: `shr` by n leaves bit n-1
    shr ah, cl                  ; in CF, so CF = 1 for n = 3, 5 and 7 alone
    jnc .snd
    mov [pm_pmirq], al
    cbw                         ; AH = 0 (n < 8)
    shl ax, 1
    shl ax, 1
    xchg ax, si                 ; SI = 4n, and int 8+n's cell is 20h further
    xor ax, ax
    mov es, ax
    cmp [es:si+0x22], bx        ; still the BIOS's multiplexer?
    jne .snd
    mov ah, 0xFE
    rol ah, cl                  ; every bit but line n's
    pushf
    cli
    in al, 0x21
    and al, ah                  ; ...then its line is open
    out 0x21, al
    popf

.snd:                           ; --- 3: the AdLib, then the SB's IRQ -------
    mov al, PMC_ADLIB
    mov bx, 1
    call pm_cmd
    mov si, pm_irqs
.irq:
    lodsb
    or al, al
    jz .out                     ; the card refused every line we can use
    cmp al, [pm_pmirq]          ; never the card's OWN line: the firmware
    je .irq                     ; refuses it too, but this costs no command
    mov ah, PM_SB_DMA
    xchg ax, bx                 ; BL = the line, BH = the channel
    mov al, PMC_SBIRQDMA
    call pm_cmd
    jc .out
    or ax, ax
    jnz .irq                    ; refused - in practice because it is the
                                ; PicoMEM's own multiplexed IRQ
    mov [pm_irq], bl            ; 0 = taken, and it is what sb.inc HOOKS:
                                ; discovery's F2h stubs would take the
                                ; multiplexer's own vector (header), so
                                ; sbl_f_irqdisc skips the probe and takes this
                                ; line when [pm_up]
    mov si, pm_sbports          ; THE PORT, AND IT IS NOT ALWAYS 220h
.port:                          ; (SPEC.md 34.10.2): the card's CMS emulation
    lodsw                       ; sits on 220h-22Fh when it is enabled, and
    xchg ax, bx                 ; the firmware refuses a port another of its
    or bx, bx                   ; devices holds. PMINIT is TOLD a port by the
    jz .out                     ; BLASTER variable; we walk sb.inc's own scan
    mov al, PMC_SBONOFF         ; order instead, so whichever base is taken
    call pm_cmd                 ; is one sbl_f_probe finds next
    jc .out
    cmp ax, PM_E_PORTUSED       ; only "that port is taken" is worth another
    je .port                    ; base - _NOIRQ, _MEM_ERR and _AMIX_FAIL are
    or ax, ax                   ; the same answer at any port
    jnz .out
    inc byte [pm_up]            ; 0 -> 1: the DSP is up and we owe an undo
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.noramp:
    popf                        ; no card: nothing has been written anywhere
    jmp short .out

; -----------------------------------------------------------------------------
; pm_undo - the attach failed after all; put the card back
; clobbers: flags
;
; SPEC.md 51.2's all-or-nothing rule: an attach that returns CF=1 must leave
; no port in a state the machine did not boot in. Reaching here at all means
; the card took the DSP and then neither opl_probe nor sbl_attach found
; anything, which should be impossible - installing the DSP installs an OPL2
; beside it - so this is a guard against a firmware that changes, not a path
; with a known way in. [pm_up] is not cleared: the refusal frees the image.
;
; It puts back the Sound Blaster and NOT the AdLib, and the asymmetry is the
; honest one rather than an oversight. The DSP is off after every reset, so
; turning it off is exactly restoring what we found. The AdLib is not - it
; comes back from the card's own saved config - so there is no way to ask what
; its state was before pm_init, and switching it off could as easily be the
; change as the undo. An OPL2 nothing has keyed is silent either way.
; -----------------------------------------------------------------------------
pm_undo:
    cmp byte [pm_up], 0
    je .out
    push ax
    push bx
    push cx
    push dx
    push di
    mov al, PMC_SBONOFF         ; 0: the emulated Sound Blaster off, the way
    xor bx, bx                  ; the card booted
    call pm_cmd
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; pm_runaway - is DMA channel 1 RACING while a stream plays? (SPEC.md 34.10.5)
; in:  a playing stream, IF = 0 (sbl_tick, inside snd_tick's IRQ0 leaf)
; out: [sbl_drqmask] = 4 and the channel masked, if so; clobbers: flags
;
; On the field 286 a PicoMEM 1.x - which drives no DMA line - has channel 1
; transferring at bus speed for as long as its DSP plays: the count read eight
; times back to back fell ~113 bytes a read, and the CPU got 9.6% of an idle
; second. Masked, it got 94% and Tracker played clean (docs/FIELD-NOTES.md
; 66). The line is quiet while the DSP is idle, which is why this is asked of
; a PLAYING stream and not at attach. The 1.x does not need the channel: its
; firmware copies from the registers it snooped and never reads the mask. A
; 2.x does - it DMAs on DRQ1 - so the test is the RATE: two reads of the
; count ~100 ISA bus cycles apart (the bus sets their pace, whatever the
; CPU), and a fall of more than PM_RACE bytes is a channel no DSP is pacing.
; Real DMA at 44 kHz moves ~5 bytes in that window. A reload between the two
; reads (the count going UP) decides nothing; the next tick asks again.
; Only on a card pm_init configured, only until it has fired, and only for
; the first PM_RCHK ticks after a stream starts or resumes ([pm_rchk], loaded
; in sb.inc): a channel that has not raced by then is a 2.x's real DMA or a
; machine where it does not race, and the question costs ~0.6 ms of IRQ0.
; -----------------------------------------------------------------------------
PM_RACE      equ 32             ; bytes in ~100 bus cycles: a racing channel
PM_RACEN     equ 100            ; ...and the bus cycles between the two reads
PM_RCHK      equ 18             ; ...asked on this many ticks a stream: a second

pm_runaway:
    cmp byte [pm_up], 0
    je .ret
    cmp byte [sbl_drqmask], 0
    jne .ret
    cmp byte [pm_rchk], 0
    je .ret
    dec byte [pm_rchk]
    push ax
    push bx
    push cx
    call .cnt
    mov bx, ax                  ; BX = the count now
    mov cx, PM_RACEN
.w:
    in al, 0x61                 ; one ISA bus cycle each - port B, whose read
    loop .w                     ; changes nothing (the 8237's status at 08h
                                ; clears its terminal-count bits)
    call .cnt
    sub bx, ax                  ; BX = how far it fell
    jb .out                     ; it rose: a reload between the reads
    cmp bx, PM_RACE
    jbe .out
    mov byte [sbl_drqmask], 4   ; racing: masked, and every start keeps it so
    mov al, 0x05
    out 0x0A, al
.out:
    pop cx
    pop bx
    pop ax
.ret:
    ret
.cnt:                           ; AX = channel 1's current count
    out 0x0C, al                ; flip-flop clear (any value)
    in al, 0x03
    mov ah, al
    in al, 0x03
    xchg al, ah
    ret

; -----------------------------------------------------------------------------
; pm_tick - SOUND.DRV's DSV_TICK on a machine pm_init found (SPEC.md 34.10.6)
; in:  IF = 0, inside IRQ0 (snd_tick's leaf); out: nothing; clobbers: AX, flags
;
; THE CARD STOPS MIXING ITS AdLib AFTER FOUR SECONDS WITHOUT AN ADDRESS WRITE,
; and a note that is still keyed is then silent. The firmware's once-a-second
; housekeeping (src/pm_cmd.cpp) disables the OPL mix when dev_adlib.delay
; reaches 4, and the ONLY thing that zeroes that counter is a write to the
; address port (dev_adlib.cpp, case 0 - a DATA write does not). So a held
; tone - Clear Skies' engine, which is re-issued only when its note moves -
; sounded for 4-5 seconds and went quiet until the throttle changed it, and
; every long FM note did the same. A real OPL2 has no such timer.
;
; THE CURE IS THE THING THE FIRMWARE COUNTS: one byte to 388h every
; PM_KEEPT ticks, and only while some channel is KEYED, which opl_b0 already
; records for both the tone voice and every claimer's channel (SNDV_OPLW keeps
; it too). An idle card is still left to switch itself off. It is only an
; address select and it is safe at any instant this can run: every opl_wr
; makes its select and its data write in one IF=0 window, so a tick cannot
; land between a select and the data meant for it, and the next opl_wr selects
; again. It is never done on a machine without the card - pm_tickpub publishes
; this only when pm_init's ramp passed - so a real chip's 23us write recovery
; is never at stake.
; -----------------------------------------------------------------------------
PM_KEEPT     equ 36             ; ~2s: half the firmware's 4s, so a missed or
                                ; late tick still lands inside it

pm_tick:
    inc byte [pm_keepc]
    cmp byte [pm_keepc], PM_KEEPT
    jb .sb
    mov byte [pm_keepc], 0
    push cx
    push si
    mov si, opl_b0
    mov cx, OPL_NCH
.k:
    test byte [si], 0x20        ; KEY-ON
    jnz .kick
    inc si
    loop .k
    jmp short .out
.kick:
    push dx
    mov dx, 0x388
    mov al, 0xB8                ; the tone voice's key register: any index
    out dx, al                  ; would do, the next opl_wr re-selects
    pop dx
.out:
    pop si
    pop cx
.sb:
    jmp sbl_tick                ; the Sound Blaster's own duties, which ask
                                ; [sbl_str_act] first and so cost a compare
                                ; on a machine whose DSP tier is off

; -----------------------------------------------------------------------------
; pm_tickpub - DSV_TICK is pm_tick on a card pm_init found; SI = the table
; -----------------------------------------------------------------------------
pm_tickpub:
    cmp word [pm_base], 0
    je .out
    mov word [si+DSV_TICK], pm_tick
.out:
    ret

; --- state and tables (a driver has no .bss - drivers/os88drv.inc) -----------
pm_irqs:    db 7, 5, 3, 0       ; sb.inc's own candidate order, less IRQ 2:
                                ; PMINIT will not offer it and an XT's line 2
                                ; is the one a card is least likely to own.
                                ; sbl_f_irqdisc maps n to candidate (7-n)/2,
                                ; which holds for these three and not for 2
pm_base:    dw 0                ; the card's command port, 0 = no card found
pm_pmirq:   db 0                ; the card's OWN (multiplexer) IRQ, 0 = unknown
pm_sbports: dw PM_SB_PORT, 0x240, 0x230, 0x250, 0x260, 0
                                ; PM_SB_PORT first, then sbl_bases' order less
                                ; 210h, which PMINIT does not offer either
pm_irq:     db 0                ; the line the firmware accepted, 0 = none
pm_up:      db 0                ; 1 = we turned the DSP on and owe an undo
pm_keepc:   db 0                ; pm_tick's count toward PM_KEEPT
pm_rchk:    db 0                ; pm_runaway's ticks left this stream
