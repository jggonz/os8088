; =============================================================================
; comscan - what is actually on this machine's serial ports
;
; A field diagnostic for "the mouse was not detected on real hardware"
; (SPEC.md 9.5). It answers, per port and from the machine itself, the four
; questions the kernel's probe cannot report from inside a GUI you need a
; mouse to drive:
;
;   1. is there a UART there at all, and does os8088's OWN probe find it?
;   2. what does it look like - which part, which registers, what did the
;      BIOS's POST think?
;   3. does anything TALK on it, and do the bytes decode as a Microsoft
;      mouse?
;   4. WHICH IRQ LINE does the card actually drive?
;
; (4) is the one no amount of emulator work can answer and the one most
; likely to be the bug: os8088 derives the IRQ from the base address
; (3F8 -> IRQ4, 2F8 -> IRQ3) because that is what the BIOS, DOS and every
; mouse driver assume, and a card whose jumper disagrees is invisible to it.
; It is read out of the 8259's own Interrupt Request Register rather than by
; hooking anything, so nothing here can storm or hang: the four candidate
; lines are MASKED for the capture, which is exactly what makes the IRR latch
; the request instead of it being acknowledged and cleared.
;
; It is built two ways from this one source:
;
;   comscan.com   a DOS program. Output goes through int 21h, so
;                 `COMSCAN > COMSCAN.TXT` captures the report to a file.
;   comscan.img   a BOOTABLE 360KB floppy. No DOS, no os8088, no mouse
;                 needed - the machine boots straight into this. Output
;                 goes through int 10h.
;
; NOTHING here writes to a disk, and nothing needs os8088 to be working.
;
; Assemble: nasm -f bin -DCOMFILE -o comscan.com comscan.asm   (DOS)
;           nasm -f bin           -o comscan.bin comscan.asm   (bootable)
; =============================================================================

    cpu 8086                    ; runs on the 5150 as well as the Portable III

%ifdef COMFILE
    org 0x100
%else
    org 0                       ; loaded at KERNEL_SEG:0000 by boot/boot.asm
%endif

NPORT       equ 4               ; COM1..COM4 - and os8088 only looks at two
PEND        equ NPORT*2         ; ...as the end of a word-indexed table walk
CAPSHOW     equ 20              ; bytes of each port's capture to print - 20
                                ; is what fits an 80-column line with the label
CAPBUF      equ 64              ; ...and bytes of it to keep, per port
CAPTICKS    equ 36              ; ~2s at 18.2Hz: the listen window, kept
                                ; short because irq_probe wants the mouse still
                                ; moving after it and a person will not keep
                                ; wiggling one forever
RSTLOW      equ 4               ; ticks DTR/RTS are held low (~220ms)
IRQGUARD    equ 400             ; interrupts on one line before the probe
                                ; masks it: a device we cannot quiet must not
                                ; be able to hang the machine
LOCKN       equ 8               ; MOU_LOCKN - what the kernel needs to settle
                                ; a port when two are live (SPEC.md 9.5)

%ifndef COMFILE
; -----------------------------------------------------------------------------
; The boot sector's handoff contract (boot/boot.asm), so the shipped loader can
; carry this instead of the kernel: it far-calls SPLASH_OFF = 0x0008 once the
; first sectors are aboard, writes its t=0 word at 0x000C, and far-jumps to
; 0x0000 with DL = the boot drive.
; -----------------------------------------------------------------------------
    jmp main
    times 0x08-($-$$) db 0
    retf                        ; 0x0008 - the splash tick, which we have none
    times 0x0C-($-$$) db 0      ; of, so it just returns
    dw 0                        ; 0x000C - the boot timer's t=0
%endif

; =============================================================================
main:
%ifndef COMFILE
    mov ax, cs                  ; the loader jumped here far; own every segment
    mov ds, ax                  ; and put a stack somewhere harmless (this
    cli                         ; image is a few KB and the relocated boot
    mov ss, ax                  ; sector is far above)
    mov sp, 0x7000
    sti
%endif
    push cs
    pop es
    cld

    mov si, s_title
    call puts

    ; --- what the BIOS POST decided ------------------------------------------
    ; 0040:0000..0007 is up to four base addresses, in the order the POST
    ; found them - so a machine with a card at 2F8 and nothing at 3F8 reports
    ; 02F8 in the FIRST slot. It is what DOS calls COM1..COM4, and it is worth
    ; having beside our own probe because the two disagreeing is itself an
    ; answer.
    mov si, s_bios
    call puts
    mov ax, 0x0040
    mov es, ax
    xor bx, bx
.bios:
    mov ax, [es:bx]
    call puthex16
    call putsp
    add bx, 2
    cmp bx, 8
    jb .bios
    push cs
    pop es
    call crlf

    ; --- and what the 8259s are masking --------------------------------------
    mov si, s_imr
    call puts
    in  al, 0x21
    call puthex8
    call putsp
    in  al, 0xA1                ; harmless on an XT: nothing answers and the
    call puthex8                ; read floats to FF, which is what it prints
    call crlf
    call crlf

    ; =========================================================================
    ; Phase 1 - the survey
    ; =========================================================================
    mov si, s_hdr1
    call puts
    mov si, s_hdr2
    call puts

    xor bx, bx
.survey:
    call port_survey
    add bx, 2
    cmp bx, PEND
    jb .survey
    call crlf

    ; --- what os8088 would make of that --------------------------------------
    call live_count             ; AL = live ports, and the kernel's threshold
    mov si, s_nlive
    call puts
    push ax
    xor ah, ah
    call putdec16
    pop ax
    mov si, s_nlive2
    call puts
    cmp al, 2
    mov ax, 1                   ; one port is not a contest (SPEC.md 9.5)
    jb .need1
    mov ax, LOCKN
