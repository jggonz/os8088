; =============================================================================
; os8088 - DEBUG.DRV
;
; The loadable debug monitor (SPEC.md 58): a serial command channel into a
; running machine, so a host-side tool can read and write memory, read and
; write I/O ports, and be told what the kernel thinks - on 86Box, which has
; no automation socket of any kind, and on real iron, which has no debugger
; at all (docs/FIELD-MACHINES.md).
;
; WHY THIS IS A DRIVER AND NOT A KERNEL KNOB. The obvious build is a
; `SERDBG=1` kernel, the DISKCNT=1 shape (SPEC.md 18.94) - and it has one
; fault that matters: a knob kernel is a DIFFERENT BINARY, so what you
; debugged is not what ships. A driver is loaded into the shipped kernel,
; from the shipped disk, by a tick in the Control Panel, and costs a machine
; that never ticks it exactly nothing (SPEC.md 51.3: no row is wanted by
; default). It also costs the kernel no bytes of debug code at all - the
; whole of it is here, on the floppy, in a file the user can delete.
;
; PORT OWNERSHIP, AND WHY IT IS COM4 (SPEC.md 58.2). os8088 owns BOTH the
; ports it knows about: SPEC.md 9.5 probes 3F8 and 2F8, programs every UART
; that answers, hooks both IRQs and retires the loser only once the mouse has
; spoken. A monitor on 2F8 would therefore be fighting the mouse prober for
; its own port, and losing in a way that reads as a mouse bug. 3E8 and 2E8 are
; the two the kernel has NEVER probed - tests/comscan surveys them precisely
; because a live UART there is invisible to os8088 - so 2E8 is a port this
; driver can own outright, with IRQ3 free whenever no card answers at 2F8.
;
; That last clause is a real condition and not an assumption, so ATTACH
; TESTS IT, through SPEC.md 57's debug registry: 0060:000E names a list of
; (tag, offset) pairs, 'MO' is the mouse instrument (SPEC.md 9.4.2), and its
; block points at mou_bases - two words, one per probed port, ZERO where no
; UART answered. A nonzero second word means the mouse module has a UART at
; 2F8 and has hooked IRQ3, and this driver REFUSES rather than stealing a
; vector out from under it. A machine that needs both wants the debug port
; moved, which is DBG_BASE/DBG_IRQ below and a rebuild.
;
; THE SPLIT DIVISOR, WHICH IS THE WHOLE BANDWIDTH STORY (SPEC.md 58.4).
; Receive is what caps the baud rate: an 8250 has no FIFO and an 8088's
; interrupt response is ~61 clocks - about 13us at 4.77MHz - so a byte time
; shorter than that is a guaranteed overrun and 115200 (8.7us) cannot be
; received at all. Transmit has no such limit, because it is POLLED: the
; sender waits for THRE and a guest too slow to keep up simply goes slower.
; One divisor sets both directions, so the fix is to change it between them:
; commands arrive at 9600 and `s` switches to 115200 for a bulk reply. The
; host end pays nothing to follow, because under 86Box's `pipe` chardev and
; QEMU's socket chardev the host side is a FIFO with no baud rate at all;
; only a real cable has to call termios, and tools/os88dbg.py does.
;
; NO WORKER TASK, AND THAT IS DELIBERATE. The SDK offers OSAPI_DRV_TASK and
; this does not use it: a worker would need a detach handshake that cannot
; spin (the scheduler has a cooperative mode, SPEC.md 8.2) and cannot yield
; safely either, since detach arrives from a Control Panel click with the gfx
; lock held. The command runs in the ISR instead, with interrupts RE-ENABLED
; after the EOI and re-entry excluded by [dbg_busy] - so the tick, the mouse
; and every background task keep running through a reply that takes a quarter
; of a second to clock out. Being an interrupt rather than a task is also
; what lets this answer on a machine whose tasks have all stopped, which is
; most of the reason to want it.
;
; WHAT IT DELIBERATELY CANNOT DO. There is no `call` verb and no disk
; payload channel. Both need the scheduler lock - a far call into kernel code
; can land anywhere, and a raw int 13h runs the BIOS's handler and its IRQ
; nesting on whichever 256-byte task stack is current (SPEC.md 8), which is
; what hard froze the 5150 when tests/sysbench did it (docs/FIELD-NOTES.md
; 10) - and [sch_lock] has no API slot. Adding one is kernel code; until
; somebody asks for it, `s` plus a polled transmit is the bulk path and it
; needs nothing.
;
; There is no .bss and no far code: a driver's zeroed data ships inside its
; image (drivers/os88drv.inc).
; =============================================================================

%include "os88drv.inc"

OS88_DRIVER 'Debug', DRVC_DEBUG, dbg_entry