.need1:
    call putdec16
    mov si, s_nlive3
    call puts

    ; only the first two rows are ports os8088 looks at at all, and a live
    ; port outside them is the whole bug on its own
    call warn_outside

    ; =========================================================================
    ; Phase 2 - listen
    ; =========================================================================
    mov si, s_warn
    call puts
    call getkey
    call crlf

    call cap_reset              ; 1200 7N1 + the DTR/RTS pulse, every live port
    mov si, s_move
    call puts
    call cap_run                ; poll them all for CAPTICKS, no IRQ needed
    call crlf
    call crlf

    xor bx, bx
.report:
    call port_report
    add bx, 2
    cmp bx, PEND
    jb .report

    call verdict
    call restore_all

%ifdef COMFILE
    mov ax, 0x4C00
    int 0x21
%else
    mov si, s_done
    call puts
    call getkey
    int 0x19                    ; back to the BIOS bootstrap
%endif

; =============================================================================
; port_survey - one row of the presence table, for port BX
;
; Five independent tests, because "is there a UART here" is exactly the
; question that has gone wrong and one test cannot say why:
;
;   latch   os8088's OWN probe (SPEC.md 9.5) - two values written to the
;           divisor latch with DLAB set and read back
;   latch2  the same test with a long settle between the write and the read.
;           If this passes where `latch` fails, the kernel's probe is too
;           quick for this machine's bus and THAT is the bug
;   scr     the scratch register at +7. An original 8250 has none, so a fail
;           here is informative rather than bad
;   loop    the UART's own loopback: MCR bit 4, a byte out of THR, the same
;           byte back from RBR. This is the definitive "the part works"
;   fifo    IIR's top bits with the FIFO armed - 8250/16450 vs 16550(A)
;
; The port's LCR, divisor, IER and MCR are banked before any of it and put
; back afterwards, so the survey cannot hang up a modem or change its speed.
; in:  BX = port row
; =============================================================================
port_survey:
    push ax
    push cx
    push dx
    push si

    mov si, [bx+p_name]         ; "COM1 03F8  "
    call puts
    mov ax, [bx+p_base]
    call puthex16
    call putsp
    call putsp

    call port_bank              ; bank LCR/DLL/DLM/IER/MCR before touching any

    mov cl, 0                   ; --- latch, fast (the kernel's own probe) ----
    call t_latch
    mov byte [bx+p_live], 0     ; the verdict is banked BEFORE it is printed:
    jnc .nolive                 ; put_ok goes through puts, and puts does not
    mov byte [bx+p_live], 1     ; preserve the carry it is being handed
.nolive:
    call put_ok
    mov cl, 1                   ; --- latch, with a long settle --------------
    call t_latch
    call put_ok
    call t_scratch              ; --- scratch register at +7 -----------------
    call put_ok
    call t_loop                 ; --- internal loopback ----------------------
    jnc .noloop                 ; ...same order, same reason
    mov byte [bx+p_live], 1     ; loopback passing is proof enough on its own
.noloop:
    call put_ok
    call t_fifo                 ; --- 8250/16450 vs 16550 --------------------

    call port_restore           ; put the port back BEFORE the dump, or the
                                ; MCR column shows the loopback test's own
                                ; 0x13 rather than the machine's setting - a
                                ; diagnostic reporting its own footprint
    cmp byte [bx+p_live], 0     ; a dead row prints no register dump: eight
    jne .regs                   ; columns of FF say nothing anyone needs
    mov si, s_dead
    call puts
    jmp short .out
.regs:
    mov cx, 7                   ; IER IIR LCR MCR LSR MSR SCR, raw
    mov dx, [bx+p_base]
    inc dx
.reg:
    in  al, dx
    call puthex8
    call putsp
    inc dx
    loop .reg
    mov al, [bx+p_sdlm]         ; ...and the divisor the machine was found
    call puthex8                ; with, banked before anything scribbled on
    mov al, [bx+p_sdll]         ; it. It says what baud the BIOS or DOS left
    call puthex8                ; the port at, and two ports disagreeing here
    call crlf                   ; is what broke the loopback test above
.out:
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; t_latch - the divisor-latch probe. CL = 0 for the kernel's own timing, 1 for
;           a deliberately slow one.
; out: CF = 1 if a UART answered
; -----------------------------------------------------------------------------
t_latch:
    push ax
    push dx
    mov dx, [bx+p_base]
    add dx, 3
    mov al, 0x80                ; DLAB = 1, so base+0 is the latch
    out dx, al
    sub dx, 3
    mov al, 0xAA
    call lat_try
    jnc .no
    mov al, 0x55                ; twice, so a bus that floats to the first
    call lat_try                ; value cannot pass
    jnc .no
    stc
    jmp short .out
.no:
    clc
.out:
    pop dx
    pop ax
    ret

; lat_try - write AL to DX, read it back. CL picks the settle.
; out: CF = 1 if it read back
lat_try:
    push ax
    push cx
    out dx, al
    or  cl, cl
    jnz .slow
    jmp short $+2               ; the kernel's settle: kernel/clock.inc's idiom
    jmp short .rd
.slow:
    mov cx, 2000                ; ...and a settle nothing could be too fast for
.wait:
    jmp short $+2
    loop .wait
.rd:
    mov ah, al
    in  al, dx
    cmp al, ah
    pop cx
    pop ax
    je .yes
    clc
    ret
.yes:
    stc
    ret

; -----------------------------------------------------------------------------
; t_scratch - the scratch register at +7. An original 8250 does not have one.
; out: CF = 1 if it held a value
; -----------------------------------------------------------------------------
t_scratch:
    push ax
    push dx
    mov dx, [bx+p_base]
    add dx, 7
    mov al, 0xA5
    out dx, al
    jmp short $+2
    in  al, dx
    cmp al, 0xA5
    pop dx
    pop ax
    je .yes
    clc
    ret
.yes:
    stc
    ret

; -----------------------------------------------------------------------------
; t_loop - the UART's own loopback. MCR bit 4 ties TX to RX internally, so a
;          byte written to THR comes back out of RBR with no cable, no device
;          and no baud rate involved. It is the one test that cannot be fooled
;          by what is or is not plugged in.
; out: CF = 1 if the byte came back
; -----------------------------------------------------------------------------
t_loop:
    push ax
    push cx
    push dx
    ; A KNOWN divisor first, and this is not tidiness. t_latch leaves DLL
    ; holding 0x55 and DLM holding whatever the machine had, so the loopback
    ; used to run at a baud rate that differed PER PORT by however much their
    ; as-found DLMs differed - and the bounded wait below then timed out on
    ; the slower one and reported a working UART as broken. That is exactly
    ; what it did on the Compaq Portable III: COM1 failed this test and COM2
    ; passed it, on two ports that are both ordinary 8250s. Divisor 1 is
    ; 115200 baud, so the byte is ~87us whatever the machine.
    mov dx, [bx+p_base]
    add dx, 3
    mov al, 0x80                ; DLAB on
    out dx, al
    mov dx, [bx+p_base]
    mov al, 1
    out dx, al                  ; DLL = 1
    inc dx
    xor al, al
    out dx, al                  ; DLM = 0
    add dx, 2
    mov al, 0x03                ; DLAB off, 8N1 - we only need the shift regs
    out dx, al
    inc dx                      ; MCR: LOOP | RTS | DTR
    mov al, 0x13
    out dx, al
    mov dx, [bx+p_base]
    inc dx                      ; IER = 0: no interrupts out of this
    xor al, al
    out dx, al
    dec dx
    mov al, 0x5A
    out dx, al                  ; THR
    mov cx, 4000                ; wait for LSR bit 0, bounded - a part that
.wait:                          ; never answers must not hang the machine
    push dx
    add dx, 5
    in  al, dx
    pop dx
    test al, 0x01
    jnz .got
    loop .wait
    jmp short .no
.got:
    in  al, dx                  ; RBR
    cmp al, 0x5A
    jne .no
    pop dx
    pop cx
    pop ax
    stc
    ret
.no:
    pop dx
    pop cx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; t_fifo - arm the FIFO and read IIR's top two bits back: 11 = 16550A,
;          10 = a 16550 whose FIFO does not work, 00 = 8250 or 16450.
;          Prints the part name; leaves the FIFO disabled again.
; -----------------------------------------------------------------------------
t_fifo:
    push ax
    push dx
    push si
    cmp byte [bx+p_live], 0
    je .none
    mov dx, [bx+p_base]
    add dx, 2                   ; FCR (write) / IIR (read) share the offset
    mov al, 0x01
    out dx, al
    jmp short $+2
    in  al, dx
    push ax
    xor al, al                  ; FIFO back off, whatever it answered
    out dx, al
    pop ax
    and al, 0xC0
    mov si, s_16550a
    cmp al, 0xC0
    je .say
    mov si, s_16550
    cmp al, 0x80
    je .say
    mov si, s_8250             ; 8250 or 16450 - the scratch column separates
    jmp short .say             ; them
.none:
    mov si, s_dash5
.say:
    call puts
    pop si
    pop dx
    pop ax
    ret

; =============================================================================
; port_bank / port_restore - bank a port's programming and put it back
;
; The survey scribbles the divisor and drives MCR, and one of these ports has
; a MODEM on it. Everything is banked before the first write and restored
; after the last, so a survey cannot change a modem's speed or drop a call.
; (The capture phase deliberately DOES pulse DTR - that is what it is for -
; and asks first.)
; =============================================================================
port_bank:
    push ax
    push dx
    mov dx, [bx+p_base]
    add dx, 3
    in  al, dx                  ; LCR
    mov [bx+p_slcr], al
    mov al, 0x80
    out dx, al                  ; DLAB on, to reach the divisor
    mov dx, [bx+p_base]
    in  al, dx
    mov [bx+p_sdll], al
    inc dx
    in  al, dx
    mov [bx+p_sdlm], al
    mov dx, [bx+p_base]
    add dx, 3
    mov al, [bx+p_slcr]
    out dx, al                  ; DLAB back where it was
    mov dx, [bx+p_base]
    inc dx
    in  al, dx                  ; IER
    mov [bx+p_sier], al
    add dx, 3
    in  al, dx                  ; MCR
    mov [bx+p_smcr], al
    pop dx
    pop ax
    ret

port_restore:
    push ax
    push dx
    mov dx, [bx+p_base]
    add dx, 3
    mov al, 0x80
    out dx, al
    mov dx, [bx+p_base]
    mov al, [bx+p_sdll]
    out dx, al
    inc dx
    mov al, [bx+p_sdlm]
    out dx, al
    add dx, 2
    mov al, [bx+p_slcr]
    out dx, al                  ; LCR last: it takes DLAB back down
    mov dx, [bx+p_base]
    inc dx
    mov al, [bx+p_sier]
    out dx, al
    add dx, 3
    mov al, [bx+p_smcr]
    out dx, al
    pop dx
    pop ax
    ret

restore_all:
    push bx
    xor bx, bx
.p:
    cmp byte [bx+p_live], 0
    je .next
    call port_restore
.next:
    add bx, 2
    cmp bx, PEND
    jb .p
    pop bx
    ret

; =============================================================================
; cap_reset - program every live port 1200 7N1 and give it the reset edge
;
; A serial mouse is POWERED by DTR/RTS and resets itself on the rising edge
; (SPEC.md 9.4), so this is the same drop-hold-raise the kernel does - and it
; is what makes the mouse announce itself with 'M'.
; =============================================================================
cap_reset:
    push ax
    push bx
    push cx
    push dx

    xor bx, bx                  ; --- 1200 7N1, interrupts armed -------------
.prog:
    cmp byte [bx+p_live], 0
    je .pnext
    call port_bank              ; bank again: the survey put it all back
    mov dx, [bx+p_base]
    add dx, 3
    mov al, 0x82                ; DLAB + 7N1
    out dx, al
    mov dx, [bx+p_base]
    mov al, 96                  ; 115200/96 = 1200
    out dx, al
    inc dx
    xor al, al
    out dx, al
    add dx, 2
    mov al, 0x02                ; DLAB off, 7N1
    out dx, al
    mov dx, [bx+p_base]
    inc dx
    xor al, al                  ; IER = 0. The capture is POLLED and wants no
    out dx, al                  ; interrupts at all - and it must not provoke
                                ; any, because a request raised here latches in
                                ; the 8259's IRR (masked, so never acknowledged
                                ; and never cleared) and would then be in
                                ; irq_probe's baseline. Measured that way the
                                ; card's own line is subtracted away and every
                                ; port reports NONE. irq_probe arms IER itself,
                                ; for one port, for as long as it takes
    add dx, 3
    mov al, 0x08                ; MCR: OUT2 only - DTR and RTS low. OUT2 stays
    out dx, al                  ; up so the IRQ gate never glitches
.pnext:
    add bx, 2
    cmp bx, PEND
    jb .prog

    mov cx, RSTLOW              ; --- hold low ------------------------------
    call sleep

    xor bx, bx                  ; --- and raise: the mouse's reset -----------
.raise:
    cmp byte [bx+p_live], 0
    je .rnext
    call port_drain             ; clear the survey's leftovers BEFORE the edge,
                                ; not after: a Microsoft mouse answers the
                                ; raise with 'M', and draining afterwards
                                ; swallowed exactly the byte worth having
    mov dx, [bx+p_base]
    add dx, 4
    mov al, 0x0B                ; DTR|RTS|OUT2
    out dx, al
.rnext:
    add bx, 2
    cmp bx, PEND
    jb .raise

    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; cap_run - poll every live port for CAPTICKS and record what arrives
;
; POLLED, not interrupt-driven, and that separation is the whole point: this
; proves whether the device TALKS independently of whether its interrupt is
; wired where os8088 believes. The IRQ question is answered beside it, out of
; the 8259's Interrupt Request Register, which needs no handler at all.
;
; The four candidate lines are masked for the duration. That is not caution
; about our own handler - we install none - it is what makes the test work:
; a masked request is never acknowledged, so the IRR LATCHES it and a single
; read afterwards says which line the card drove. It also means a card whose
; interrupt nobody services cannot storm the machine while we listen.
; =============================================================================
cap_run:
    push ax
    push bx
    push cx
    push dx
    push si

    in  al, 0x21                ; bank the master's mask, then hold IRQ 3, 4,
    mov [saved_imr], al         ; 5 and 7 down. IRQ0 (tick) is untouched -
    or  al, 0xB8                ; every timing loop here needs it
    out 0x21, al

    call ticknow
    mov [cap_t0], ax