; --- the port ----------------------------------------------------------------
; COM4 by the IBM assignment 86Box and QEMU both use (86Box's serial.h:
; COM4_ADDR 0x2e8, COM4_IRQ 3). Change BOTH of the first two together: the
; base decides nothing about the line, but the vector and the 8259 mask are
; derived from the IRQ and a mismatch is a driver that attaches and is never
; called.
DBG_BASE    equ 0x2E8
DBG_IRQ     equ 3
DBG_VEC     equ (8 + DBG_IRQ) * 4   ; IRQ3 -> int 0Bh -> IVT offset 0x2C
DBG_IMASK   equ 1 << DBG_IRQ        ; its bit in the master 8259's OCW1 (0x21)

; --- 8250 register offsets ---------------------------------------------------
U_RBR       equ 0               ; read: the received byte
U_THR       equ 0               ; write: the byte to send
U_DLL       equ 0               ; ...and the divisor low, with DLAB set
U_IER       equ 1
U_DLM       equ 1               ; ...divisor high, with DLAB set
U_IIR       equ 2
U_LCR       equ 3
U_MCR       equ 4
U_LSR       equ 5
U_MSR       equ 6

LCR_8N1     equ 0x03            ; 8 data bits, no parity, 1 stop
LCR_DLAB    equ 0x80
IER_RX      equ 0x01            ; received-data-available only: this driver
                                ; never wants a THRE interrupt, because every
                                ; byte it sends is sent by polling
MCR_DTR     equ 0x01
MCR_RTS     equ 0x02
MCR_OUT2    equ 0x08            ; the AND gate that lets the UART's interrupt
                                ; reach the bus at all on a PC
LSR_DR      equ 0x01            ; a byte is waiting in RBR
LSR_THRE    equ 0x20            ; the transmit holding register is empty

; 1,843,200 / 16 = 115,200, so the divisor is 115200/baud.
DBG_DIV     equ 12              ; 9600 - see the header: the RX ceiling
DBG_MAXDIV  equ 96              ; ...and 1200, the slowest this offers

DBG_LINE    equ 80              ; input line buffer, bytes. A command is a
                                ; letter and at most four hex fields; the
                                ; length exists to bound `M`, whose payload is
                                ; the only variable-length input
DBG_TXWAIT  equ 20000           ; polled-THRE spins before giving up on a byte.
                                ; A bounded wait, for viddet.inc's reason: the
                                ; one way to hang a machine is to wait forever
                                ; for a bit that never changes on hardware
                                ; where every read is 0FFh

; =============================================================================
; The entry proc
; =============================================================================

; -----------------------------------------------------------------------------
; dbg_entry - attach / detach / ready (SPEC.md 51.2)
; in:  AL = DRVV_*; DS = CS = ours, ES = KERNEL_SEG
; out: attach: CF = 0 and SI = the service table, or CF = 1 = nothing here
;      detach: nothing
; -----------------------------------------------------------------------------
dbg_entry:
    cmp al, DRVV_DETACH
    je dbg_detach
    cmp al, DRVV_READY
    je dbg_ready
    cmp al, DRVV_ATTACH
    je dbg_attach
    clc                         ; a verb we do not implement is not a failure
    ret

; -----------------------------------------------------------------------------
; dbg_attach - all or nothing (SPEC.md 51.2)
;
; Order matters and it is the SDK's rule rather than a preference: every test
; that can refuse runs before anything is hooked, so a refusal leaves the
; machine byte for byte as it was. The kernel frees this image the moment we
; say no, and a vector or an unmasked line left behind outlives us.
; -----------------------------------------------------------------------------
dbg_attach:
    mov byte [dbg_up], 0
    mov byte [dbg_busy], 0
    mov byte [dbg_len], 0
    mov word [dbg_nline], 0

    call dbg_irq_free           ; 1. is our IRQ the kernel's? (SPEC.md 9.4.2)
    jc .busy
    call dbg_probe              ; 2. is there a UART at DBG_BASE?
    jc .no

    call dbg_uart_init          ; 3. commit: the line, then the vector, then
    call dbg_hook               ;    the mask - so the ISR is installed before
                                ;    anything can drive the line

    mov byte [dbg_up], 1
    mov si, dbg_banner          ; announce, so a host tool watching the pipe
    call dbg_puts               ; sees the machine arrive rather than having
    call dbg_crlf               ; to guess when it is ready
    mov si, dbg_services
    clc
    ret
.busy:
    mov al, DRVE_BUSY           ; the UART is THERE and its line is not ours.
    stc                         ; 'No hardware found' would send the reader
    ret                         ; looking for a card that is sitting in the
                                ; machine (SPEC.md 58.2)
.no:
    mov al, DRVE_HW
    stc
    ret