.loop:
    xor bx, bx
.port:
    cmp byte [bx+p_live], 0
    je .pnext
    mov dx, [bx+p_base]
    add dx, 5
    in  al, dx                  ; LSR: anything waiting?
    test al, 0x01
    jz .pnext

    mov dx, [bx+p_base]         ; take the byte. Which IRQ line the card
    in  al, dx                  ; drives is asked separately, by irq_probe -
    call cap_byte               ; sampling it here and ORing across three
                                ; seconds just accumulates the timer and the
                                ; keyboard, which is how this first reported
                                ; every candidate line at once
.pnext:
    add bx, 2
    cmp bx, PEND
    jb .port

    mov ah, 1                   ; a key aborts early
    int 0x16
    jnz .stop
    call ticknow
    sub ax, [cap_t0]
    cmp ax, CAPTICKS
    jb .loop
.stop:
    mov al, [saved_imr]         ; the mask back exactly as the BIOS left it
    out 0x21, al

    call irq_flush              ; spend what was latched before we arrived...
    xor bx, bx
.irq:                           ; ...then ask each live port which line it
    cmp byte [bx+p_live], 0     ; drives, one at a time so the answer is
    je .inext                   ; attributable
    call irq_probe
.inext:
    add bx, 2
    cmp bx, PEND
    jb .irq

    xor bx, bx                  ; ...and every port quiet again
.quiet:
    cmp byte [bx+p_live], 0
    je .qnext
    mov dx, [bx+p_base]
    inc dx
    xor al, al
    out dx, al                  ; IER = 0
.qnext:
    add bx, 2
    cmp bx, PEND
    jb .quiet

    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; irq_probe - WHICH IRQ LINE does port BX's card actually drive?
;
; The question os8088 cannot ask itself and no emulator can answer for a real
; card: the kernel DERIVES the IRQ from the base address (3F8 -> IRQ4,
; 2F8 -> IRQ3) because that is what the BIOS, DOS and every mouse driver
; assume, and a card jumpered anywhere else is invisible to it forever - it
; will be probed, programmed, hooked, and then never interrupt.
;
; So this measures the interrupt the way the kernel CONSUMES it: hook all four
; candidate vectors, arm the RX interrupt on this port ALONE, unmask, and see
; which handler runs. Arming one port at a time is what makes the answer
; attributable - no other UART can raise anything while it happens.
;
; WHAT WAS TRIED FIRST, AND WHY IT IS NOT HERE: reading the 8259's Interrupt
; Request Register with the lines masked, which needs no handler and cannot
; storm. It does not work. A masked request is never acknowledged, and an 8259
; clears IRR on acknowledgement - so a line that was asserted once before we
; arrived reads as 1 for the rest of time. Measured on QEMU, base, hot and
; cold readings around a byte were 18h, 18h and 18h: both serial lines stuck
; up, the card's own contribution unobservable, and every port reporting that
; it raised no interrupt at all. Watching the bit go away on the drain fails
; the same way. The register is a latch, not a probe.
;
; The runaway guard is what makes hooking safe: a line whose interrupt we
; cannot quiet - somebody else's device on a shared line - is masked from
; inside the handler after IRQGUARD of them, so the worst case is a brief
; burst rather than a hung machine.
; in:  BX = port row
; -----------------------------------------------------------------------------
irq_probe:
    push ax
    push cx
    push dx
    push si

    mov byte [bx+p_irqok], 0
    mov byte [bx+p_irrm], 0
    mov ax, [bx+p_base]
    mov [irq_port], ax          ; the handler drains THIS port, which is what
                                ; makes its UART drop the request
    mov word [irq_cnt], 0
    mov word [irq_cnt+2], 0
    mov word [irq_cnt+4], 0
    mov word [irq_cnt+6], 0
    call port_drain
    call irq_hook

    in  al, 0x21                ; unmask the four candidates, with our own
    mov [irq_savmask], al       ; handlers behind them
    and al, 0x47
    out 0x21, al

    mov dx, [bx+p_base]         ; ...and arm THIS port's RX interrupt. Every
    inc dx                      ; other port stays at IER = 0, so anything
    mov al, 0x01                ; that fires now belongs to this one
    out dx, al
    sti

    call ticknow
    mov cx, ax
.wait:
    call irq_any
    jnz .got
    call ticknow
    sub ax, cx
    cmp ax, 27                  ; ~1.5s, then give up and say so
    jb .wait
    jmp short .off
.got:
    mov byte [bx+p_irqok], 1
.off:
    mov dx, [bx+p_base]         ; disarm before unmasking anything back
    inc dx
    xor al, al
    out dx, al
    mov al, [irq_savmask]
    out 0x21, al
    call irq_unhook

    cmp byte [bx+p_irqok], 0    ; fold the counters into a bit per line, the
    je .out                     ; same shape name_irqs already prints
    mov si, 0
    xor dl, dl
.fold:
    cmp word [si+irq_cnt], 0    ; SI and not BP: [bp+..] addresses SS, and
    je .fnext                   ; only luck (SS = DS here) would make it work
    mov al, [si+irq_bit]
    or  dl, al
.fnext:
    add si, 2
    cmp si, 8
    jb .fold
    mov [bx+p_irrm], dl
.out:
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; irq_flush - deliver and acknowledge whatever was already latched
;
; The candidate lines are masked at boot, and a masked request is never
; acknowledged - so anything that asserted one before we arrived is still
; sitting in the 8259's IRR. Unmasking hands all of it over at once, and
; whichever port is probed FIRST collects it: measured on QEMU, COM1 reported
; IRQ3 and IRQ4 without a single byte having arrived on it.
;
; So the stale requests are spent once, up front, against no port at all, and
; the counters are thrown away. Edge-triggered lines do not re-fire without a
; new edge, so one pass is enough.
; -----------------------------------------------------------------------------
irq_flush:
    push ax
    push cx
    mov word [irq_port], 0      ; no port: the handler drains nothing
    call irq_hook
    in  al, 0x21
    mov [irq_savmask], al
    and al, 0x47
    out 0x21, al
    sti
    mov cx, 2                   ; ~110ms is plenty for a delivery each
    call sleep
    mov al, [irq_savmask]
    out 0x21, al
    call irq_unhook
    pop cx
    pop ax
    ret

; irq_any - ZF = 0 if any candidate line has fired
irq_any:
    push ax
    mov ax, [irq_cnt]
    or  ax, [irq_cnt+2]
    or  ax, [irq_cnt+4]
    or  ax, [irq_cnt+6]
    or  ax, ax
    pop ax
    ret

; irq_hook / irq_unhook - the four vectors, saved and put back exactly
irq_hook:
    push ax
    push bx
    push es
    push si
    xor ax, ax
    mov es, ax
    xor bx, bx
.h:
    mov si, [bx+irq_vec]        ; the IVT offset of this line
    cli
    mov ax, [es:si]
    mov [bx+irq_soff], ax
    mov ax, [es:si+2]
    mov [bx+irq_sseg], ax
    mov ax, [bx+irq_isr]
    mov [es:si], ax
    mov [es:si+2], cs
    sti
    add bx, 2
    cmp bx, 8
    jb .h
    pop si
    pop es
    pop bx
    pop ax
    ret

irq_unhook:
    push ax
    push bx
    push es
    push si
    xor ax, ax
    mov es, ax
    xor bx, bx
.h:
    mov si, [bx+irq_vec]
    cli
    mov ax, [bx+irq_soff]
    mov [es:si], ax
    mov ax, [bx+irq_sseg]
    mov [es:si+2], ax
    sti
    add bx, 2
    cmp bx, 8
    jb .h
    pop si
    pop es
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; the four handlers. Each names its own row and falls into one body: count it,
; quiet the port under test, and mask this line if it will not be quieted.
; -----------------------------------------------------------------------------
irq_isr3:
    push bx
    mov bx, 0
    jmp short irq_body
irq_isr4:
    push bx
    mov bx, 2
    jmp short irq_body
irq_isr5:
    push bx
    mov bx, 4
    jmp short irq_body
irq_isr7:
    push bx
    mov bx, 6