; -----------------------------------------------------------------------------
; dbg_ready - DRVV_READY. Nothing to do: this driver spawns no worker and
;             registers no volume, so it has nothing that has to wait for the
;             publication fence to be armed.
; -----------------------------------------------------------------------------
dbg_ready:
    clc
    ret

; -----------------------------------------------------------------------------
; dbg_detach - cannot fail (SPEC.md 51.2)
;
; The reverse of attach, and the ORDER is the whole of its correctness: mask
; the line first so no new interrupt can arrive, then wait out a command that
; is already running, and only then put the vector back. Restoring the vector
; under a live ISR points int 0Bh at the kernel's old handler halfway through
; our own frame.
;
; The wait is a bounded spin and it is safe in BOTH scheduler modes, which a
; worker task's teardown would not have been: the thing it waits for is an
; interrupt handler, not a task, so it does not need the scheduler to run.
; -----------------------------------------------------------------------------
dbg_detach:
    cmp byte [dbg_up], 0
    je .out

    in  al, 0x21                ; 1. mask IRQ3
    or  al, DBG_IMASK
    out 0x21, al

    mov dx, DBG_BASE + U_IER    ; 2. and silence the UART's interrupt at the
    xor al, al                  ;    source, so a card whose line is shared or
    out dx, al                  ;    whose mask we got wrong still says nothing

    mov cx, 0                   ; 3. wait out a reply in flight. 65,536 spins
.wait:                          ;    with interrupts enabled is milliseconds on
    cmp byte [dbg_busy], 0      ;    any machine and the ISR only ever holds
    je .idle                    ;    the flag for the length of one reply
    loop .wait
.idle:
    call dbg_unhook             ; 4. the vector, last
    mov dx, DBG_BASE + U_MCR
    xor al, al                  ; drop DTR/RTS/OUT2: the line is nobody's now
    out dx, al
    mov byte [dbg_up], 0
.out:
    ret

; =============================================================================
; Attach's three tests
; =============================================================================

; -----------------------------------------------------------------------------
; dbg_irq_free - does the kernel's mouse module already own our IRQ?
; in:  ES = KERNEL_SEG
; out: CF = 0 free, CF = 1 taken (or the published block did not check out)
; clobbers: AX, BX, SI
;
; SPEC.md 57's registry names the 'MO' block (SPEC.md 9.4.2), whose second
; word points at mou_bases - two words, one per probed port, ZERO where no
; UART answered. So "does the kernel have a live UART at 2F8" is three
; indirections and a compare, and it is exactly the question that decides
; whether IRQ3 is ours: mouse_init hooks a port's vector and unmasks its line
; only for a row whose base survived the probe.
;
; It went through a FIXED WORD at 0060:0006 until the registry replaced it,
; which is worth knowing because the two failure modes differ: a fixed word
; that has been reassigned reads as a valid pointer to something else, and
; the registry's tag check turns that into a clean "not published".
;
; It is READ-ONLY, deliberately. Zeroing that word would hand us the port -
; mou_pall no-ops on a zeroed row, mou_lockon skips it, mouse_unhook skips it
; - and it would also be a driver writing kernel state through a block
; published for reading. Refusing is honest and the port is movable.
; -----------------------------------------------------------------------------
DBG_REG_AT  equ 0x000E          ; 0060:000E -> the registry (SPEC.md 57)
MOU_DBG_BAS equ 2               ; ...and +2 of the 'MO' block -> mou_bases

dbg_irq_free:
%if DBG_IRQ == 3 || DBG_IRQ == 4
    mov ax, DBG_TAG_MOUSE
    call dbg_dbgfind            ; SI -> the mouse block, or CF = 1
    jc .free                    ; not published: an older or smaller kernel,
                                ; and nothing here to conflict with
    mov si, [es:si+MOU_DBG_BAS] ; -> mou_bases
  %if DBG_IRQ == 3
    cmp word [es:si+2], 0       ; row 1 = 2F8, and 2F8 is IRQ3
  %else
    cmp word [es:si], 0         ; row 0 = 3F8, and 3F8 is IRQ4
  %endif
    jne .taken
%endif
.free:
    clc
    ret
%if DBG_IRQ == 3 || DBG_IRQ == 4
.taken:
    stc
    ret
%endif

; -----------------------------------------------------------------------------
; dbg_dbgfind - look a block up in the debug registry (SPEC.md 57)
; in:  AX = the DBG_TAG_* wanted, ES = KERNEL_SEG
; out: CF = 0 and SI = the block's offset in KERNEL_SEG; CF = 1 = not published
; clobbers: SI (and flags)
;
; `tests/sysbench`'s sb_dbgfind, in a driver. The two are deliberately the
; same shape and neither is shared code: SPEC.md 57 rule 2 is that a reader
; which cannot find its block says so and continues, and a dozen lines
; duplicated is a cheaper price than a reader that cannot be dropped into a
; kernel without one.
;
; The TAG CHECK at the end is the whole reason the format carries the tag
; twice. This is a pointer read out of a hard-coded address in another
; module's segment; verifying that it lands on something that names itself is
; the only thing standing between "an older kernel" and executing a garbage
; offset as a table.
; -----------------------------------------------------------------------------
dbg_dbgfind:
    push bx
    mov si, [es:DBG_REG_AT]
    or  si, si
    jz .none                    ; no registry: a kernel older than SPEC.md 57