irq_body:
    push ax
    push cx
    push dx
    push ds
    mov ax, cs
    mov ds, ax
    inc word [bx+irq_cnt]

    mov dx, [irq_port]          ; take the byte, so the UART drops its request
    or  dx, dx                  ; ...unless this is the flush pass, which has
    jz .noch                    ; no port and must not read I/O port 5
    add dx, 5
    in  al, dx
    test al, 0x01
    jz .noch
    sub dx, 5
    in  al, dx
.noch:
    cmp word [bx+irq_cnt], IRQGUARD
    jb .eoi
    mov cl, [bx+irq_bit]        ; this line will not go quiet and is not ours
    in  al, 0x21                ; to service: hold it down and get out
    or  al, cl
    out 0x21, al
.eoi:
    mov al, 0x20
    out 0x20, al
    pop ds
    pop dx
    pop cx
    pop ax
    pop bx
    iret

; port_drain - read port BX's RBR until nothing is waiting, so no request is
;              left asserted. Bounded: a UART answering FF to everything would
;              otherwise spin here forever.
port_drain:
    push ax
    push cx
    push dx
    mov cx, 64
.l:
    mov dx, [bx+p_base]
    add dx, 5
    in  al, dx
    test al, 0x01
    jz .out
    mov dx, [bx+p_base]
    in  al, dx
    loop .l
.out:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cap_byte - one received byte (AL) on port BX: keep it, and run it through
;            the SAME state machine the kernel does (SPEC.md 9.5), so the
;            report can say not just "bytes arrived" but "these bytes would
;            have settled the port" - or exactly why they would not.
; -----------------------------------------------------------------------------
cap_byte:
    push ax
    push bx
    push si

    mov si, [bx+p_count]
    or  si, si
    jne .keep
    mov [bx+p_id], al           ; the first byte after the reset edge: 'M' is
.keep:                          ; a Microsoft mouse identifying itself
    cmp si, CAPBUF
    jae .nobuf
    push ax
    mov ax, bx                  ; buffer is CAPBUF bytes a port, and BX is the
    mov cl, 5                   ; word row - so row*32 = row/2*CAPBUF
    shl ax, cl
    add si, ax
    pop ax
    mov [si+p_buf], al
.nobuf:
    inc word [bx+p_count]

    test al, 0x40               ; --- the packet machine ---------------------
    jz .cont
    cmp byte [bx+p_phase], 0    ; a bit-6 byte MID-packet is a resync, which a
    je .b0                      ; real mouse never causes
    inc word [bx+p_viol]
    mov byte [bx+p_run], 0
.b0:
    mov byte [bx+p_phase], 1
    jmp short .out
.cont:
    cmp byte [bx+p_phase], 1
    jne .chk2
    mov byte [bx+p_phase], 2
    jmp short .out
.chk2:
    cmp byte [bx+p_phase], 2    ; a bit-6-CLEAR byte between packets is a
    je .pkt                     ; stray, and equally impossible
    inc word [bx+p_viol]
    mov byte [bx+p_run], 0
    jmp short .out
.pkt:
    mov byte [bx+p_phase], 0
    inc word [bx+p_pkts]
    inc byte [bx+p_run]
    mov al, [bx+p_run]          ; the high-water mark is the number that
    cmp al, [bx+p_maxrun]       ; matters: reaching LOCKN is what settles a
    jbe .out                    ; port when two are live
    mov [bx+p_maxrun], al
.out:
    pop si
    pop bx
    pop ax
    ret

; =============================================================================
; port_report - everything the capture learned about port BX
; =============================================================================
port_report:
    push ax
    push cx
    push dx
    push si

    cmp byte [bx+p_live], 0
    je .out
    mov si, [bx+p_name]
    call puts
    mov ax, [bx+p_base]
    call puthex16
    mov si, s_colon
    call puts

    mov ax, [bx+p_count]
    call putdec16
    mov si, s_bytes
    call puts

    cmp word [bx+p_count], 0
    jne .talked
    mov si, s_silent
    call puts
    jmp .out
.talked:
    mov si, s_id                ; the identify byte, raw and as a character
    call puts
    mov al, [bx+p_id]
    call puthex8
    mov si, s_idq
    call puts
    mov al, [bx+p_id]
    call putprint
    mov si, s_idq2
    call puts

    mov si, s_pkts
    call puts
    mov ax, [bx+p_pkts]
    call putdec16
    mov si, s_viol
    call puts
    mov ax, [bx+p_viol]
    call putdec16
    mov si, s_maxrun
    call puts
    mov al, [bx+p_maxrun]
    xor ah, ah
    call putdec16
    call crlf

    mov si, s_irq               ; --- which line did it actually drive? ------
    call puts
    cmp byte [bx+p_irqok], 0
    jne .haveirq
    mov si, s_irqno
    call puts
    jmp short .bytes
.haveirq:
    call name_irqs
    cmp byte [bx+p_irrm], 0
    jne .irqdone
    mov si, s_irqnone           ; a byte was pending and NO line came up: the
    call puts                   ; card is not driving any IRQ at all
.irqdone:
    call crlf
.bytes:

    mov si, s_first             ; --- and the bytes themselves ---------------
    call puts
    mov cx, [bx+p_count]
    cmp cx, CAPSHOW
    jbe .n
    mov cx, CAPSHOW
.n:
    mov ax, bx
    push cx
    mov cl, 5
    shl ax, cl
    pop cx
    mov si, ax
    add si, p_buf
.dump:
    lodsb
    call puthex8
    call putsp
    loop .dump
    call crlf
.out:
    pop si
    pop dx
    pop cx
    pop ax
    ret