.scan:
    mov bx, [es:si]
    or  bx, bx
    jz .none                    ; end of list
    cmp bx, ax
    je .hit
    add si, 4
    jmp short .scan
.hit:
    mov si, [es:si+2]
    cmp [es:si], ax             ; ...and it must lead with the tag it claimed
    jne .none
    pop bx
    clc
    ret
.none:
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; dbg_probe - is there a UART at DBG_BASE?
; out: CF = 0 present, CF = 1 nothing there
; clobbers: AX, DX
;
; The DIVISOR LATCH, not the scratch register at +7: an original 8250 has no
; scratch register, and this OS targets machines that have original 8250s
; (docs/FIELD-MACHINES.md's 5150 reads 8250 on both ports). Two different
; values written and read back through DLAB prove a real register rather than
; a floating bus, which answers 0FFh to everything and would otherwise read
; as a card.
;
; It leaves DLAB SET and the divisor holding the second test value, which
; dbg_uart_init immediately overwrites - the two are called in that order and
; only in that order.
; -----------------------------------------------------------------------------
dbg_probe:
    mov dx, DBG_BASE + U_LCR
    mov al, LCR_DLAB
    out dx, al

    mov dx, DBG_BASE + U_DLL
    mov al, 0xA5
    out dx, al
    in  al, dx
    cmp al, 0xA5
    jne .none
    mov al, 0x5A
    out dx, al
    in  al, dx
    cmp al, 0x5A
    jne .none
    clc
    ret
.none:
    mov dx, DBG_BASE + U_LCR    ; put LCR back the way we found it before
    xor al, al                  ; saying no: attach must leave no port in a
    out dx, al                  ; state the machine did not boot in
    stc
    ret

; -----------------------------------------------------------------------------
; dbg_uart_init - the line, at [dbg_div]
; clobbers: AX, DX
; -----------------------------------------------------------------------------
dbg_uart_init:
    mov dx, DBG_BASE + U_LCR
    mov al, LCR_DLAB
    out dx, al
    mov dx, DBG_BASE + U_DLL
    mov al, [dbg_div]
    out dx, al
    mov dx, DBG_BASE + U_DLM
    xor al, al                  ; every divisor this offers fits a byte
    out dx, al
    mov dx, DBG_BASE + U_LCR
    mov al, LCR_8N1             ; ...and DLAB back off
    out dx, al

    mov dx, DBG_BASE + U_RBR    ; drain whatever the line collected while it
    in  al, dx                  ; was being programmed, or the first command
    mov dx, DBG_BASE + U_LSR    ; arrives with a garbage byte in front of it
    in  al, dx
    mov dx, DBG_BASE + U_MSR
    in  al, dx

    mov dx, DBG_BASE + U_MCR
    mov al, MCR_DTR | MCR_RTS | MCR_OUT2
    out dx, al
    mov dx, DBG_BASE + U_IER
    mov al, IER_RX
    out dx, al
    ret

; -----------------------------------------------------------------------------
; dbg_hook / dbg_unhook - our ISR into the IVT, and the old one back
; clobbers: AX, ES (dbg_hook restores ES to KERNEL_SEG for its caller)
;
; Both run inside one pushf/cli window, because a vector is a far pointer in
; two words and an interrupt taken between them dispatches through half of
; each - SPEC.md 1's rule, and the reason mouse.inc writes its vectors the
; same way.
; -----------------------------------------------------------------------------
dbg_hook:
    push bx
    xor ax, ax
    mov es, ax
    mov bx, DBG_VEC
    pushf
    cli
    mov ax, [es:bx]
    mov [dbg_oldoff], ax
    mov ax, [es:bx+2]
    mov [dbg_oldseg], ax
    mov word [es:bx], dbg_isr
    mov [es:bx+2], cs
    popf

    in  al, 0x21                ; unmask, last: the ISR is in place
    and al, ~DBG_IMASK & 0xFF
    out 0x21, al

    mov ax, KERNEL_SEG          ; the SDK's contract for every entry is
    mov es, ax                  ; ES = KERNEL_SEG, and attach continues here
    pop bx
    ret

dbg_unhook:
    push bx
    push es
    xor ax, ax
    mov es, ax
    mov bx, DBG_VEC
    pushf
    cli
    mov ax, [dbg_oldoff]
    mov [es:bx], ax
    mov ax, [dbg_oldseg]
    mov [es:bx+2], ax
    popf
    pop es
    pop bx
    ret

; =============================================================================
; The ISR (SPEC.md 58.3)
; =============================================================================

; -----------------------------------------------------------------------------
; dbg_isr - IRQ3: collect a line, then run it
;
; Two halves with a deliberate seam between them. The first is a proper ISR:
; interrupts off, drain every byte the UART has, EOI, done. The second runs a
; complete command with INTERRUPTS ON - it can take a quarter of a second to
; clock a reply out at 9600 baud, and holding IF=0 for that long would lose
; the tick, the mouse and any sound refill.
;
; [dbg_busy] is what makes the seam safe. It is set before the sti and
; cleared after the reply, so a byte arriving mid-reply takes the ISR, is
; collected into the buffer, and returns without re-entering the executor.
; Note it is tested by dbg_detach too, which is why it is cleared LAST.
;
; The stack is whichever task was running (SPEC.md 8, 256 bytes with a
; canary): this pushes ten words and the executor keeps its buffers in the
; image, so the deepest this ever gets is well inside the slice - and it is
; the same discipline mou_isr already lives by on the same stacks.
; -----------------------------------------------------------------------------
dbg_isr:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    mov ax, cs
    mov ds, ax
    cld

.drain:
    mov dx, DBG_BASE + U_LSR
    in  al, dx
    test al, LSR_DR
    jz .eoi
    mov dx, DBG_BASE + U_RBR
    in  al, dx
    call dbg_collect
    jmp short .drain

.eoi:
    mov al, 0x20                ; non-specific EOI to the master, before the
    out 0x20, al                ; long half: the line is free from here

    cmp byte [dbg_rdy], 0
    je .done
    cmp byte [dbg_busy], 0      ; a command already running: leave the new
    jne .done                   ; line queued and let that one finish
    mov byte [dbg_busy], 1
    mov byte [dbg_rdy], 0
    sti                         ; ...and now the machine runs again
    call dbg_exec
    cli
    mov byte [dbg_busy], 0

.done:
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    iret

; -----------------------------------------------------------------------------
; dbg_collect - one received byte into the line buffer
; in:  AL = the byte
; clobbers: BX
;
; CR ends a line and LF is ignored, so a host may send either convention.
; An over-long line is TRUNCATED rather than wrapped: a wrapped buffer would
; execute the tail of a command as though it were the whole of one, and the
; failure of a debugger has to be visible.
; -----------------------------------------------------------------------------
dbg_collect:
    cmp al, 10
    je .out                     ; LF: never significant
    cmp al, 13
    je .end
    cmp byte [dbg_rdy], 0     ; a completed line not yet run: drop, rather
    jne .out                    ; than mixing two commands in one buffer
    mov bl, [dbg_len]
    cmp bl, DBG_LINE - 1
    jae .out                    ; full: truncate
    xor bh, bh
    mov [bx+dbg_buf], al
    inc byte [dbg_len]
.out:
    ret
.end:
    cmp byte [dbg_len], 0
    je .out                     ; a bare CR is not a command
    mov bl, [dbg_len]
    xor bh, bh
    mov byte [bx+dbg_buf], 0    ; NUL-terminate for the parser
    mov byte [dbg_len], 0
    mov byte [dbg_rdy], 1
    inc word [dbg_nline]
    ret

; =============================================================================
; The command executor (SPEC.md 58.5)
; =============================================================================

; -----------------------------------------------------------------------------
; dbg_exec - run the line in dbg_buf and send its reply
; in:  DS = CS; interrupts ON; [dbg_busy] held
; clobbers: everything but the ISR's saved frame
;
; The protocol is deliberately line-at-a-time ASCII rather than a binary
; framing: it is typeable by hand into a terminal, which is what you want at
; three in the morning on a machine that will not boot, and a lost byte
; costs one command rather than resynchronisation.
; -----------------------------------------------------------------------------
dbg_exec:
    mov si, dbg_buf
    lodsb
    cmp al, 'v'
    je dbg_c_ver
    cmp al, 'm'
    je dbg_c_read
    cmp al, 'M'
    je dbg_c_write
    cmp al, 'i'
    je dbg_c_in
    cmp al, 'o'
    je dbg_c_out
    cmp al, 's'
    je dbg_c_speed
    ; fall through to the refusal

; -----------------------------------------------------------------------------
; dbg_bad - '?' and nothing else.
;
; One reply per command, always, even a refused one: a host tool that reads
; until it sees a terminator must never be left waiting on a typo.
; -----------------------------------------------------------------------------
dbg_bad:
    mov al, '?'
    call dbg_putc
    jmp dbg_crlf

; --- v: who is this ----------------------------------------------------------
dbg_c_ver:
    mov si, dbg_banner
    call dbg_puts
    jmp dbg_crlf

; --- m SSSS OOOO LL: read memory ---------------------------------------------
; Answers the address back before the bytes, so a captured transcript says
; what it is a picture of - FIELD-MACHINES.md's third dump rule ("find a value
; that pins the reading") applied to the cheapest possible case.
;
; A length of 0 means 256, which is what makes `m` a one-byte-field command
; and still able to fill a line of any width a host wants.
dbg_c_read:
    call dbg_hexw               ; segment
    jc dbg_bad
    mov bp, ax
    call dbg_hexw               ; offset
    jc dbg_bad
    mov di, ax
    call dbg_hexw               ; length
    jc dbg_bad
    mov cx, ax
    and cx, 0x00FF
    jnz .go
    mov cx, 256
.go:
    mov ax, bp                  ; echo the address
    call dbg_puthexw
    mov al, ':'
    call dbg_putc
    mov ax, di
    call dbg_puthexw
    mov al, ' '
    call dbg_putc
.byte:
    mov es, bp
    mov al, [es:di]
    call dbg_puthexb
    inc di
    loop .byte
    jmp dbg_crlf

; --- M SSSS OOOO hh hh ...: write memory -------------------------------------
; Every byte is written as it is parsed rather than staged, so a malformed
; tail leaves the good prefix written - which is the behaviour you want from
; a poke command whose whole purpose is to change one thing and see what
; happens. The count comes back so the caller can tell how far it got.
dbg_c_write:
    call dbg_hexw
    jc dbg_bad
    mov bp, ax
    call dbg_hexw
    jc dbg_bad
    mov di, ax
    xor bx, bx                  ; BX = bytes written
.byte:
    call dbg_hexb
    jc .done
    mov es, bp
    mov [es:di], al
    inc di
    inc bx
    jmp short .byte
.done:
    mov ax, bx
    call dbg_puthexw
    jmp dbg_crlf

; --- i PPPP: read an I/O port ------------------------------------------------
dbg_c_in:
    call dbg_hexw
    jc dbg_bad
    mov dx, ax
    in  al, dx
    call dbg_puthexb
    jmp dbg_crlf

; --- o PPPP hh: write an I/O port --------------------------------------------
dbg_c_out:
    call dbg_hexw
    jc dbg_bad
    mov bp, ax
    call dbg_hexb
    jc dbg_bad
    mov dx, bp
    out dx, al
    mov al, 'o'
    call dbg_putc
    mov al, 'k'
    call dbg_putc
    jmp dbg_crlf

; --- s DD: change the divisor (SPEC.md 58.4) ---------------------------------
; The reply goes out BEFORE the line changes, at the rate the host is still
; listening at, and the last byte is drained off the shift register first -
; reprogramming the divisor with a byte still clocking out truncates it, and
; the host then resynchronises on a partial line at a rate it does not yet
; know about. That is the one ordering in this file that cannot be got wrong
; twice, because the channel is how you would have debugged it.
dbg_c_speed:
    call dbg_hexw
    jc dbg_bad
    or  al, al
    jz dbg_bad
    cmp al, DBG_MAXDIV
    ja dbg_bad
    mov [dbg_div], al
    mov al, 'o'
    call dbg_putc
    mov al, 'k'
    call dbg_putc
    call dbg_crlf
    call dbg_txwait             ; wait out the shift register, not just THRE
    jmp dbg_uart_init

; =============================================================================
; Output: polled, bounded, and never interrupt-driven
; =============================================================================

; -----------------------------------------------------------------------------
; dbg_putc - one byte out, waiting for THRE
; in:  AL = the byte
; out: nothing (AL preserved)
; clobbers: nothing
;
; The wait is BOUNDED. An unbounded one is the single way this driver could
; hang a machine it exists to diagnose: a cable pulled, a host that stopped
; reading and let the emulated UART back up, a card that answers 0FFh to
; everything. Dropping the byte loses a line of output; spinning loses the
; machine.
; -----------------------------------------------------------------------------
dbg_putc:
    push ax
    push cx
    push dx
    mov ah, al
    mov cx, DBG_TXWAIT
    mov dx, DBG_BASE + U_LSR
.wait:
    in  al, dx
    test al, LSR_THRE
    jnz .send
    loop .wait
    jmp short .out              ; timed out: drop it
.send:
    mov dx, DBG_BASE + U_THR
    mov al, ah
    out dx, al
.out:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dbg_txwait - wait until the transmitter is COMPLETELY idle
; clobbers: nothing
;
; LSR bit 6, not bit 5: bit 5 says the holding register is free and bit 6 says
; the shift register is too. Only the second means the last byte has actually
; left the machine, which is what dbg_c_speed needs before it moves the line.
; -----------------------------------------------------------------------------
dbg_txwait:
    push ax
    push cx
    push dx
    mov cx, DBG_TXWAIT
    mov dx, DBG_BASE + U_LSR
.wait:
    in  al, dx
    test al, 0x40
    jnz .out
    loop .wait
.out:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dbg_puts - a NUL-terminated string in OUR segment
; in:  SI = the string
; clobbers: nothing (SI preserved)
; -----------------------------------------------------------------------------
dbg_puts:
    push ax
    push si
.next:
    lodsb
    or  al, al
    jz .out
    call dbg_putc
    jmp short .next
.out:
    pop si
    pop ax
    ret

dbg_crlf:
    push ax
    mov al, 13
    call dbg_putc
    mov al, 10
    call dbg_putc
    pop ax
    ret

; -----------------------------------------------------------------------------
; dbg_puthexb / dbg_puthexw - AL / AX as hex, followed by one space
; clobbers: nothing
;
; The trailing space is part of the byte, not a separator between bytes: it
; makes the last field of a line look like every other one, so a host parser
; is a split() and never a special case at the end.
; -----------------------------------------------------------------------------
dbg_puthexb:
    push ax
    push cx
    mov cl, al
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    call dbg_nib
    mov al, cl
    call dbg_nib
    mov al, ' '
    call dbg_putc
    pop cx
    pop ax
    ret

dbg_puthexw:
    push ax
    push cx
    mov cx, ax
    mov al, ah
    call dbg_hexnyb2
    mov ax, cx
    call dbg_hexnyb2
    pop cx
    pop ax
    ret

dbg_hexnyb2:                    ; AL as two nibbles, no trailing space
    push ax
    push cx
    mov cl, al
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    call dbg_nib
    mov al, cl
    call dbg_nib
    pop cx
    pop ax
    ret

dbg_nib:                        ; the low nibble of AL as one character
    push ax
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .emit
    add al, 'a' - '0' - 10
.emit:
    call dbg_putc
    pop ax
    ret

; =============================================================================
; Input parsing: fixed shape, no cleverness
; =============================================================================

; -----------------------------------------------------------------------------
; dbg_hexw / dbg_hexb - the next whitespace-separated hex field at DS:SI
; in:  SI = the parse point
; out: CF = 0 and AX (or AL) = the value, SI past it
;      CF = 1 = there was no field there, SI unmoved past the spaces
; clobbers: AX, SI
;
; A field is one to four hex digits. Anything else - a letter, a second colon,
; the end of the line - ends it, and an EMPTY field is CF = 1 rather than
; zero: `m 60 0` and `m 60 0 0` mean different things and a parser that reads
; a missing length as 0 would silently answer the second when asked the
; first.
; -----------------------------------------------------------------------------
dbg_hexw:
    push bx
    push cx
    xor bx, bx
    xor cx, cx                  ; CL = digits consumed
.skip:
    mov al, [si]
    cmp al, ' '
    jne .digits
    inc si
    jmp short .skip
.digits:
    mov al, [si]
    call dbg_nibval
    jc .end
    cmp cl, 4
    jae .end                    ; a fifth digit is a typo, not an overflow to
                                ; wrap silently
    inc si
    inc cl
    mov ah, 4
.sh:
    shl bx, 1                   ; 8086: no shl by an immediate other than 1
    dec ah
    jnz .sh
    xor ah, ah
    or  bl, al
    jmp short .digits
.end:
    or  cl, cl
    jz .none
    mov ax, bx
    pop cx
    pop bx
    clc
    ret
.none:
    pop cx
    pop bx
    stc
    ret

dbg_hexb:
    call dbg_hexw
    ret                         ; the callers that want a byte use AL; a field
                                ; wider than two digits is the caller's
                                ; problem and not worth a second parser

; -----------------------------------------------------------------------------
; dbg_nibval - one hex digit to its value
; in:  AL = a character
; out: CF = 0 and AL = 0..15, or CF = 1 = not a hex digit
; -----------------------------------------------------------------------------
dbg_nibval:
    cmp al, '0'
    jb .no
    cmp al, '9'
    jbe .dec
    or  al, 0x20                ; fold case: 'A'..'F' and 'a'..'f' alike
    cmp al, 'a'
    jb .no
    cmp al, 'f'
    ja .no
    sub al, 'a' - 10
    clc
    ret
.dec:
    sub al, '0'
    clc
    ret
.no:
    stc
    ret

; =============================================================================
; The Control Panel page (SPEC.md 31.9)
;
; Read-only, and that is the whole design: everything this driver does is
; done from the other end of the cable, so the page's only job is to answer
; "is it up, and on which port" - which is exactly the question you have when
; the host tool sees nothing. It is also the only place the machine itself
; ever says the debug channel is open, which matters more than it looks: a
; monitor nobody can see is a monitor nobody remembers to turn off.
; =============================================================================

DBGP_LX     equ 8               ; the pane is 221 x 121 px - 27 characters by
DBGP_Y0     equ 6               ; 13 rows (drivers/os88drv.inc)
DBGP_ROWH   equ 10

; -----------------------------------------------------------------------------
; dbg_page_paint - DSV_CPPAINT
; in:  DI = pane left, DX = pane top; the pane is white-filled and the gfx
;      lock is HELD
; -----------------------------------------------------------------------------
dbg_page_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bp, dx
    mov [dbg_pane_x], di

    mov al, CBLACK
    call OSAPI_SET_COLOR

    mov si, dbg_p_title
    xor bx, bx
    call dbg_page_row
    mov si, dbg_p_port
    mov bx, 2
    call dbg_page_row
    mov si, dbg_p_baud
    mov bx, 3
    call dbg_page_row

    mov ax, [dbg_nline]         ; the counter is restamped on every paint
    mov di, dbg_p_nbuf          ; rather than kept current, because it moves
    call dbg_page_hexw          ; without the panel ever being told
    mov si, dbg_p_lines
    mov bx, 5
    call dbg_page_row

    pop bp
    pop di
    pop dx
    pop cx
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dbg_page_row - SI = string, BX = row, BP = pane top, [dbg_pane_x] = pane left
; -----------------------------------------------------------------------------
dbg_page_row:
    push ax
    push dx
    mov ax, DBGP_ROWH
    mul bx                      ; 8086: no imm shift, and a mul is honest here
    mov dx, bp
    add dx, DBGP_Y0
    add dx, ax
    mov cx, [dbg_pane_x]
    add cx, DBGP_LX
    call OSAPI_FONT_STR
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dbg_page_hexw - AX as four hex characters at DS:DI (no terminator written)
; clobbers: nothing
; -----------------------------------------------------------------------------
dbg_page_hexw:
    push ax
    push bx
    push cx
    push di
    mov bx, ax
    mov cx, 4
.nyb:
    mov al, bh                  ; the top nibble of the word, every pass
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .emit
    add al, 'a' - '0' - 10
.emit:
    mov [di], al
    inc di
    shl bx, 1                   ; ...and shift the next one up into it
    shl bx, 1
    shl bx, 1
    shl bx, 1
    loop .nyb
    pop di
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dbg_page_click - DSV_CPCLICK. Nothing is clickable: the page is a readout.
; -----------------------------------------------------------------------------
dbg_page_click:
    ret

; =============================================================================
; Data (a driver's zeroed data ships in the image)
; =============================================================================

dbg_services:
    dw 0                        ; DSV_CAPS    - this is not a sound driver
    dw 0                        ; DSV_FM
    dw 0                        ; DSV_STREAM
    dw 0                        ; DSV_TICK
    dw 0                        ; DSV_RELINST
    dw 0                        ; DSV_NAME
    dw 0                        ; DSV_TONE
    dw 0                        ; DSV_TIERS   - one tier, so no DRVV_TIER
    dw 0                        ; DSV_BLK     - this is not a disk driver
    dw dbg_p_title              ; DSV_CPNAME  - and so the page exists
    dw dbg_page_paint           ; DSV_CPPAINT
    dw dbg_page_click           ; DSV_CPCLICK
    times DSV_SIZE - ($ - dbg_services) db 0
                                ; drv_publish copies DSV_SIZE bytes whether or
                                ; not the driver filled them - unpadded, the
                                ; two bytes after this table were dbg_up (=1
                                ; by publish time) and dbg_busy, and the
                                ; kernel read that 0x0001 as a DSV_CPCLOSE
                                ; entry: every Control Panel close far-called
                                ; offset 1 of this image. sound.asm's pad,
                                ; for sound.asm's reason.

dbg_up      db 0                ; attached, and therefore hooked
dbg_busy    db 0                ; a command is running with interrupts on
dbg_rdy     db 0                ; a complete line is waiting in dbg_buf
dbg_len     db 0                ; bytes collected into it so far
dbg_div     db DBG_DIV          ; the live divisor (SPEC.md 58.4)
dbg_pane_x  dw 0
dbg_nline   dw 0                ; lines received this session, for the page
dbg_oldoff  dw 0                ; the vector we displaced
dbg_oldseg  dw 0

dbg_banner  db 'os8088 debug 1', 0

dbg_p_title db 'Debug', 0
dbg_p_port  db 'Port  2E8  irq 3', 0
dbg_p_baud  db 'Line  8N1', 0
dbg_p_lines db 'Lines '
dbg_p_nbuf  db '0000', 0

dbg_buf     times DBG_LINE db 0

OS88_DRV_END