; name_irqs - spell out which of the candidate lines are set in port BX's
;             accumulated IRR. IRQ0/1/6 are the tick, the keyboard and the
;             floppy: they are always busy and naming them would be noise.
name_irqs:
    push ax
    push cx
    push dx
    push si
    mov dl, [bx+p_irrm]
    mov cx, 4
    mov si, irq_list
.l:
    push cx
    mov al, [si]                ; the bit...
    test dl, al
    jz .no
    mov al, [si+1]              ; ...and the number that goes with it
    add al, '0'
    push si
    mov si, s_irqp
    call puts
    pop si
    call putc
.no:
    add si, 2
    pop cx
    loop .l
    pop si
    pop dx
    pop cx
    pop ax
    ret

; =============================================================================
; verdict - say the thing out loud, so a photograph of the screen is enough
; =============================================================================
verdict:
    push ax
    push bx
    push si
    call crlf
    mov si, s_vhdr
    call puts

    xor bx, bx
    mov byte [found], 0
.p:
    cmp word [bx+p_pkts], 0
    je .next
    mov al, [bx+p_maxrun]
    cmp al, LOCKN               ; would the kernel have settled on this port?
    jb .weak
    mov si, [bx+p_name]
    call puts
    mov si, s_vmouse
    call puts
    mov byte [found], 1
    jmp short .next
.weak:
    mov si, [bx+p_name]
    call puts
    mov si, s_vweak
    call puts
.next:
    add bx, 2
    cmp bx, PEND
    jb .p

    cmp byte [found], 0
    jne .out
    mov si, s_vnone
    call puts
.out:
    mov si, s_vtail
    call puts
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; live_count - AL = how many of the rows OS8088 PROBES came back live. Rows 2
;              and 3 are deliberately not counted: the kernel never looks at
;              3E8 or 2E8, so they cannot affect its threshold. warn_outside
;              is what says they exist.
; -----------------------------------------------------------------------------
live_count:
    push bx
    xor al, al
    xor bx, bx
.p:
    cmp bx, 4
    jae .done
    cmp byte [bx+p_live], 0
    je .next
    inc al
.next:
    add bx, 2
    cmp bx, PEND
    jb .p
.done:
    pop bx
    ret

; -----------------------------------------------------------------------------
; warn_outside - os8088 only knows 3F8 and 2F8 (SPEC.md 9.5). A live UART at
;                3E8 or 2E8 is a port the kernel never probes, never hooks and
;                never listens to - which would be the entire bug, so it is
;                said in as many words rather than left to be inferred from a
;                table.
; -----------------------------------------------------------------------------
warn_outside:
    push ax
    push bx
    push si
    mov bx, 4                   ; rows 2 and 3 = COM3 and COM4
.p:
    cmp byte [bx+p_live], 0
    je .next
    mov si, s_outside
    call puts
    mov si, [bx+p_name]
    call puts
    mov si, s_outside2
    call puts
.next:
    add bx, 2
    cmp bx, PEND
    jb .p
    pop si
    pop bx
    pop ax
    ret

; =============================================================================
; small helpers
; =============================================================================

; sleep - CX BIOS ticks (~55ms each), wrap-safe
sleep:
    push ax
    push dx
    call ticknow
    mov dx, ax
.w:
    call ticknow
    sub ax, dx
    cmp ax, cx
    jb .w
    pop dx
    pop ax
    ret

; ticknow - AX = the BIOS tick count's low word (0040:006C)
ticknow:
    push es
    push bx
    mov bx, 0x0040
    mov es, bx
    mov ax, [es:0x006C]
    pop bx
    pop es
    ret

getkey:
    push ax
    xor ax, ax
    int 0x16
    pop ax
    ret

; putc - AL to the console. Under DOS that is stdout, so the whole report
;        redirects into a file; standalone it is the BIOS teletype.
putc:
    push ax
    push bx
    push dx
%ifdef COMFILE
    mov dl, al
    mov ah, 0x02
    int 0x21
%else
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
%endif
    pop dx
    pop bx
    pop ax
    ret

; putprint - AL as a character if it is printable, '.' if not
putprint:
    push ax
    cmp al, 0x20
    jb .dot
    cmp al, 0x7E
    jbe .ok
.dot:
    mov al, '.'
.ok:
    call putc
    pop ax
    ret

puts:                           ; DS:SI, NUL-terminated
    push ax
    push si
.l:
    lodsb
    or  al, al
    jz .out
    call putc
    jmp short .l
.out:
    pop si
    pop ax
    ret

crlf:
    push ax
    mov al, 13
    call putc
    mov al, 10
    call putc
    pop ax
    ret

putsp:
    push ax
    mov al, ' '
    call putc
    pop ax
    ret

puthex8:                        ; AL
    push ax
    push cx
    push ax
    mov cl, 4
    shr al, cl
    call .nib
    pop ax
    call .nib
    pop cx
    pop ax
    ret
.nib:
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .p
    add al, 7
.p:
    call putc
    ret

puthex16:                       ; AX
    push ax
    mov al, ah
    call puthex8
    pop ax
    call puthex8
    ret

putdec16:                       ; AX, no leading zeros
    push ax
    push bx
    push cx
    push dx
    mov bx, 10
    xor cx, cx
.div:
    xor dx, dx
    div bx
    push dx
    inc cx
    or  ax, ax
    jnz .div
.out:
    pop ax
    add al, '0'
    call putc
    loop .out
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; put_ok - CF as a padded "ok" / "--" column
put_ok:
    push ax
    push si
    mov si, s_no
    jnc .say
    mov si, s_yes
.say:
    call puts
    pop si
    pop ax
    ret

; =============================================================================
; the port table - one row per candidate, word-indexed like the kernel's
; =============================================================================
p_base      dw 0x3F8, 0x2F8, 0x3E8, 0x2E8
p_name      dw s_com1, s_com2, s_com3, s_com4

irq_list    db 0x08, 3          ; bit, IRQ number - the four lines a serial
            db 0x10, 4          ; card can plausibly be jumpered to
            db 0x20, 5
            db 0x80, 7
irq_vec     dw 0x2C, 0x30, 0x34, 0x3C   ; ...their IVT offsets (int 0Bh..0Fh)
irq_bit     db 0x08,0, 0x10,0, 0x20,0, 0x80,0  ; ...and their 8259 mask bits,
                                        ; strided 2 to match the word index
irq_isr     dw irq_isr3, irq_isr4, irq_isr5, irq_isr7
irq_cnt     dw 0,0,0,0          ; how many times each fired
irq_soff    dw 0,0,0,0          ; the vectors we displaced
irq_sseg    dw 0,0,0,0
irq_port    dw 0                ; the port under test - what a handler drains
irq_savmask db 0

; Per-row state, walked with the same word index BX as the tables above
; (0, 2, 4, 6). A WORD array is naturally that shape - four words ARE eight
; bytes - but a BYTE array indexed that way uses every OTHER byte, so it needs
; NPORT*2 and not NPORT. Declared four bytes each, every one of these silently
; overflowed into the next: p_live[2] was p_phase[0], so a dead COM3 read as
; live and the whole survey was nonsense. The kernel's own two-port arrays
; (kernel/mouse.inc) are four bytes and correct, which is exactly how the
; wrong idiom got copied here.
p_live      times NPORT*2 db 0
p_phase     times NPORT*2 db 0
p_run       times NPORT*2 db 0
p_maxrun    times NPORT*2 db 0
p_id        times NPORT*2 db 0
p_irrm      times NPORT*2 db 0      ; the IRR bits this port's UART raised...
p_irrs      times NPORT*2 db 0      ; ...on the slave 8259,
p_irqok     times NPORT*2 db 0      ; ...whether the probe got an answer at all
p_slcr      times NPORT*2 db 0
p_sdll      times NPORT*2 db 0
p_sdlm      times NPORT*2 db 0
p_sier      times NPORT*2 db 0
p_smcr      times NPORT*2 db 0
p_count     dw 0,0,0,0
p_pkts      dw 0,0,0,0
p_viol      dw 0,0,0,0
p_buf       times NPORT*CAPBUF db 0

saved_imr   db 0
cap_t0      dw 0
found       db 0

; =============================================================================
s_title     db 13,10,'os8088 comscan - serial port survey (SPEC.md 9.5)',13,10
            db '=================================================',13,10,13,10,0
s_bios      db 'BIOS POST found (40:00h): ',0
s_imr       db '8259 mask (IMR) master/slave: ',0
s_hdr1      db 'port base  latch lat2  scr   loop  part   IER IIR LCR MCR LSR MSR SCR DIV',13,10,0
s_hdr2      db '---- ----  ----- ----  ----  ----  -----  --- --- --- --- --- --- --- ----',13,10,0
s_com1      db 'COM1 ',0
s_com2      db 'COM2 ',0
s_com3      db 'COM3 ',0
s_com4      db 'COM4 ',0
s_yes       db 'ok    ',0
s_no        db '--    ',0
s_8250      db '8250   ',0
s_16550     db '16550  ',0
s_16550a    db '16550A ',0
s_dash5     db '--     ',0
s_dead      db 'nothing answers here',13,10,0

s_nlive     db 13,10,'os8088 would find ',0
s_nlive2    db ' live port(s) of the two it looks at, so it needs ',0
s_nlive3    db 13,10,'clean packets in a row to settle one (SPEC.md 9.5).',13,10,0
s_outside   db 13,10,'*** ',0
s_outside2  db 'is LIVE and os8088 never probes it. The kernel only knows',13,10
            db '    3F8 and 2F8 - a mouse on this port cannot be found at all.',13,10,0

s_warn      db 13,10,'Next: every live port is set to 1200 7N1 and its DTR/RTS is pulsed.',13,10
            db 'That is what makes a serial mouse announce itself - and it will',13,10
            db 'HANG UP a modem call in progress. Press a key to go on.',13,10,0
s_move      db 13,10,'*** MOVE THE MOUSE NOW - and KEEP MOVING IT until the report',13,10
            db '    appears (about 6 seconds). The second half of that is the',13,10
            db '    IRQ test, which needs a byte to arrive to have anything to',13,10
            db '    measure. ***',13,10,0

s_colon     db ': ',0
s_bytes     db ' bytes  ',0
s_silent    db '(silent)',13,10,0
s_id        db 'first=',0
s_idq       db " '",0
s_idq2      db "'  ",0
s_pkts      db 'pkts ',0
s_viol      db '  viol ',0
s_maxrun    db '  best clean run ',0
s_irq       db '     IRQ line: ',0
s_irqno     db 'NO INTERRUPT ARRIVED on IRQ 3, 4, 5 or 7',13,10,0
s_irqnone   db ' none',0
s_irqp      db 'IRQ',0
s_first     db '     bytes: ',0

s_vhdr      db 'VERDICT',13,10,'-------',13,10,0
s_vmouse    db 'is a MOUSE, and os8088 would settle on it.',13,10,0
s_vweak     db 'sent packets but never a long enough clean run - see IRR/violations.',13,10,0
s_vnone     db 'No port sent a single mouse packet. Either nothing is plugged in,',13,10
            db 'the port is not one of the four scanned, or the mouse is not being',13,10
            db 'powered (check the DTR/RTS lines and any adapter).',13,10,0
s_vtail     db 13,10,'Send this whole report back with the machine name.',13,10,0
%ifndef COMFILE
s_done      db 13,10,'Press a key to reboot.',13,10,0
%endif
