; =============================================================================
; os8088 - mouse.inc
;
; Serial Microsoft mouse on COM1 OR COM2 (SPEC.md 9.5) plus the arrow cursor
; with save-under. The ISR decodes the 3-byte Microsoft packet, keeps
; mouse_x/mouse_y/mouse_btn fresh, queues EVT_MDOWN/EVT_MUP on left-button
; changes and EVT_RDOWN on the right button's PRESS edge, and moves the
; cursor itself only when the gfx lock is free and the cursor is
; refcount-visible (SPEC 7); otherwise it just sets cur_dirty.
;
; Which port the mouse is on is not asked and not configured: both are probed
; for a UART, every one that answers is programmed, hooked and listened to at
; once, and the first port to deliver a complete packet WINS - the rest are
; retired on the UI task's next pass. On a machine with one serial card that
; degenerates to exactly the single-port code this replaced.
;
; cursor_show/cursor_hide are refcounted via cur_level (-1 = hidden at boot,
; classic Show/HideCursor style) and guard their draw/restore sections with
; pushf/cli ... popf so the ISR never sees a half-drawn cursor. Both are
; called with the gfx lock held (from gfx_lock/gfx_unlock) or at boot.
;
; The hotspot is the drawn position (0,0 of the cell); at the right and
; bottom screen edges the cell is clipped, never shifted;
; mouse_x/mouse_y themselves range over the full 0..639 / 0..479.
; =============================================================================

; UART register offsets from a port's base: +1 IER, +2 IIR, +3 LCR, +4 MCR,
; +5 LSR (and with LCR's DLAB set, +0 DLL and +1 DLM).
MOU_NPORT   equ 2               ; COM1 and COM2 (SPEC.md 9.5)
MOU_PEND    equ MOU_NPORT*2     ; ...as the end of a word-indexed table walk

MOU_LOCKN   equ 8               ; CLEAN packets in a row from ONE port before
                                ; it is declared the mouse's. Not 1: a modem's
                                ; 'O','K',CR,LF is a bit-6 header and two
                                ; bit-6-clear bytes, which IS a well-formed
                                ; Microsoft packet, and one of those would
                                ; have handed the mouse's slot to the modem.
                                ; Clean, because what really separates a mouse
                                ; from ASCII is that every byte is consumed -
                                ; see mou_claim and SPEC.md 9.5. Eight costs
                                ; a fifth of a second of the first movement
                                ; and nothing else: the packets before it are
                                ; decoded and acted on like any other

MOU_RSTLOW  equ 3               ; ticks DTR/RTS are held LOW in a reset
                                ; (~165ms; a serial mouse wants >=100ms
                                ; unpowered to guarantee its power-on reset)
MOU_REPOLL  equ 55              ; ticks between hot-plug reset attempts
                                ; (~3s) while no packet has ever arrived
MOU_DRAINT  equ 9               ; ticks the ISR discards RX after a hot-plug
                                ; raise (~0.5s): the identify/PnP burst is
                                ; bit-6-set bytes the resync rule would read
                                ; as packet headers - phantom clicks

; --- the identify burst (SPEC.md 9.4.1) --------------------------------------
; mouse_init's reset edge makes a serial mouse announce itself, and the window
; that drains that burst is the ONE moment this kernel owns a port outright:
; IER = 0, both IRQs still masked, and the edge provoking it was ours. So the
; burst is READ as well as discarded, and a port that answers like a mouse is
; not power-cycled for the rest of the session.
MOU_IDWIN   equ 19              ; ticks the burst is drained for (~1s at
                                ; 18.2Hz - ample for 'M3' at 1200 7N1, where
                                ; a byte is 10 bits and so ~8.3ms)
MOU_IDBYTE  equ 'M'             ; the Microsoft mouse ID byte (0x4D), which
                                ; every MS-compatible serial mouse answers a
                                ; DTR/RTS raise with. NO standard Hayes result
                                ; code contains it - not OK, RING, NO CARRIER,
                                ; CONNECT, ERROR, BUSY, NO DIALTONE or NO
                                ; ANSWER - and every one of those IS a
                                ; well-formed Microsoft packet (SPEC.md 9.5.1)
MOU_IDMAX   equ 8               ; ...and at most this many bytes in all. A
                                ; mouse says 'M', maybe '3' or 'Z', and stops;
                                ; a modem banner is a STREAM. A verbose PnP ID
                                ; overruns this and gets exactly the behaviour
                                ; it had before - no stand-down, no threshold
                                ; drop - which is the accepted degradation
MOU_IDSTRICT equ 3              ; ...and THIS many to lower [mou_need], which
                                ; is a tighter bar on purpose (SPEC.md 9.4.1).
                                ; The two consequences are not symmetrical: a
                                ; wrong stand-down costs a later hot-plug its
                                ; reset edges, a wrong threshold lets one
                                ; 'OK' CR LF claim the port and mask the real
                                ; mouse off for the session - 9.5.1's original
                                ; bug. 3 is 'M', 'M3' and 'MZ@' and nothing
                                ; with room for a banner in it
MOU_IDQUIET equ 4               ; ...and nothing for the last ~220ms of the
                                ; window. This is the test that separates a
                                ; burst from a stream, and MOU_IDMAX alone
                                ; cannot: a banner that happens to be short is
                                ; still arriving when the window closes

; --- the PS/2 mouse (SPEC.md 9.9) --------------------------------------------
; The 8042 keyboard controller's AUXILIARY device port, on IRQ12, which is
; where every machine above an XT puts its mouse. Probed AFTER both serial
; ports - that is SPEC.md 9.9's order, and it is an order rather than a
; preference because the serial half has no probe at all and its identify
; window is a whole second of mouse_init.
MOU_P2DAT   equ 0x60            ; the 8042's data port...
MOU_P2CMD   equ 0x64            ; ...and its status (read) / command (write)
MOU_P2ROW   equ 4               ; [mou_port] once the PS/2 mouse has won: ONE
                                ; PAST the last serial row, so every `cmp bl,
                                ; [mou_port]` in the serial decoder fails and
                                ; mou_lockon retires both UARTs (SPEC.md 9.9.4)
MOU_P2LINE  equ 0xFF            ; ...and [mou_line]'s value then. NOT a master
                                ; 8259 bit, so 9.5.2.1's "never mask the line
                                ; the winning packets came in on" guard masks
                                ; both serial lines and neither of the two
                                ; wires this mouse is actually on
MOU_P2SL    equ 0x10            ; IRQ12's bit in the SLAVE 8259's OCW1 (0xA1)
MOU_P2CASC  equ 0x04            ; ...and IRQ2's in the master's: the CASCADE,
                                ; without which the slave's line reaches
                                ; nothing, and the symptom is a mouse that
                                ; passes every step of the probe and is silent
MOU_P2VEC   equ 0x74*4          ; int 74h's IVT offset

MOU_P2TMO   equ 2               ; ticks one handshake step may take (~110ms).
                                ; TICKS AND NOT A SPIN COUNT: a count sized on
                                ; a 286 is some fifty times too short on a 486
                                ; and one sized on a 486 is minutes on a 286,
                                ; while [ticks] is the same quantity on both
MOU_P2BAT   equ 5               ; ...and the reset's self-test, which the part
                                ; is allowed half a second for, is that short
                                ; wait RETRIED this many times rather than a
                                ; second timeout to size

CUR_W       equ 16              ; cursor cell size, px - the size of the two
CUR_H       equ 16              ; bitmap TABLES, and nothing else

; The arrow inside that cell (SPEC.md 7.1). The tables are 16 rows of 16 bits
; because that is the classic shape; the ARROW is 8 pixels wide and 12 rows
; tall, and for years every save, every restore and both draw passes walked
; the whole 16x16 - a quarter of the rows and a third of the bytes per row of
; work on empty bits, on every gfx_lock and every gfx_unlock in the machine.
;
; Both are ASSERTED against the tables at the bottom of this file rather than
; trusted, so a redrawn arrow that outgrows either one fails the build instead
; of quietly losing its right-hand column. The width is what retires the cell's
; THIRD byte: a 16-bit row whose low byte is zero cannot reach one however far
; it is shifted, so `row << (8-shift)` was a byte that was always 0 and a
; branch that was never taken, once per row per pass.
CUR_MVGRACE equ 6               ; ticks since the last movement packet within
                                ; which the pointer counts as MOVING (SPEC.md
                                ; 7.1.4.3). ~330ms, and it is MEASURED rather
                                ; than reasoned: a report is 3 bytes at 1200
                                ; baud and the 8250 has no FIFO, so a hold
                                ; that delays IRQ4 loses one and the decoder
                                ; waits for the next - gaps of 3 and 4 ticks
                                ; are ordinary DURING a steady sweep. At 2 the
                                ; gate called those gaps "stopped" and the
                                ; stutter came back in 16 of 56 lock holds.
                                ; The cost of the larger value is that the
                                ; arrow keeps blinking for a third of a second
                                ; after the hand stops, which is a third of a
                                ; second of the behaviour that shipped for
                                ; years before 7.1.4
CUR_GW      equ 8               ; the arrow's own width, px...
CUR_GH      equ 12              ; ...and height, rows
CUR_SPAN    equ 2               ; ...and the framebuffer bytes it spans once
                                ; shifted by x & 7, which CUR_GW <= 8 bounds
CUR_TABSZ   equ CUR_H * 2       ; cur_data is exactly one table past cur_and,
                                ; so one SI walks both (asserted below)
CUR_SAVE_SZ equ CUR_SPAN * CUR_GH * 4       ; ...x four planes, for VGA

; --- the shapes (SPEC.md 7.2) ------------------------------------------------
; A window names one of these and the pointer wears it over that window and
; nowhere else. They all share the cell above, which is the whole reason a
; shape costs 64 bytes of .text and not one instruction anywhere in the draw:
; every walk here is bounded by CUR_GW/CUR_GH/CUR_SPAN, so a shape needing a
; bigger cell would hand back SPEC.md 7.1's savings on every lock hold in the
; machine rather than only over the window that asked for it.
CUR_ARROWSH equ 0               ; the Mac arrow, hot spot (0,0) - the default,
                                ; and 0 for the reason W_FLAGS' spare byte
                                ; arrives 0 (SPEC.md 7.2.1)
CUR_CROSSSH equ 1               ; the aiming crosshair, hot spot at its centre
CUR_NSHAPE  equ 2
CUR_XHX     equ 3               ; the crosshair's hot spot inside the cell...
CUR_XHY     equ 5               ; ...which is the only reason 7.2.2 exists

; -----------------------------------------------------------------------------
; The port table (SPEC.md 9.5)
;
; One row per candidate serial port, walked with BX as a WORD index (0, 2, ...
; MOU_PEND) so [bx+table] is a plain DS-relative fetch. [bx+mou_bases] is the
; only cell that changes at run time: mouse_init's probe ZEROES it where no
; UART answers, and every routine below reads that as "this port does not
; exist" - which is what keeps a one-serial-card machine on exactly the code
; path it had before COM2 was a possibility.
;
; The base decides the IRQ, because that is what the BIOS, DOS and every mouse
; driver ever written assume: 3F8 is IRQ4 and 2F8 is IRQ3. A card whose jumper
; disagrees with its base address is out of scope here, as it was before.
; -----------------------------------------------------------------------------
MOU_L4      equ 0x10            ; IRQ4's bit in the master 8259's OCW1 (0x21)
MOU_L3      equ 0x08            ; ...and IRQ3's. Named because the ISR entries
                                ; carry them too (SPEC.md 9.5.2.1) and a table
                                ; and a stub that disagree about which bit is
                                ; which line would mask the mouse's own

mou_bases   dw 0x3F8, 0x2F8     ; UART base per port; 0 = nothing answered
mou_ivecs   dw 0x30, 0x2C       ; ...its IVT offset: int 0x0C, int 0x0B
mou_masks   dw MOU_L4, MOU_L3   ; ...the line its base says it drives, which is
                                ; what to HOOK and never what to retire
mou_isrs    dw mou_isr, mou_isr3    ; ...and the stub that vector points at

; -----------------------------------------------------------------------------
; mou_pout - AL out to [port BX's base + AH]
;
; A port the probe rejected is a NO-OP, and that is the point: every caller
; below writes UART registers without a presence test of its own, and a
; machine with one serial card runs the identical sequence it always did with
; one dead call per step.
; ins:  BX = port row, AH = register offset, AL = value
; outs: none (all registers preserved)
; -----------------------------------------------------------------------------
mou_pout:
    push cx
    push dx
    mov dx, [bx+mou_bases]
    or  dx, dx
    jz .out
    mov cl, ah
    xor ch, ch
    add dx, cx
    out dx, al
.out:
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; mou_pall - mou_pout on every port. Both ports are reset, powered and drained
;            TOGETHER, because until a packet arrives there is no way to know
;            which one the mouse is on and no reason to prefer either.
; ins:  AH = register offset, AL = value
; outs: none (all registers preserved)
; -----------------------------------------------------------------------------
mou_pall:
    push bx
    xor bx, bx
.port:
    call mou_pout
    add bx, 2
    cmp bx, MOU_PEND
    jb .port
    pop bx
    ret

; -----------------------------------------------------------------------------
; mou_uart - is there an 8250 at port BX's base? (SPEC.md 9.5)
;
; The DIVISOR LATCH is the test. With LCR's DLAB set, base+0 is an ordinary
; read/write register, so writing two distinct values and reading each back
; proves a latch is there to hold them; an unpopulated ISA address answers
; 0FFh to both. Every 8250 variant has it - unlike the scratch register at
; +7, which the original 8250 on a period serial card does not.
;
; Two values and not one, because a bus that happens to float to the first
; would otherwise pass. The probe leaves DLAB set and the divisor scrambled,
; which costs nothing: the caller programs both immediately afterwards.
;
; This is the ONLY reason an IRQ is ever unmasked here. Unmasking IRQ3 on a
; machine with no COM2 card would arm a line nothing drives.
; ins:  BX = port row
; outs: CF = 1 if a UART answered (all registers preserved)
; -----------------------------------------------------------------------------
mou_uart:
    push ax
    push dx
    mov dx, [bx+mou_bases]
    add dx, 3                   ; LCR: DLAB = 1, so base+0 is the latch
    mov al, 0x80
    out dx, al
    sub dx, 3
    mov al, 0xAA
    out dx, al
    jmp short $+2               ; I/O settling (kernel/clock.inc's idiom)
    in  al, dx
    cmp al, 0xAA
    jne .no
    mov al, 0x55
    out dx, al
    jmp short $+2
    in  al, dx
    cmp al, 0x55
    jne .no
    stc
    jmp short .out
.no:
    clc
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mouse_init - probe both ports, hook the vector of each one that answers,
;              program its UART (1200 baud 7N1, RX interrupt, DTR|RTS|OUT2 so
;              the mouse powers up), flush stale RX/LSR/IIR, unmask its IRQ.
; ins:  none
; outs: none (all registers preserved)
; -----------------------------------------------------------------------------
mouse_init:
    push ax
    push bx
    push cx
    push dx
    push si
    push es

    ; --- which ports are actually there? (SPEC.md 9.5) -----------------------
    xor bx, bx
.probe:
    call mou_uart
    jc .next
    mov word [bx+mou_bases], 0  ; nothing answered: retire the row, and every
.next:                          ; routine below skips it from here on
    add bx, 2
    cmp bx, MOU_PEND
    jb .probe

    ; --- one port is not a contest -------------------------------------------
    ; With a single serial card there is nothing that could steal the mouse's
    ; slot and nothing to weigh against, so the first packet settles it and is
    ; acted on - which is this kernel's behaviour byte for byte before COM2
    ; was a possibility. Only a machine with two live ports pays MOU_LOCKN.
    xor cx, cx
    xor bx, bx
.count:
    cmp word [bx+mou_bases], 0
    je .cnext
    inc cx
.cnext:
    add bx, 2
    cmp bx, MOU_PEND
    jb .count
    mov al, 1
    cmp cx, 2
    jb .one
    mov al, MOU_LOCKN
.one:
    mov [mou_need], al          ; per port (SPEC.md 9.4.1): the identify burst
    mov [mou_need+2], al        ; below may lower ONE of them again

    ; --- save each present port's old vector, install ours -------------------
    xor ax, ax
    mov es, ax
    xor bx, bx
.hook:
    cmp word [bx+mou_bases], 0
    je .nohook
    mov si, [bx+mou_ivecs]
    pushf
    cli
    mov ax, [es:si]
    mov [bx+mou_old_off], ax
    mov ax, [es:si+2]
    mov [bx+mou_old_seg], ax
    mov ax, [bx+mou_isrs]
    mov [es:si], ax
    mov [es:si+2], cs
    popf
.nohook:
    add bx, 2
    cmp bx, MOU_PEND
    jb .hook

    ; --- int 09h, for the one button key some BIOSes discard (SPEC.md 9.6.4) -
    ; EVERY TIER, which is a change from the first version and is what the
    ; guard in kbm_btn buys: the hook is a FALLBACK for a BIOS that throws
    ; keypad 5 away, and on one that delivers it int 16h wins and this claims
    ; nothing. The peek reads port 60h without acknowledging it, which is the
    ; same access DOS's own keyboard code makes.
    xor ax, ax
    mov es, ax
    pushf
    cli
    mov ax, [es:0x24]           ; int 09h's vector, saved for the chain
    mov [kbm_old9], ax
    mov ax, [es:0x26]
    mov [kbm_old9+2], ax
    mov word [es:0x24], kbm_isr
    mov [es:0x26], cs
    popf

    ; --- UART: 1200 baud, 7 data bits, no parity, 1 stop ---------------------
    mov ax, 0x0382              ; LCR: DLAB=1 + 7N1 bits
    call mou_pall
    mov ax, 0x0060              ; divisor low = 96 (115200/96 = 1200)
    call mou_pall
    mov ax, 0x0100              ; divisor high = 0
    call mou_pall
    mov ax, 0x0302              ; LCR: DLAB=0, 7N1
    call mou_pall

    ; --- reset the mouse: DTR/RTS LOW, hold, then raise ----------------------
    ; A serial mouse is POWERED by DTR/RTS and resets itself on the RISING
    ; edge. Writing the final MCR value straight away only makes that edge on
    ; a cold boot, where the lines start low; on a Ctrl-Alt-Del warm boot
    ; they are still high from the previous session, the rewrite is no edge
    ; at all, and a mouse that was wedged (hot-plugged onto live lines, say)
    ; STAYS wedged until the user finds the power switch - which is exactly
    ; how this was found on a real 5150. Hold low for ~150ms (the parts want
    ; >=100ms unpowered to guarantee a reset), then raise: every boot now
    ; hands the mouse the same clean power-on a cold start does.
    mov ax, 0x0408              ; MCR: OUT2 only - DTR and RTS low. OUT2
    call mou_pall               ; stays up throughout so the IRQ gate never
                                ; glitches the bus
    mov bx, [ticks]
.rstlow:
    mov ax, [ticks]
    sub ax, bx
    cmp ax, MOU_RSTLOW
    jb .rstlow
    mov ax, 0x040B              ; MCR: DTR|RTS|OUT2 - powers the mouse and
    call mou_pall               ; gates the IRQ onto the bus; this rising
                                ; edge is the mouse's reset

    ; --- drain the identify burst (IER still 0, the IRQs still masked) -------
    ; The DTR/RTS edge makes a real Microsoft mouse announce itself: 'M',
    ; maybe '3', maybe a whole PnP string - bytes with bit 6 set that the
    ; ISR's resync rule would misread as packet headers (phantom clicks,
    ; cursor jumps). So they are discarded here, before RX interrupts are
    ; enabled; sched_init already runs, so [ticks] is live.
    ;
    ; They are also RECORDED (SPEC.md 9.4.1). This window is the one moment
    ; the kernel owns a port outright - IER 0, both IRQs masked, and the edge
    ; that provoked the burst was ours at a moment we chose - so what arrives
    ; in it is far better evidence than the packet contest can ever get, and
    ; throwing it away is what left a plugged-in mouse being power-cycled
    ; every MOU_REPOLL ticks for the whole session.
    mov cx, [ticks]
.settle:
    xor bx, bx
.sport:
    mov dx, [bx+mou_bases]
    or  dx, dx
    jz .snext
    add dx, 5                   ; LSR: RX data ready?
    in  al, dx
    test al, 0x01
    jz .snext
    sub dx, 5
    in  al, dx                  ; the identify byte: banked, then discarded
    call mou_idbyte
.snext:
    add bx, 2
    cmp bx, MOU_PEND
    jb .sport
    mov ax, [ticks]
    sub ax, cx
    cmp ax, MOU_IDWIN
    jb .settle

    call mou_idjudge            ; ...and who sounded like a mouse?

    mov ax, 0x0101              ; IER: RX data available only
    call mou_pall
    call mou_newround           ; ...before anything is unmasked, or the store
                                ; could land on a packet already in progress

    ; --- flush whatever is stale, then unmask (in that order, per port) ------
    xor bx, bx
.flush:
    mov dx, [bx+mou_bases]
    or  dx, dx
    jz .fnext
    in  al, dx                  ; RX
    add dx, 5
    in  al, dx                  ; LSR
    sub dx, 3
    in  al, dx                  ; IIR
    mov cl, [bx+mou_masks]      ; ...and unmask this port's IRQ, which the
    not cl                      ; probe is what licenses: nothing is armed
    in  al, 0x21                ; on a line no card drives
    and al, cl
    out 0x21, al
.fnext:
    add bx, 2
    cmp bx, MOU_PEND
    jb .flush

    ; --- ...and ONLY NOW the other socket (SPEC.md 9.9) ----------------------
    ; Serial first, then PS/2. It is an order rather than a preference: the
    ; serial half has no probe at all - only the identify burst and a contest -
    ; and the burst alone is MOU_IDWIN of this routine, so it runs to
    ; completion and this runs after it. Where both mice exist both end up
    ; live, and SPEC.md 9.5's contest settles it exactly as it settles two
    ; UARTs: the first complete packet wins.
%ifdef KERN_BIG
    call mou_p2_init
%endif

    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mou_idbyte - bank one byte of port BX's identify burst (SPEC.md 9.4.1)
;
; Three facts per port, and they are the three the judgement below needs: the
; FIRST byte after our rising edge, how many arrived in all, and when the last
; one did. Nothing here decides anything - mouse_init is still mid-window and
; a burst that has only got as far as 'M' is indistinguishable from a stream
; that begins with one.
;
; ins:  BX = port row, AL = the byte just read
; outs: none (all registers preserved)
; -----------------------------------------------------------------------------
mou_idbyte:
    push ax
    push dx
    mov dl, [bx+mou_idn]
    or  dl, dl
    jne .notfirst
    mov [bx+mou_idb0], al       ; the first byte is the whole of rule 2
.notfirst:
    cmp dl, 0xFF                ; saturate: a stream must not wrap the count
    je  .stamp                  ; back down through MOU_IDMAX and pass rule 3
    inc byte [bx+mou_idn]
.stamp:
    mov ax, [ticks]
    mov [bx+mou_idlast], ax
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mou_idjudge - which ports answered like a mouse? (SPEC.md 9.4.1)
;
; Four rules, all four required: something arrived, the first byte was 'M',
; no more than MOU_IDMAX arrived in all, and the port was quiet for the last
; MOU_IDQUIET ticks of the window. The last two are the ones that separate a
; mouse's burst from a modem's banner, and MOU_IDMAX alone cannot - a banner
; that happens to be short is still ARRIVING when the window closes.
;
; Then two consequences, deliberately unequal (SPEC.md 9.4.1):
;
;   [mou_idany]  the recovery cycle stands down. The safe half: a false
;                positive costs only the reset edges a LATER hot-plug would
;                have got, and no identify can claim a port, move the cursor
;                or queue a click.
;   [mou_need]   drops to 1, and only where EXACTLY ONE port identified -
;                which is SPEC.md 9.5's "one port is not a contest" with the
;                burst answering which port that is. Both identifying is
;                ambiguous and leaves both thresholds alone.
;
; Called once, from mouse_init, with the window just closed.
; ins:  none
; outs: none (all registers preserved)
; -----------------------------------------------------------------------------
mou_idjudge:
    push ax
    push bx
    push cx
    push dx
    mov dx, [ticks]             ; the window's closing tick, for rule 4
    xor cx, cx                  ; CL = how many identified, CH = the last one's
    xor bx, bx                  ; row (meaningful only where CL ends up 1)
.port:
    cmp word [bx+mou_bases], 0
    je  .next                   ; a port the probe rejected cannot answer
    mov al, [bx+mou_idn]
    or  al, al
    jz  .next                   ; rule 1: nothing arrived
    cmp al, MOU_IDMAX
    ja  .next                   ; rule 3: too many - this is a stream
    cmp byte [bx+mou_idb0], MOU_IDBYTE
    jne .next                   ; rule 2: it did not start with 'M'
    mov ax, dx
    sub ax, [bx+mou_idlast]     ; wrap-safe, like every other elapsed here
    cmp ax, MOU_IDQUIET
    jb  .next                   ; rule 4: still talking as the window closed
    mov byte [bx+mou_ident], 1
    mov byte [mou_idany], 1
    cmp byte [bx+mou_idn], MOU_IDSTRICT  ; ...and the tighter bar the THRESHOLD
    ja  .next                   ; wants (it is still counted as an identify
                                ; above: the stand-down is the cheap
                                ; consequence). Re-read rather than kept in AL,
                                ; which rule 4's `mov ax, dx` above spends -
                                ; held there it compared the low byte of the
                                ; ELAPSED TICKS and no port ever qualified
    inc cl
    mov ch, bl
.next:
    add bx, 2
    cmp bx, MOU_PEND
    jb  .port

    cmp cl, 1                   ; exactly one, or the burst has not told us
    jne .out                    ; which port is not a contest
    mov bl, ch
    xor bh, bh
    mov byte [bx+mou_need], 1
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mou_claim - one more complete packet, on port BX (SPEC.md 9.5)
;
; [mou_need] of them IN A ROW on one port settles [mou_port] and sets
; [mou_seen]. The run is per port, like the decoder that feeds it, so the two
; ports race independently and whichever gets there first is the mouse.
;
; A run, and not the first packet, because a well-formed Microsoft packet is
; only "a byte with bit 6 set, then two without" - and a modem's 'O','K',CR,LF
; is exactly that. So is 'RING'+CRLF, and so is 'NO CARRIER'+CRLF: each of
; them completes ONE packet, which under a first-past-the-post rule handed the
; mouse's slot to the modem and masked the real mouse off for the session.
;
; But a COUNT alone is not enough either, and that is worth stating because it
; is what a first attempt got wrong: nothing decayed, so four result codes
; seconds apart accumulated to a run of four exactly as four mouse packets
; would. What actually separates a mouse from ASCII is not how many packets
; complete but that a mouse's bytes are ALL of them - they arrive in threes
; with nothing between - so the ISR breaks the run on either protocol
; violation: a bit-6 byte arriving mid-packet (a resync) and a bit-6-clear
; byte arriving between packets (a stray). Every modem string above dies on
; its second byte: 'RING' is R then I, both bit-6, and that is a resync.
; mou_hotplug's reset edge clears the run too, so nothing can be built up
; across one.
;
; The residual is a port carrying BINARY traffic in the seconds before the
; mouse is first moved - random bytes do satisfy the protocol occasionally,
; and MOU_LOCKN is what prices that: eight clean packets is 24 bytes that all
; had to fall right, against a mouse that emits them in a fifth of a second of
; motion. It is bounded anyway by mou_lockon masking the loser the moment the
; mouse is found.
;
; Nothing the OTHER port does can break this port's run, and that separation is
; the whole reason the decoders are per port. It was shared once, and a modem
; chattering on COM1 then reset the run of a mouse moving on COM2 - so the
; mouse never reached the threshold and never claimed its port at all, while
; the modem's own violations kept it from claiming one either. A device that
; cannot win must not be able to stop the one that should.
;
; ISR context, IF = 0.
; ins:  BX = the port row the packet arrived on
; outs: none (all registers preserved)
; -----------------------------------------------------------------------------
mou_claim:
    push ax
    cmp byte [mou_seen], 0
    jne .out                    ; settled, and never revisited
    inc byte [bx+mou_run]
    mov al, [bx+mou_need]       ; 1 where the probe found one port, MOU_LOCKN
    cmp byte [bx+mou_run], al   ; where it found two - and 1 again on a port
                                ; whose identify burst already named it the
                                ; only mouse-shaped one (SPEC.md 9.4.1)
    jb .out
    mov [mou_port], bl          ; settled. mou_hotplug's next pass retires
    mov byte [mou_seen], 1      ; every other port
    mov al, [mou_lfired]        ; ...and THIS is the line those packets came in
    mov [mou_line], al          ; on, which the base cannot say (SPEC 9.5.2.1)
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; mou_newround - forget every port's part-built packet and part-built run
;
; A reset edge is a fresh contest and nothing may be carried across one. That
; is not tidiness: without it a modem accumulated a run out of result codes
; SECONDS apart - 'OK' then 'RING' then 'NO CARRIER', one packet each - which
; is indistinguishable, to a counter that never decays, from a mouse being
; moved.
; ins:  none
; outs: none (all registers preserved)
; -----------------------------------------------------------------------------
mou_newround:
    mov word [mou_phase], 0
    mov word [mou_phase+2], 0
    mov word [mou_run], 0
    mov word [mou_run+2], 0
    ret

; -----------------------------------------------------------------------------
; mou_lockon - the mouse has spoken on port [mou_port]: retire the others
;
; IER = 0 so the UART stops interrupting, and its IRQ masked at the 8259. The
; base is deliberately LEFT in place, because mouse_unhook still owes that
; port its original vector back.
;
; A second serial device is somebody else's - a modem, a printer, a terminal -
; and once the mouse is found its bytes can only cost interrupts and, through
; the ISR's shared packet state, corrupt real packets.
;
; THE MASK IS TAKEN FROM THE LINE, NOT FROM THE BASE (SPEC.md 9.5.2.1). 9.5.2
; fixed the ISR and left this behind: a loser's row says it is on IRQ4 because
; its base is 3F8, and on a Compaq Portable III the MOUSE is the thing on IRQ4
; - so retiring the loser masked the winner's own line. One movement settled
; the port, this ran on the UI task's next pass, and the mouse was dead for
; the session. IER = 0 is what actually silences a loser; the mask is the half
; that needed to know which wire, and only the ISR can say.
; ins:  none (reads [mou_port] and [mou_line])
; outs: none (all registers preserved)
; -----------------------------------------------------------------------------
mou_lockon:
    push ax
    push bx
    push cx
    xor bx, bx
.port:
    cmp bl, [mou_port]
    je .next
    cmp word [bx+mou_bases], 0
    je .next
    mov ax, 0x0100              ; IER = 0 - and THIS is what silences the
    call mou_pout               ; loser, whatever line it turns out to drive
    mov cl, [bx+mou_masks]      ; the line its BASE says it is on...
    mov al, [mou_line]          ; ...against the one the mouse's packets
    or  al, al                  ; actually arrived on. Unknown (impossible:
    jz .next                    ; mou_claim sets it with [mou_seen]) masks
    cmp cl, al                  ; nothing, because the failure is silent
    je .next                    ; and permanent
    in  al, 0x21
    or  al, cl
    out 0x21, al
.next:
    add bx, 2
    cmp bx, MOU_PEND
    jb .port
%ifdef KERN_BIG
    cmp byte [mou_port], MOU_P2ROW  ; ...and the same question asked of the
    je .keep                        ; OTHER SOCKET: a serial port winning makes
    call mou_p2_off                 ; the PS/2 mouse the loser, and a loser is
.keep:                              ; silenced (SPEC.md 9.9.4). Row 4 is no
                                    ; port, so when IT won the loop above has
                                    ; just retired both UARTs on its behalf
%endif
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mou_hotplug - keep offering a hot-plugged mouse its reset (SPEC.md 9.4)
; in:  nothing; UI task, every pass, no lock needed (port I/O + .bss only)
; out: nothing (all registers preserved)
;
; mouse_init runs blind - there is no probe, and a machine with no mouse at
; boot is indistinguishable from one whose mouse has not spoken yet. So while
; NO packet has ever arrived ([mou_seen]), this re-runs init's reset edge
; every MOU_REPOLL ticks: drop DTR/RTS (the mouse's power), hold MOU_RSTLOW,
; raise - and a mouse plugged in at any point since the last attempt gets the
; same clean power-on a cold boot with it attached does. The identify burst
; the raise provokes arrives through the ISR this time (IER is live), so the
; raise arms [mou_drain] and the ISR discards RX for MOU_DRAINT.
;
; The cycle runs on EVERY present port at once (mou_pall), for the same
; reason mouse_init does: until a packet arrives there is nothing to say
; which port the mouse is on.
;
; The first complete packet sets [mou_seen] and this becomes one cmp per
; pass, forever. Until then the cost of being wrong is nil: with no serial
; card the port was never probed into existence, and with a healthy-but-idle
; mouse a cycle re-identifies it and the drain eats the burst. The one
; visible seam is that motion STARTED inside a drain window is eaten with it
; - up to half a second, self-healing on the next packet - which is the price
; of never mistaking a PnP string for a click.
; -----------------------------------------------------------------------------
mou_hotplug:
    cmp byte [mou_seen], 0
    jne .have                   ; the mouse exists: stand down - but see .have
    push ax
    push bx
    push cx
    push dx
    mov bx, [ticks]
    mov ax, bx
    sub ax, [mou_hpt]           ; wrap-safe: elapsed since the state's stamp
    cmp byte [mou_hpst], 1
    je .low
%ifdef KERN_BIG
    cmp byte [mou_p2], 0        ; a PS/2 MOUSE answered its own reset (SPEC.md
    jne .done                   ; 9.9.4), which is the same statement as the
                                ; one below and a stronger one: there IS a
                                ; mouse on this machine, so stop offering the
                                ; serial ports power cycles under the user's
                                ; hand. Beside [mou_idany] and in state 0 ONLY,
                                ; for the reason spelled out there
%endif
    cmp byte [mou_idany], 0     ; a port answered our reset edge like a mouse
    jne .done                   ; (SPEC.md 9.4.1), so there IS one and it must
                                ; not be power-cycled under the user's hand.
                                ; Tested in state 0 ONLY - from state 1 this
                                ; would strand DTR low and leave the mouse
                                ; unpowered for the session, which is 9.4's
                                ; own trap in the shape a new caller meets it.
                                ; Nothing is retired here: an identify moves
                                ; the prior, it does not settle the contest
    cmp ax, MOU_REPOLL          ; state 0: waiting out the poll interval
    jb .done
    mov ax, 0x0408              ; drop DTR/RTS - power the mouse down. OUT2
    call mou_pall               ; stays up so the IRQ gate never glitches
    mov [mou_hpt], bx
    mov byte [mou_hpst], 1
    jmp short .done
.low:
    cmp ax, MOU_RSTLOW          ; state 1: held low long enough?
    jb .done
    mov ax, 0x040B              ; the rising edge a serial mouse resets on
    call mou_pall
    mov [mou_hpt], bx
    mov byte [mou_hpst], 0
    mov [mou_dstamp], bx        ; the burst comes through the ISR now: arm
    mov byte [mou_drain], 1     ; the discard window
    call mou_newround           ; ...and a fresh reset edge is a fresh contest
.done:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

.have:                          ; proof can arrive at an awkward moment: a
    cmp byte [mou_hpst], 2      ; byte the UART latched before a drop can
    je .out                     ; complete a packet while DTR is LOW, and
    push ax                     ; standing down right there would leave the
    push bx                     ; mouse UNPOWERED forever - deader than the
    push cx                     ; bug this exists to fix. So put the power
    push dx                     ; back up on every port FIRST...
    mov ax, 0x040B
    call mou_pall
    call mou_lockon             ; ...and only then retire the ports the mouse
    mov byte [mou_hpst], 2      ; did not speak on. State 2 is terminal: this
    pop dx                      ; is two compares per UI pass from here on
    pop cx
    pop bx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; mouse_unhook - silence every UART we programmed (IER=0), mask its IRQ again,
;                restore its old vector. Used before reboot.
;
; This walks the ports the PROBE found, not the one the mouse turned out to be
; on, which is why mou_lockon leaves a retired port's base in place: a vector
; still pointing into KERNEL_SEG after the handoff is a vector nobody owns.
; ins:  none
; outs: none (all registers preserved)
; -----------------------------------------------------------------------------
mouse_unhook:
    push ax
    push bx
    push cx
    push si
    push es

    mov ax, 0x0100              ; IER = 0: no more UART interrupts
    call mou_pall

    xor ax, ax
    mov es, ax
    xor bx, bx
.port:
    cmp word [bx+mou_bases], 0
    je .next
    mov cl, [bx+mou_masks]      ; mask this port's IRQ...
    in  al, 0x21
    or  al, cl
    out 0x21, al
    mov si, [bx+mou_ivecs]      ; ...and put its vector back
    pushf
    cli
    mov ax, [bx+mou_old_off]
    mov [es:si], ax
    mov ax, [bx+mou_old_seg]
    mov [es:si+2], ax
    popf
.next:
    add bx, 2
    cmp bx, MOU_PEND
    jb .port

    ; --- ...and the AUXILIARY PORT, if a mouse ever answered on it (9.9.4) ---
    ; mou_p2_off is a no-op unless the PS/2 mouse is live - it may already have
    ; lost the contest and been retired - but the VECTOR is owed back either
    ; way, which is why [mou_p2st] and not [mou_p2] is what gates it: 8 is the
    ; step that installed it, and mou_p2_off clears [mou_p2] without touching
    ; int 74h. It is int 09h's trap below, on the other line.
%ifdef KERN_BIG
    call mou_p2_off
    cmp byte [mou_p2st], 8
    jb .nop2
    mov si, MOU_P2VEC
    pushf
    cli
    mov ax, [mou_p2old]
    mov [es:si], ax
    mov ax, [mou_p2old+2]
    mov [es:si+2], ax
    popf
.nop2:
%endif

    ; --- AND INT 09h, WHICH NOTHING PUT BACK (SPEC.md 9.6.5) -----------------
    ; mouse_init hooks int 09h for keypad 5 and this is the only place that
    ; can undo it. It went missing because the loop above walks mou_ivecs and
    ; the keyboard is not a UART, so the one vector installed outside that
    ; table was the one vector nothing restored - and the cost of that is not
    ; a leak, it is a HARD FREEZE ON THE NEXT KEYSTROKE.
    ;
    ; The only caller is sched_unhook, and the only caller of THAT is the
    ; Chip menu's Restart, which ends in int 19h. int 19h is the bootstrap
    ; loader and not POST: it does not reset the interrupt vector table. So
    ; int 09h survived the restart pointing at KERNEL_SEG:kbm_isr, the boot
    ; sector read a fresh kernel over that same segment, and mouse_init then
    ; saved the vector it found - its own predecessor, at its own offset -
    ; into kbm_old9. kbm_isr's `jmp far [cs:kbm_old9]` was a jump to itself.
    ;
    ; It does not grow the stack, so nothing overruns and nothing faults: it
    ; is a tight loop inside an interrupt gate, so IF is 0, IRQ1 is in service
    ; with no EOI coming, and the machine stops dead with the picture it had.
    ; The first key pressed after a soft restart ended the session.
    pushf
    cli
    mov ax, [kbm_old9]
    mov [es:0x24], ax
    mov ax, [kbm_old9+2]
    mov [es:0x26], ax
    popf

    pop es
    pop si
    pop cx
    pop bx
    pop ax
    ret

%ifdef KERN_BIG
; =============================================================================
; The PS/2 mouse (SPEC.md 9.9)
;
; KERN_BIG ONLY, and that is SPEC.md 62.9.15's split used the way it is meant
; to be: kern_small is the 128-256KB machine's kernel, every one of which is an
; XT whose keyboard is an 8255 and whose [cpu_tier] would refuse all of this at
; run time anyway. So it is compiled out there rather than carried as ~700
; bytes that provably cannot execute - the STATE below stays in both builds, so
; that SPEC.md 9.4.2's published block is the same shape whatever kernel a test
; package is reading.
;
; The 8042 keyboard controller's AUXILIARY device port, on IRQ12 / int 74h,
; which is where every machine above an XT puts its mouse.
;
; UNLIKE A SERIAL MOUSE, THIS ONE ANSWERS. SPEC.md 9.4 opens with "there is no
; probe - a machine with no mouse is indistinguishable from one whose mouse has
; not spoken yet", and that is a fact about a serial mouse and false here: a
; reset the device acknowledges with 0xFA and completes with 0xAA is a probe in
; exactly the sense the serial side has never had. So there is no run threshold
; on this side and nothing to weigh against - the aux port carries ONE device -
; and the first complete packet settles [mou_port] and is acted on, which is
; SPEC.md 9.5's "one port is not a contest" reached by a stronger road.
;
; NOTHING HERE RUNS ON THE TARGET MACHINE. An 8088's keyboard is an 8255 PPI at
; 60h-63h, 64h is not decoded at all, and what a write there reaches is a
; question about one machine's address decoding that nobody should have to
; answer to boot a desktop. The whole module is behind [cpu_tier], so on an XT
; mou_p2_init is one compare and a ret (SPEC.md 9.9.2).
; =============================================================================

; -----------------------------------------------------------------------------
; mou_p2wr - wait until the 8042's input buffer is empty, so a write is safe
; ins:  none
; outs: CF = 1 on timeout (all registers preserved)
; -----------------------------------------------------------------------------
mou_p2wr:
    push ax
    push cx
    push dx
    mov dx, [ticks]
.w:
    in  al, MOU_P2CMD
    test al, 0x02               ; IBF: the controller has not taken the last
    jz .ok                      ; byte yet
    mov cx, [ticks]
    sub cx, dx                  ; wrap-safe: elapsed since the wait began
    cmp cx, MOU_P2TMO
    jb .w
    stc
    jmp short .out
.ok:
    clc
.out:
    pop dx                      ; a pop does not touch the flags, so CF is the
    pop cx                      ; answer all the way out
    pop ax
    ret

; -----------------------------------------------------------------------------
; mou_p2rd - take the next byte the 8042 offers
; ins:  none
; outs: AL = the byte, CF = 1 on timeout. Every other register preserved.
;
; It takes whatever is in that buffer, and that is safe ONLY because every
; caller runs inside mou_p2_init's 0xAD window: with the keyboard interface
; disabled nothing but the auxiliary device can put a byte there. Outside that
; window this would steal keystrokes from int 09h, which is why mou_p2_off
; below - the one PS/2 routine that runs on the UI task - reads nothing.
; -----------------------------------------------------------------------------
mou_p2rd:
    push cx
    push dx
    mov dx, [ticks]
.w:
    in  al, MOU_P2CMD
    test al, 0x01               ; OBF: is there anything to take?
    jnz .got
    mov cx, [ticks]
    sub cx, dx
    cmp cx, MOU_P2TMO
    jb .w
    stc
    jmp short .out
.got:
    in  al, MOU_P2DAT
    clc
.out:
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; mou_p2cw / mou_p2dw - a command byte to 64h / a data byte to 60h
; ins:  AL = the byte
; outs: CF = 1 if the controller never took it (all registers preserved)
; -----------------------------------------------------------------------------
mou_p2cw:
    call mou_p2wr
    jc .out
    out MOU_P2CMD, al
    clc
.out:
    ret

mou_p2dw:
    call mou_p2wr
    jc .out
    out MOU_P2DAT, al
    clc
.out:
    ret

; -----------------------------------------------------------------------------
; mou_p2wcmd - write AL as the 8042's COMMAND BYTE (0x60, then the value)
; ins:  AL = the new command byte
; outs: CF = 1 if either half stalled (all registers preserved)
; -----------------------------------------------------------------------------
mou_p2wcmd:
    push ax
    mov al, 0x60
    call mou_p2cw
    pop ax
    jc .out
    call mou_p2dw
.out:
    ret

; -----------------------------------------------------------------------------
; mou_p2dev - send AL to the AUXILIARY DEVICE and collect its acknowledgement
; ins:  AL = the device command
; outs: CF = 1 if the write stalled or the device did not answer 0xFA.
;       AL destroyed; every other register preserved.
; -----------------------------------------------------------------------------
mou_p2dev:
    push ax
    mov al, 0xD4                ; "the next data byte is for the aux device"
    call mou_p2cw
    pop ax
    jc .out
    call mou_p2dw
    jc .out
    call mou_p2rd
    jc .out
    cmp al, 0xFA                ; ACK, and nothing else will do: 0xFE is a
    je .out                     ; resend and 0xFF is a device that is not there
    stc
.out:
    ret

; -----------------------------------------------------------------------------
; mou_p2flush - empty the 8042's output buffer
; ins:  none
; outs: none (all registers preserved)
; -----------------------------------------------------------------------------
mou_p2flush:
    push ax
    push cx
    mov cx, 16                  ; BOUNDED: a controller wedged with OBF stuck
.f:                             ; set must not hang the boot
    in  al, MOU_P2CMD
    test al, 0x01
    jz .out
    in  al, MOU_P2DAT
    loop .f
.out:
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mou_p2_init - probe the auxiliary port and, if a mouse answers, commit to it
;
; The five steps of SPEC.md 9.9.1, each a QUESTION whose wrong answer abandons
; the whole thing and puts the controller back exactly as it was found, and
; then the commitment - which nothing reaches until a device has answered
; 0xFA 0xAA to a reset edge this kernel made.
;
; ins:  none
; outs: none (all registers preserved). [mou_p2] = 1 if the mouse is live and
;       [mou_p2st] = how far the handshake got either way (SPEC.md 9.4.4).
; -----------------------------------------------------------------------------
mou_p2_init:
    cmp byte [cpu_tier], CPU_8086
    je .none                    ; an XT has no 8042 - and this is the whole of
                                ; what it costs there (SPEC.md 9.9.2). NOTHING
                                ; below has run, so nothing has been written
    push ax
    push bx
    push si
    push es

    in  al, 0xA1                ; IRQ12 MASKED BEFORE ANYTHING IS ASKED. The
    or  al, MOU_P2SL            ; machine's BIOS may have left the line armed
    out 0xA1, al                ; with a handler of its own, and that handler
                                ; reading 60h takes the replies below out from
                                ; under this routine - which reads exactly like
                                ; a controller that stopped answering. It stays
                                ; masked to the last instruction, so nothing
                                ; here is ever racing an interrupt

    mov al, 0xAD                ; THE KEYBOARD INTERFACE OFF, and not for
    call mou_p2cw               ; tidiness: the 8042 has ONE output buffer and
    jc .quit                    ; int 09h is live, so a keystroke landing in
                                ; the middle of this raises IRQ1, the BIOS
                                ; reads 60h, and the byte it takes is OUR
                                ; device's reply. The keystroke is delivered
                                ; out of the keyboard's own buffer when 0xAE
                                ; puts the interface back below
    call mou_p2flush

    mov byte [mou_p2st], 1      ; the command byte as this machine's BIOS left
    mov al, 0x20                ; it, banked: every write below goes over this
    call mou_p2cw               ; value and every failure path restores it
    jc .quit
    call mou_p2rd
    jc .quit
    and al, 0xEF                ; ...WITHOUT bit 4, which is 0xAD's own doing
    mov [mou_p2cmd0], al        ; two lines up. Banking it set would make every
                                ; path that restores this byte disable the
                                ; keyboard - and mou_p2_off does that on the UI
                                ; task, long after the 0xAE below has run

    mov byte [mou_p2st], 2      ; IS THERE AN AUXILIARY PORT HERE AT ALL?
    mov al, 0xA8                ; enable it...
    call mou_p2cw
    jc .fail
    mov al, 0xA9                ; ...and TEST it. 0 is "no error"; 0xFF is "no
    call mou_p2cw               ; auxiliary interface"; anything else names a
    jc .fail                    ; stuck clock or data line; and a controller
    call mou_p2rd               ; that never heard of the command answers
    jc .fail                    ; nothing and times out. This is what makes
    or  al, al                  ; every write below safe on a machine with no
    jnz .fail                   ; aux port - it is a question, and only 0
                                ; licenses a single one of them

    mov byte [mou_p2st], 3
    mov al, [mou_p2cmd0]
    and al, 0xDD                ; bit 5 clear: run the aux clock. Bit 1 clear:
    call mou_p2wcmd             ; IRQ12 stays OFF until the vector is ours
    jc .fail

    mov byte [mou_p2st], 4      ; reset the device...
    mov al, 0xFF
    call mou_p2dev
    jc .fail
    mov byte [mou_p2st], 5      ; ...and wait out its self-test, which the part
    mov bx, MOU_P2BAT           ; is allowed half a second for. The SHORT wait
.bat:                           ; retried, rather than a second timeout to size
    call mou_p2rd
    jnc .bdone
    dec bx
    jnz .bat
    jmp short .fail
.bdone:
    cmp al, 0xAA                ; BAT passed?
    jne .fail
    mov byte [mou_p2st], 6
    call mou_p2rd               ; the device ID: BANKED AND PUBLISHED, never
    jc .fail                    ; tested - a wheel mouse sitting in its 3-byte
    mov [mou_p2id], al          ; boot mode is exactly the mouse this decoder
                                ; wants, and refusing it would buy nothing

    ; --- a mouse answered: from here it is commitment, not probing -----------
    ; THE VECTOR FIRST, and that ordering is not tidiness - it is the defect
    ; this was found with. Arming IRQ12 in the command byte while int 74h still
    ; points at the machine's own BIOS hands the very next byte - the 0xF4
    ; acknowledgement - to a handler that reads 60h and drops it, and the enable
    ; then times out with [mou_p2st] stuck at this step. It looks exactly like
    ; a controller that stopped answering, on a machine where every earlier
    ; step passed.
    mov byte [mou_p2st], 7
    xor ax, ax
    mov es, ax
    mov si, MOU_P2VEC
    pushf
    cli
    mov ax, [es:si]
    mov [mou_p2old], ax
    mov ax, [es:si+2]
    mov [mou_p2old+2], ax
    mov word [es:si], mou_p2_isr
    mov [es:si+2], cs
    popf
    mov byte [mou_p2st], 8      ; ...and 8 is what says the vector is OURS,
                                ; which is the one thing mouse_unhook has to
                                ; know and [mou_p2] cannot tell it: mou_p2_off
                                ; clears that byte when this mouse loses the
                                ; contest, and the vector is still owed back

    mov al, [mou_p2cmd0]
    and al, 0xDF                ; the aux clock on...
    or  al, 0x02                ; ...and IRQ12 armed AT THE CONTROLLER, still
    call mou_p2wcmd             ; masked at the 8259 and now pointing at us
    jc .fail
    mov al, 0xF4                ; start data reporting
    call mou_p2dev
    jc .fail
    mov byte [mou_p2st], 9      ; live

    call mou_p2flush            ; before 0xAE, so a keystroke arriving as the
    mov al, 0xAE                ; interface comes back is not flushed away
    call mou_p2cw
    in  al, 0xA1                ; IRQ12 at the SLAVE...
    and al, 0xEF                ; ...clearing MOU_P2SL
    out 0xA1, al
    in  al, 0x21                ; ...and IRQ2 at the master, the CASCADE -
    and al, 0xFB                ; clearing MOU_P2CASC. Without it the slave's
    out 0x21, al                ; line reaches nothing, and the symptom is a
                                ; mouse that passed every step above and is
                                ; then silent for the session
    mov byte [mou_p2], 1
    jmp short .quit

.fail:                          ; put the controller back the way it was found
    mov al, 0xA7                ; the auxiliary port off again...
    call mou_p2cw
    mov al, [mou_p2cmd0]        ; ...and its command byte exactly as the BIOS
    call mou_p2wcmd             ; had it. IRQ12 is deliberately LEFT masked:
                                ; nothing else in this kernel uses that line,
                                ; the port above is now unclocked so nothing
                                ; can drive it, and a probe that failed has no
                                ; business handing the line back to a handler
                                ; whose device it just power-cycled. It
                                ; self-heals - int 19h re-enters mouse_init,
                                ; which unmasks on the path that succeeds
.quit:
    mov al, 0xAE                ; the keyboard interface, ON EVERY PATH - a
    call mou_p2cw               ; boot that leaves it disabled is a machine
    pop es                      ; with no keyboard, which is worse than every
    pop si                      ; failure this routine is guarding against
    pop bx
    pop ax
.none:
    ret

; -----------------------------------------------------------------------------
; mou_p2_off - retire the PS/2 mouse: a serial port won the contest
;
; The mirror of mou_lockon's IER = 0 (SPEC.md 9.9.4). The vector is deliberately
; LEFT installed, for the reason a retired UART keeps its base: mouse_unhook
; still owes int 74h its own back, and a vector pointing into KERNEL_SEG after
; the handoff is a vector nobody owns.
;
; NOTHING HERE READS 60h. This is the one PS/2 routine that runs on the UI task
; rather than inside mouse_init's 0xAD window, so the keyboard interface is
; live and a read there would take a keystroke away from int 09h. Switching the
; auxiliary port off at the controller is enough: an unclocked device cannot
; report, so there is nothing to acknowledge and nothing to drain.
;
; ins:  none
; outs: none (all registers preserved)
; -----------------------------------------------------------------------------
mou_p2_off:
    cmp byte [mou_p2], 0
    je .out
    push ax
    mov byte [mou_p2], 0
    in  al, 0xA1                ; IRQ12 masked at the slave...
    or  al, MOU_P2SL
    out 0xA1, al
    mov al, 0xA7                ; ...the auxiliary port switched off, so the
    call mou_p2cw               ; device is not even clocked...
    mov al, [mou_p2cmd0]        ; ...and the command byte back as the BIOS had
    call mou_p2wcmd             ; it, IRQ12 bit and all
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; mou_p2_isr - IRQ12, int 74h (SPEC.md 9.9.3)
;
; The 8042 has ONE output buffer for two devices, so the first question here is
; whose byte this is: status bit 5 set means the auxiliary device's. A KEYBOARD
; BYTE IS LEFT WHERE IT IS - int 09h owns it, and reading 60h here would make
; it vanish, which is a keystroke lost with nothing to show for it.
;
; IRQ12 is on the SLAVE 8259, so the EOI at the bottom is two of them: the
; slave for the line itself and the master for the cascade.
; Runs with IF = 0 throughout; never sti (SPEC.md 7).
; -----------------------------------------------------------------------------
mou_p2_isr:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    mov ax, KERNEL_SEG
    mov ds, ax
    cld
    in  al, MOU_P2CMD
    test al, 0x01               ; anything in the buffer at all?
    jz .eoi
    test al, 0x20               ; ...and is it the AUXILIARY device's?
    jz .eoi
    in  al, MOU_P2DAT           ; ...and THIS read is what drops the request
    call mou_p2_byte
.eoi:
    mov al, 0x20                ; the SLAVE first and then the master: the BIOS
    out 0xA0, al                ; handles IRQ12 for nobody, and a cascaded line
    out 0x20, al                ; owes both controllers an EOI
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
; mou_p2_byte - one byte from the auxiliary device
;
;   byte 0    YV XV YS XS  1 MB RB LB
;   byte 1    X movement, 8 bits, sign in byte 0's XS
;   byte 2    Y movement, likewise - AND POSITIVE IS UP
;
; in:  AL = the byte; ISR context, IF = 0
; -----------------------------------------------------------------------------
mou_p2_byte:
    cmp byte [mou_seen], 0
    je .take                    ; nothing has won yet
    cmp byte [mou_port], MOU_P2ROW
    jne .out                    ; a serial port won: this device is on its way
                                ; to being retired and its bytes are nobody's.
                                ; The same instruction mou_byte uses, asked of
                                ; the other socket
.take:
    cmp byte [mou_p2ph], 1
    je .b1
    ja .b2
    test al, 0x08               ; byte 0's bit 3 is ALWAYS set, and it is the
    jz .out                     ; only sync this protocol offers
    mov [mou_p2b0], al
    mov byte [mou_p2ph], 1
    ret
.b1:
    mov [mou_p2b1], al
    mov byte [mou_p2ph], 2
    ret

.b2:
    mov byte [mou_p2ph], 0
    mov bl, al                  ; BL = the Y magnitude
    mov dh, [mou_p2b0]          ; DH = byte 0, and it stays there to the end
    test dh, 0xC0               ; either overflow bit: the deltas are a LIE, so
    jnz .out                    ; the packet is dropped WHOLE rather than
                                ; clamped. A dropped packet costs one report of
                                ; motion; a believed overflow flag costs a
                                ; pointer that jumps the width of the screen

    cmp byte [mou_seen], 0      ; the FIRST packet settles it and is acted on -
    jne .go                     ; there is no run threshold on this side.
    mov byte [mou_port], MOU_P2ROW  ; [mou_need] exists because a Hayes result
    mov byte [mou_line], MOU_P2LINE ; code is a well-formed Microsoft packet
    mov byte [mou_seen], 1      ; (SPEC.md 9.5.1); the aux port carries ONE
                                ; device and it answered a reset before its
                                ; interrupt was ever armed (SPEC.md 9.9.4).
                                ; MOU_P2LINE is not a master-8259 bit, so
                                ; mou_lockon masks both serial lines and
                                ; neither of this mouse's own
.go:
    mov al, [mou_p2b1]          ; --- dx: 8 bits, sign in byte 0's bit 4 ------
    xor ah, ah
    test dh, 0x10
    jz .xp
    dec ah                      ; AH = 0FFh. Sign-extended BY HAND: an 8086 has
.xp:                            ; no movsx, and cbw would take the magnitude's
    mov si, ax                  ; own top bit for the sign

    mov al, bl                  ; --- dy, AND POSITIVE IS UP ON THIS WIRE -----
    xor ah, ah
    test dh, 0x20
    jz .yp
    dec ah
.yp:
    neg ax                      ; ...so the screen's dy is its negation, and
    mov bx, ax                  ; this one line is the whole difference. Getting
                                ; it wrong looks exactly like a working mouse

    mov cl, dh
    and cl, 0x03                ; bit 0 = left, bit 1 = right: already
                                ; mouse_btn's own order, so the shared back end
                                ; needs no translation. The middle button (bit
                                ; 2) is read and dropped - SPEC.md 10's event
                                ; set has no record for it
    mov ax, si
    jmp mou_apply
.out:
    ret
%endif                          ; KERN_BIG - the PS/2 mouse (SPEC.md 9.9)

; =============================================================================
; The keyboard mouse (SPEC.md 9.6)
;
; With no mouse on the machine there is no pointer, and with no pointer there
; is nothing to click: the menu bar, the dock, the drive icons and every
; window control are unreachable, so the desktop is a picture of an operating
; system rather than one. The arrow keys become the mouse.
;
; It is gated on [mou_seen], the same byte 9.4/9.5 already keep, so a machine
; with a working mouse never enters any of this - the cost there is one
; compare per keystroke.
;
; WHAT IT TAKES FROM APPLICATIONS, which is the part worth deciding rather
; than discovering. An 83/84-key keyboard has no separate cursor pad: the
; arrows ARE the numeric keypad, and which one you get is NumLock. So
;
;   NumLock off   int 16h returns AL = 0 and a scancode. These are the keys
;                 this takes, and an application does not see them.
;   NumLock on    int 16h returns ASCII digits. Untouched, always.
;
; An application that wants arrow keys on a mouseless machine cannot have
; them while this is on, and that is the right default - without a pointer it
; could not have been launched. ScrollLock is one escape hatch: while it is on
; nothing here intercepts anything, so Arkanoid can be played and Note Pad's
; caret moved on a machine with no mouse. It is READ, not watched for - see
; kbm_slock - and it is the ONLY one, which SPEC.md 9.6.2 records the reason
; for: the way back to typing can never be a key you would want to type, and a
; '.' latch lasted a day for exactly that reason. ScrollLock costs no
; keystroke at all, and on a keyboard with the lamp it shows its own state.
; kbm_ui watches its EDGE and raises the window under the pointer on the way
; in, because keys go to wm_top.
;
; ---------------------------------------------------------------------------
; WHAT THIS COSTS, AND WHEN TO TAKE IT OUT AGAIN
;
; 520 bytes of .text, which is two 512-byte steps of KERN_BUDGET: the spare
; went 4,096 -> 3,584 -> 3,072. The first was decided on when the budget was
; at its OLD ceiling and that step was half the slack rather than an eighth of
; it, on the grounds that this is the difference between a usable machine and
; an unusable one at exactly the moment the mouse fails. The second bought
; 9.6.1 and 9.6.2, and it is 114 bytes of code for a whole step: the image
; rung had ONE byte of slack (.text + .bss = 49,151 against a 49,152
; ceiling), so any addition at all cost the same 512 and trimming could not
; have avoided it. That is the guard working, not this being dear - and the
; next byte added to .text anywhere pays 512 too.
;
; IF FOOTPRINT EVER BECOMES THE HIGHER PRIORITY, THIS IS A FIRST CANDIDATE TO
; REMOVE - dropped outright, or built only into the testing and benchmark
; kernels, where the harness drives the mouse over QMP and never needs it.
; Nothing else depends on it: this module plus six call sites (ui_task's key
; poll and its deferred ladder, the kbm_poll in menu_track / ui_drag /
; ui_grow, and osapi_mouse). Recommend it to whoever is spending the budget
; rather than removing it unasked.
; docs/KERNEL-MEMORY.md carries the same note beside the numbers.
; =============================================================================
KBM_MIN     equ 3               ; px a press moves when the keys are tapped...
KBM_ACC     equ 3               ; ...how much a repeat adds while one is held
KBM_MAX     equ 24              ; ...and the ceiling. A 640px screen is 320
                                ; presses at KBM_MIN, which is not a feature;
                                ; typematic repeat plus this makes it ~40
KBM_GAP     equ 3               ; ticks: presses closer than this are a HOLD.
                                ; Typematic repeat is ~100ms (1-2 ticks) and a
                                ; deliberate tap is 200ms+ (4+), so this is
                                ; where the two separate on an 18.2Hz clock

; scancode, dx, dy - the eight directions of the keypad
kbm_tab     db 0x47, -1, -1     ; Home  up-left
            db 0x48,  0, -1     ; Up
            db 0x49,  1, -1     ; PgUp  up-right
            db 0x4B, -1,  0     ; Left
            db 0x4D,  1,  0     ; Right
            db 0x4F, -1,  1     ; End   down-left
            db 0x50,  0,  1     ; Down
            db 0x51,  1,  1     ; PgDn  down-right
KBM_NDIR    equ 8

; -----------------------------------------------------------------------------
; kbm_key - the UI task's keystroke, before anything else sees it
; in:  AL = ascii, AH = scan (as int 16h returned them)
; out: CF = 1 if this key was the mouse and must not be dispatched
;      (all registers preserved)
; -----------------------------------------------------------------------------
kbm_key:
    cmp byte [mou_seen], 0
    jne kbm_pass                ; a real mouse has spoken: never intercept
    call kbm_slock              ; ...or ScrollLock is on and the user has asked
    jnz kbm_pass                ; for the keys back
    cmp al, ' '                 ; Space: the button, for a hand already on the
    je kbm_btn                  ; arrows. Tested above the ASCII gate because it
                                ; carries a byte - and it is the one printable
                                ; key this takes, which is what makes the hatch
                                ; below have to be a key that carries none
    cmp ah, 0x4C                ; the keypad's centre 5: the button, and tested
    je kbm_btn                  ; ABOVE the gate as well because it is the one
                                ; keypad key with NO cursor function - so what
                                ; a BIOS puts in AL for it is that BIOS's
                                ; business. SeaBIOS says 0 and the field
                                ; machine says otherwise, and testing AL is
                                ; what made this key dead there while every
                                ; test here passed (SPEC.md 9.6.1)
    or al, al
    jnz kbm_pass                ; ASCII - NumLock is on and these are digits,
                                ; which belong to whatever is running
    cmp ah, 0x52                ; the keypad's 0 (Ins)...
    je kbm_btn
    cmp ah, 0x53                ; Del: the right button (SPEC.md 12.4)
    je kbm_rclick

    push bx
    push cx
    push dx
    mov bx, kbm_tab
    mov cx, KBM_NDIR
.find:
    cmp ah, [bx]
    je kbm_move
    add bx, 3
    loop .find
    pop dx
    pop cx
    pop bx
kbm_pass:
    clc
    ret

; kbm_slock - ZF = 0 when ScrollLock is on
;
; It is read as a LEVEL out of the BIOS data area and not watched for as a
; keystroke, because it is not one: ScrollLock is a shift STATE, int 09h
; swallows it to toggle KB_FLAG, and int 16h never reports a key for it. The
; first version tested for scancode 46h and could not have worked - the byte
; never arrives. The upside of being wrong there is that a level needs no
; state of its own, and on a keyboard with the lamp the machine says which
; mode it is in.
; out: ZF (all registers preserved)
kbm_slock:
    push ax
    push es
    mov ax, 0x40
    mov es, ax
    test byte [es:0x17], 0x10   ; KB_FLAG bit 4 = Scroll Lock active
    pop es                      ; pop does not disturb flags
    pop ax
    ret

; -----------------------------------------------------------------------------
; mou_clamp - the nearest legal pointer position (SPEC.md 39.15.4)
;
; in:       AX = x, BX = y, virtual, possibly outside every display
; out:      AX/BX legal
; clobbers: AX, BX, flags. CX and DX SURVIVE - mou_isr still has the staged
;           button byte in DX and kbm_move its step in DL.
;
; ONE DISPLAY IS THE OLD PER-AXIS CLAMP, unchanged: [vid_wm1]/[vid_hm1] are
; display 0's extent there, and this is the same six compares the two callers
; used to carry inline.
;
; TWO IS NOT A RECTANGLE. The virtual desktop is a union (SPEC.md 39.2.1), so
; clamping each axis independently would walk the pointer into the DEAD ZONE
; and lose it. The rule instead:
;
;     inside SOME display -> take it
;     otherwise           -> clamp into the display the arrow is on
;
; which produces every behaviour the seam needs without naming any of them.
; Off display 0's right edge at a y both cover is inside display 1, so the
; pointer CROSSES. Off display 1's outer edge is inside nothing, so it stops
; there. Down off display 0 beside display 1's left edge is inside nothing
; either, so it stops at display 0's last row rather than vanishing into the
; gap. And back left from display 1 at a y display 0 covers crosses home.
; -----------------------------------------------------------------------------
mou_clamp:
%ifdef KERN_BIG
    cmp byte [vid_ndisp], 1
    ja .multi
%endif
    or ax, ax                   ; x into 0..[vid_wm1]
    jns .xlo
    xor ax, ax
.xlo:
    cmp ax, [vid_wm1]
    jle .xok
    mov ax, [vid_wm1]
.xok:
    or bx, bx                   ; ...and y into 0..[vid_hm1]
    jns .ylo
    xor bx, bx
.ylo:
    cmp bx, [vid_hm1]
    jle .yok
    mov bx, [vid_hm1]
.yok:
    ret
%ifdef KERN_BIG
.multi:
    push cx
    push ax                     ; vid_disp_find answers in AL
    call vid_disp_find
    pop ax
    jnc .mout                   ; inside a display already: nothing to correct
    push bx
    push ax
    mov ax, [mouse_x]           ; the display under the PRE-STEP position,
    mov bx, [mouse_y]           ; which the clamp invariant keeps inside SOME
    call vid_disp_find          ; display - NOT [cur_disp]: that is where the
    mov cl, al                  ; arrow was last DRAWN, and it freezes while
    pop ax                      ; the gfx lock is held, so a seam crossed
    pop bx                      ; during a repaint and then clamped into it
    call vid_pt_clamp           ; dragged the pointer a whole monitor back
.mout:
    pop cx
    ret
%endif

; --- a direction: step, clamp, and move the arrow ----------------------------
; A DROPPED MENU IS A LIST, and gets asked first (SPEC.md 9.6.3): a press with
; a vertical component there moves one ITEM rather than KBM_MIN pixels, and
; menu_kbnav has already placed the pointer when it answers CF = 0. Asking it
; ABOVE kbm_accel is what keeps the two mechanisms apart - a placed press never
; stamps [kbm_last], so the first arrow AFTER the menu closes sees a large tick
; gap and starts fresh at KBM_MIN, which is what it should do anyway.
kbm_move:
    mov al, [bx+2]              ; dy: the axis a menu is a list along
    call menu_kbnav
    jnc .paint                  ; placed - the repaint below is all that is left
    call kbm_accel              ; DL = px this press is worth, and imul does
    mov al, [bx+1]              ; not disturb it, so it serves both axes
    imul dl                     ; dx (-1, 0, +1) x the step -> AX, signed
    add ax, [mouse_x]
    push ax                     ; banked on OUR stack until y is ready, for
                                ; mou_clamp's 2-D reason (SPEC.md 39.15.4).
                                ; NOT in [mou_nx]: that word is the IRQ4
                                ; ISR's staging area, and a serial byte
                                ; landing between our store and read-back
                                ; paired the ISR's x with our y
    mov al, [bx+2]              ; dy, the same way
    imul dl
    add ax, [mouse_y]
    mov bx, ax
    pop ax
    call mou_clamp
    mov [mouse_x], ax
    mov [mouse_y], bx
.paint:
    call kbm_paint
    pop dx
    pop cx
    pop bx
    stc
    ret

; --- kbm_accel - DL = how far this press moves the pointer -------------------
; A fixed step cannot be right: small enough to aim with is too small to cross
; the screen with. Presses arriving closer together than KBM_GAP are a held
; key (typematic repeat), and the step grows while that lasts.
; clobbers: DL
kbm_accel:
    push ax
    push bx
    push cx
    mov ch, [bx]                ; the direction this press is (the row's scan)
    mov bx, [ticks]
    mov ax, bx
    sub ax, [kbm_last]          ; wrap-safe, like every other tick compare here
    mov [kbm_last], bx
    mov dl, KBM_MIN
    cmp ax, KBM_GAP
    jae .set                    ; a fresh tap starts slow again...
    cmp ch, [kbm_lastk]
    jne .set                    ; ...and so does a CHANGE OF DIRECTION, which
                                ; is what a person does the instant they
                                ; overshoot. Without it the correction is
                                ; travelling at full speed too, and a menu item
                                ; sits unreachable between two 24px strides -
                                ; which is exactly how this first failed to
                                ; pick one
    mov dl, [kbm_step]
    add dl, KBM_ACC
    cmp dl, KBM_MAX
    jbe .set
    mov dl, KBM_MAX
.set:
    mov [kbm_step], dl
    mov [kbm_lastk], ch
    pop cx
    pop bx
    pop ax
    ret

; --- kbm_paint - put the arrow where mouse_x/mouse_y now say -----------------
; The ISR's own .move rules (SPEC.md 7): draw only with the gfx lock free and
; the cursor refcount-visible, otherwise leave it to the next unlock. IF is 0
; across the move because cur_move is not re-entrant against the ISR - and on
; a machine with a modem on the other port that ISR is still live.
;
; It saves SI/DI/BP/ES around cur_move, which clobbers all four (SPEC.md
; 7.1.4.1 - a routine reached from the drawing paths inherits THEIR register
; contract). AX and BX were enough while ui_task was the only caller and both
; of the others were kernel loops; osapi_mouse calls kbm_poll now, so this is
; reached from inside a package's own tracking loop, where kbm_key's stated
; 'all registers preserved' is the whole contract and not a courtesy.
kbm_paint:
    push ax
    push bx
    pushf
    cli
    cmp byte [gfx_lock_flag], 0
    jne .dirty
    mov al, [cur_level]
    or  al, al
    js .dirty
    push si
    push di
    push bp
    push es
    mov ax, [mouse_x]
    mov bx, [mouse_y]
    call cur_move
    pop es
    pop bp
    pop di
    pop si
    mov byte [cur_dirty], 0
    jmp short .out
.dirty:
    mov byte [cur_dirty], 1
.out:
    popf
    pop bx
    pop ax
    ret

; --- the button: press = down, press again = up ------------------------------
; ONE key action, and there used to be two - a click (Ins) that pressed and
; released together, and a hold (keypad 5) that latched. They were the wrong
; pair. Nothing tracks a click, so the click key could never open a menu:
; menu_track drew the pull-down and its very next instruction polled a button
; that was already up, which is a menu that flashes and will not stay open -
; reported off a Compaq Portable III, and the same for a window drag and the
; grow box. The hold key opened them and read as broken everywhere else,
; because a button that stays down is not what an icon or a close box wants.
;
; So: a press LATCHES mouse_btn and posts EVT_MDOWN, and kbm_relax releases it
; the moment the ui_task pass that dispatched that down is over. Anything that
; dispatches and returns therefore gets a CLICK - the release lands before
; anything can poll a level. menu_track, ui_drag, ui_grow and a package's own
; tracking loop do NOT return until the button comes up, so for those the same
; press holds, and the next press releases it: press-drag-release on a
; keyboard (SPEC.md 9.6.1).
;
; A REPEAT IS NOT A PRESS. Typematic arrives ~1.8 ticks apart and a deliberate
; second press is 4+ - the same split KBM_GAP already makes for the ramp - so
; without this a press held a moment too long opened a menu and closed it
; again on its own repeat, which is the other way the Compaq's symptom is
; produced. The stamp moves on an ignored repeat too, so a key held down stays
; held rather than firing again once the repeats have drifted KBM_GAP apart.
kbm_btn:
    mov byte [kbm_p5], 0        ; WHICHEVER ROUTE GOT HERE, one press (9.6.4):
                                ; whether this BIOS discards keypad 5 or
                                ; delivers it is a property of the BIOS, so on
                                ; one that delivers it BOTH kbm_isr and this
                                ; would claim the same keystroke - the menu
                                ; opening and shutting in one press. int 16h
                                ; is always looked at first, so cancelling the
                                ; pending scancode here is what makes the hook
                                ; a FALLBACK rather than a second opinion
    push ax
    mov ax, [ticks]
    sub ax, [kbm_btick]         ; wrap-safe, like every other tick compare here
    cmp ax, KBM_GAP
    jb .held
    xor byte [mouse_btn], 0x01
    mov ax, EVT_MUP
    test byte [mouse_btn], 0x01
    jz .post
    mov ax, EVT_MDOWN
.post:
    call kbm_post
.held:
    mov ax, [ticks]
    mov [kbm_btick], ax
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; kbm_isr - int 09h, because the BIOS will not deliver the keypad's 5
;           (SPEC.md 9.6.4)
;
; Keypad 5 is the one keypad key with NO cursor function, and that is not only
; why 9.6.1 has to match it on its scancode alone - it is why, with NumLock
; OFF, THE BIOS HAS NOTHING TO TRANSLATE IT TO AND THROWS IT AWAY. Measured on
; a period BIOS, pressing it moves the keyboard buffer's head and tail not at
; all, where Ins gives 5200h and Up 4800h. So int 16h can never report it, and
; kbm_key's `cmp ah, 0x4C` can only ever fire with NumLock ON - which is the
; mode where the ASCII gate makes the arrows digits and there is no keyboard
; mouse to press a button for. The key worked only where the feature did not.
;
; That is also why every test of it passed: 9.6.1 verified this key by turning
; NumLock on, which is the one mode in which it arrives. A harness kinder than
; the machine hides exactly the bug it exists to find.
;
; So the kernel reads the SCANCODE rather than the BIOS's translation of it.
; This is the only place os8088 touches the keyboard hardware, and it does the
; least a hook can do: it PEEKS port 60h and chains. On the 8088's 8255 the
; byte is latched in port A until the BIOS acknowledges it with bit 7 of port
; 61h, so reading it early is not consuming it - the real handler below reads
; the same byte and does all the work, including the ack, the shift states and
; the EOI. Nothing here writes a port, queues a key or ends an interrupt.
;
; WHETHER THE BIOS DISCARDS IT IS A PROPERTY OF THE BIOS, NOT OF THE MACHINE
; CLASS, which is why this is installed on every tier and not only on the
; 8088. Measured: a period 5150 ROM throws keypad 5 away in cursor mode, and
; SeaBIOS DELIVERS it (AL = 0, AH = 4Ch) on the same setting. So the hook is a
; FALLBACK: on a BIOS that delivers the key, int 16h is looked at first,
; kbm_key claims it and clears [kbm_p5], and this claims nothing. Without that
; guard both would fire and one press would open a menu and shut it again.
;
; Three gates around the peek, in cost order: a mouse that has spoken (so a
; machine with one reads the port only if SPEC.md 9.7's map is armed), then
; NumLock - on, int 16h delivers the key on every BIOS tried and kbm_key has
; it - and ScrollLock, which is 9.6.2's hatch and must hand this key back with
; all the others.
;
; THE PEEK IS ALSO SPEC.md 9.7's, and that is why the first gate moved rather
; than went. A key-state map has to see every byte, including on a machine
; with a mouse - so `[kbd_armed]` is a second reason to read the port, and a
; machine where nothing has ever asked for key state still never reads it.
; That is the whole reason the map arms on being ASKED: the 8042 paragraph
; below is a claim no period AT ROM has been put to, and it stays scoped to
; the machines that need it rather than being spent on every machine.
;
; ON THE 8042 THE READ IS NOT ACKNOWLEDGED EITHER, but the reasoning differs
; and is worth stating because the first version gated this to tier 0 over it.
; An 8088's 8255 latches the byte until the BIOS pulses port 61h, so a peek is
; provably free. A 286+ 8042 clears output-buffer-full on the read while the
; data register keeps the byte, so a BIOS that tests OBF before reading would
; find none. No int 09h handler tried does test it - SeaBIOS reads 60h
; directly, and the IBM ROM is already inside the interrupt when it reads -
; and the keyboard is verified alive through this hook on both. It is the one
; claim here that a period AT ROM has not been put to.
;
; The break code (0CCh) matches nothing, and typematic repeats collapse into
; one press because kbm_btn's own KBM_GAP guard is what spends this.
; -----------------------------------------------------------------------------
KBM_5SCAN   equ 0x4C            ; the keypad's centre key, make code

kbm_isr:
    push ax
    push ds
    mov ax, KERNEL_SEG
    mov ds, ax
    call kbd_ovflow             ; SPEC.md 9.8, and AHEAD of every gate below:
                                ; a BIOS about to find its buffer full is a
                                ; nested beep and a dead machine, and that is
                                ; true on a machine with a mouse and on one
                                ; whose software never asked for key state
    mov ah, [kbd_armed]         ; SPEC.md 9.7's key-state map wants every byte
    or ah, ah                   ; that arrives; the keypad-5 half wants one
    jnz .peek                   ; byte on a mouseless machine. EITHER is a
    cmp byte [mou_seen], 0      ; reason to read the port, and neither being
    jne .chain                  ; one is what keeps an idle machine off it
.peek:
    in al, 0x60                 ; PEEK - see above: the 8255 holds it until the
                                ; BIOS acks, so this consumes nothing
    or ah, ah
    jz .p5                      ; nobody is tracking keys: keypad 5 only
    call kbd_track              ; SPEC.md 9.7 - preserves AL
.p5:
    cmp byte [mou_seen], 0
    jne .chain                  ; a real mouse has spoken: never intercept
    cmp al, KBM_5SCAN
    jne .chain
    push es
    mov ax, 0x40                ; the BIOS data area, NOT zero: KB_FLAG is at
    mov es, ax                  ; 0040:0017, and an ES of 0 reads the middle of
                                ; the vector table instead - which has bits in
                                ; 0x30 set, so the test below always chained
                                ; and this key stayed dead (kbm_slock is the
                                ; idiom; the vector install above is not)
    test byte [es:0x17], 0x30   ; KB_FLAG: NumLock (20h) - int 16h will deliver
    pop es                      ; it and kbm_key claims it - or ScrollLock
    jnz .chain                  ; (10h), the hatch. pop leaves the flags alone
    mov byte [kbm_p5], 1        ; ...otherwise it is a press, spent in task
                                ; context by kbm_p5spend
.chain:
    ; A CHAIN TO OURSELVES IS A DEAD MACHINE, so it is refused rather than
    ; taken (SPEC.md 9.6.5). mouse_unhook puts int 09h back now and that is
    ; what stops kbm_old9 ever naming this routine - but the vector outlives
    ; any path that skips the unhook, and the failure it produces is the worst
    ; in the system: a tight loop inside an interrupt gate, IF clear, IRQ1 in
    ; service and no EOI ever sent. Nothing faults, nothing overruns, the
    ; screen keeps whatever it had and every clock in the machine stops.
    ;
    ; No BIOS handler lives in the kernel's segment, so the segment alone is
    ; the whole test. Refusing costs the keystroke and an EOI the BIOS would
    ; otherwise have sent - a keyboard that has stopped working, on a machine
    ; that is still running, in a state this can no longer reach.
    mov ax, cs
    cmp [cs:kbm_old9+2], ax
    je .selfchain
    pop ds
    pop ax
    jmp far [cs:kbm_old9]       ; CS, not DS: DS is the interrupted code's again
.selfchain:
    mov al, 0x20                ; the EOI is the BIOS's job and there is no
    out 0x20, al                ; BIOS below us: without it IRQ1 stays in
    pop ds                      ; service and takes the mouse's IRQ3/IRQ4 down
    pop ax                      ; with it
    iret

; =============================================================================
; The key-state map (SPEC.md 9.7) - IS THIS KEY DOWN RIGHT NOW?
;
; int 16h answers "what was typed", which is a different question, and for a
; real-time app it is the WRONG one: there is no key-up in it and a typematic
; repeat is byte-identical to a fresh press, so "is left held" cannot be asked
; of it at all. Every consumer therefore ended up inferring a hold from the
; INTERVAL between events, and that inference has two failures nothing inside
; it can fix. It cannot see the typematic DELAY - about 9 ticks in which a
; held key produces no evidence whatever, so the app either stops dead in the
; middle of a hold or must move for the whole delay whether the key came up or
; not. And the keyboard repeats the LAST key pressed, so pressing a second key
; (a fire button) silently ends the first key's evidence while the finger is
; still on it. Both are apps/arkanoid's, both were reported off the field
; machine, and neither is an app bug.
;
; The scancodes carry the answer and always did: a make code and its break
; code differ by bit 7, and the kernel is already inside int 09h for SPEC.md
; 9.6.4's keypad 5. So one bit per key, written by that ISR, read by anyone.
;
; It is 128 bits - one per set-1 make code - and E0/E1 are skipped, because
; those are PREFIXES and name no key of their own: the key's own code follows
; the prefix and its break code follows a second one, so an extended arrow and
; the keypad arrow beside it land on the same bit, which is what a caller
; asking "is left down" means.
;
; ASKING IS WHAT ARMS IT (kbd_down), and the first answer after arming is
; always "up". A machine whose software never asks never reads port 60h at
; all - which keeps kbm_isr's unproven 8042 claim scoped to the machines that
; want the feature - and a key already held when the map arms is simply not
; seen until it is pressed again. Nothing latches wrong: a break code clears a
; bit that is already clear.
;
; What it cannot survive is a LOST break code - an interrupt dropped inside a
; long IF=0 window leaves that key reading down until it is pressed and
; released again. That is why this is advice and not an oracle: a consumer
; bounds what it does with the answer (apps/arkanoid clamps at its rails), and
; the state self-heals on the next press of the same key.
; =============================================================================
KBD_MAPSZ   equ 16              ; 128 bits, one per set-1 make code

; -----------------------------------------------------------------------------
; kbd_bit - where does this scancode live in the map?
; in:  AL = a scancode, make or break
; out: BX = the byte index, AH = the bit's mask; AL and every other register
;      kept. ONE copy of this arithmetic, because a reader and a writer that
;      disagree about which bit a key is would be a bug with no symptom until
;      one particular key was pressed.
; -----------------------------------------------------------------------------
kbd_bit:
    push cx
    mov bl, al
    and bl, 0x7F                ; make and break index the SAME bit
    mov cl, bl
    and cl, 7
    shr bl, 1                   ; 8086: no shr reg, imm - and three of these is
    shr bl, 1                   ; smaller than loading CL to shift by 3
    shr bl, 1
    mov bh, 0
    mov ah, 1
    shl ah, cl
    pop cx
    ret

; -----------------------------------------------------------------------------
; kbd_track - one scancode from kbm_isr into the map
; in:  AL = the byte port 60h just gave; DS = KERNEL_SEG
; out: nothing; preserves every register and the flags are the caller's to
;      ignore. INSIDE AN INTERRUPT, so it touches no port and ends no
;      interrupt: it is four instructions and a store.
; -----------------------------------------------------------------------------
kbd_track:
    push ax
    push bx
    cmp al, 0xE0                ; a prefix names no key: the code that follows
    je .out                     ; it does, and that code's break follows a
    cmp al, 0xE1                ; second prefix
    je .out
    call kbd_bit
    test al, 0x80               ; bit 7 = break
    jnz .up
    or [kbd_dnmap+bx], ah
    jmp short .out
.up:
    not ah
    and [kbd_dnmap+bx], ah
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; kbd_down - is this key down right now? (SPEC.md 9.7, slot 0x03F0)
; in:  AL = a make scancode (0x4B left, 0x4D right, 0x39 space, ...)
; out: CF = 1 down, CF = 0 up; every register preserved
;
; No lock, no VRAM, no port: legal from a worker task, from a callback, and
; from anywhere else a package can call at all.
; -----------------------------------------------------------------------------
kbd_down:
    push ax
    push bx
    cmp byte [kbd_armed], 0
    je .arm
.go:
    call kbd_bit
    mov al, ah
    and al, [kbd_dnmap+bx]
    add al, 0xFF                ; CF = 1 exactly when the bit was set, and the
    pop bx                      ; pops leave the flags alone
    pop ax
    ret
.arm:
    ; The map starts from "nothing is down" rather than from whatever .bss
    ; happened to hold: a bit stuck ON here is a key an app believes is held
    ; for the rest of the session, which is the one failure worth spending
    ; nine bytes to make impossible.
    mov bx, KBD_MAPSZ
.z:
    dec bx
    mov byte [kbd_dnmap+bx], 0  ; a store leaves the flags alone, so the jnz
    jnz .z                      ; below is still testing the dec
    mov byte [kbd_armed], 1     ; ...and only NOW may the ISR write here
    jmp short .go

; =============================================================================
; The overrun guard (SPEC.md 9.8) - A FULL BIOS BUFFER IS A HANG, NOT A BEEP
;
; The BIOS beeps when its 16-entry key buffer fills, and on the period IBM ROM
; that beep is `call F000:E8C0`: two `loop $` delays run with INTERRUPTS
; ENABLED. So every IRQ1 arriving inside one nests another int 09h - and
; another beep - on whichever task stack is current, and a worker gets 256
; bytes (SPEC.md 8). Measured on a 5150: seven nested copies of the same
; 26-byte frame and a halt in sch_stkdie, about a second after the first held
; key. It is a stack overrun with no bug anywhere near the stack.
;
; A margin cannot absorb it - a beep is up to ~60ms, LONGER than the ~50ms gap
; between two held keys, so once one starts every following key lands inside
; it. The buffer must never be full when the BIOS goes to write to it, which
; is the whole of the invariant below.
; =============================================================================
KBD_BUFB    equ 0x1E            ; 0040:001E - the BIOS key buffer, and one past
KBD_BUFE    equ 0x3E            ; its end. LITERAL, not 0040:0080/0082: those
                                ; two BDA words do not exist on the earliest
                                ; 5150 ROM, and winding the tail back to what
                                ; happens to be at 40:80 is a keyboard that has
                                ; stopped working (SPEC.md 9.8)
KBD_OVBEEPS equ 3               ; ...and the beeps one overrun burst is worth

; -----------------------------------------------------------------------------
; kbd_ovflow - keep the BIOS buffer off full, from inside int 09h
; in:  DS = KERNEL_SEG
; out: nothing; preserves every register, clobbers the flags (kbm_isr calls
;      this before it loads any of its own)
;
; Full, by the BIOS's own definition, is tail + 2 == head. When it is, the
; NEWEST entry is un-enqueued - the tail winds back one slot - so the handler
; below finds room, stores the arriving key where the previous newest sat, and
; never reaches its beep. What gets dropped is therefore the NEW input: the
; fifteen keys already queued keep their order and reach the application
; untouched, which is the half of the queue the user is still waiting on.
;
; IT PREDICTS NOTHING. An earlier version read the scancode and made room only
; for codes that would actually enqueue - skipping modifiers, prefixes and
; break codes, so a key RELEASE on a full buffer cost nothing. Every such
; filter is a guess with holes in it (Alt released with a pending Alt-numpad
; value enqueues a character from a BREAK code), and a hole here is not a lost
; keystroke, it is the hang above. So on a full buffer any int 09h at all
; costs the newest queued key. That is a real cost, paid knowingly, in a state
; where keys are being dropped regardless.
;
; It reads no port, which is what lets it sit ahead of SPEC.md 9.6.4's gates:
; 9.7 arms the peek on being ASKED, and a guard every machine needs must not
; quietly widen that. Two words of the BIOS data area are the whole input.
; -----------------------------------------------------------------------------
kbd_ovflow:
    push ax
    push bx
    push es
    mov ax, 0x40
    mov es, ax
    mov ax, [es:0x1A]           ; head - where the reader is
    mov bx, [es:0x1C]           ; tail - where the BIOS is about to write
    cmp ax, bx
    je .drained                 ; empty: the reader has caught up, so the next
                                ; burst is a new one and beeps again
    cmp ax, KBD_BUFB            ; a head this arithmetic cannot reason about is
    jb .out                     ; left alone and the old behaviour stands - it
    cmp ax, KBD_BUFE            ; is a BIOS whose buffer is not where every
    jae .out                    ; BIOS this boots on puts it
    test al, 1
    jnz .out
    add bx, 2                   ; the slot the BIOS is about to fill...
    cmp bx, KBD_BUFE
    jb .nowrap
    mov bx, KBD_BUFB
.nowrap:
    cmp bx, ax
    jne .out                    ; ...is not the head: there is room, and this
                                ; is the path every ordinary keystroke takes
    sub ax, 4                   ; FULL. head - 4 is the slot BEFORE the newest,
    cmp ax, KBD_BUFB            ; which is where the tail belongs once the
    jae .wind                   ; newest is gone
    add ax, KBD_BUFE - KBD_BUFB
.wind:
    mov [es:0x1C], ax
    cmp byte [kbd_ovn], KBD_OVBEEPS
    jae .out                    ; three beeps is the lot: past that the drop is
                                ; silent, so the machine spends the time
                                ; catching up rather than announcing that it
                                ; has not (SPEC.md 9.8)
    inc byte [kbd_ovn]
    mov byte [kbd_ovb], 1       ; THE ISR ONLY EVER SETS A BYTE: snd_beep takes
                                ; a critical section and may reach a loaded
                                ; driver, and stack inside a nested keyboard
                                ; interrupt is the one thing this routine
                                ; exists to stop spending. kbd_ovspend sounds
                                ; it in task context
.out:
    pop es
    pop bx
    pop ax
    ret
.drained:
    mov byte [kbd_ovn], 0
    jmp short .out

; -----------------------------------------------------------------------------
; kbd_ovspend - sound the overrun beep the ISR asked for, in TASK context
;
; Called from the two places that between them cover every state the machine
; can be in: ui_task's deferred ladder (every pass, mouse or no mouse - kbm_ui
; returns early on a machine that has one, so this cannot ride on its tail the
; way kbm_p5spend does) and kbm_poll0, for the modal loops that ladder never
; reaches. A pending beep that waits a pass is a beep slightly late, and the
; overrun it is announcing is still going on.
; out: nothing (all registers preserved, flags clobbered)
; -----------------------------------------------------------------------------
kbd_ovspend:
    push ax
    xor al, al
    xchg al, [kbd_ovb]          ; read-and-clear in one, kbm_p5spend's idiom
    or al, al                   ; and for its reason
    jz .out
    call snd_beep               ; SPEC.md 34.3, and a refusal by a higher
.out:                           ; priority owner is fine: something louder than
    pop ax                      ; a full keyboard buffer is already sounding
    ret

; -----------------------------------------------------------------------------
; kbm_p5spend - turn a keypad-5 press the ISR saw into the button press
;
; The ISR only ever sets a byte: everything else the button does - the event,
; the latch, the repeat guard - is kbm_btn's, and kbm_btn runs in TASK context
; where every other button key already reaches it. So IRQ1 pushes nothing onto
; the event queue and this needed no new rule about what an ISR may call.
;
; Called from the two places the keyboard mouse is serviced from, which
; between them cover every state the machine can be in: kbm_poll0 (the modal
; loops and a package's own tracking loop) and the end of kbm_ui (an ordinary
; ui_task pass). In kbm_ui it must come AFTER .relax, or the press is released
; by the same pass that made it - 9.6.1's flashing menu from a third direction.
; out: nothing (all registers preserved, flags clobbered)
; -----------------------------------------------------------------------------
kbm_p5spend:
    push ax
    xor al, al
    xchg al, [kbm_p5]           ; read-and-clear in one, so a press arriving
    or al, al                   ; between the test and the store cannot be lost
    jz .out                     ; (fr_restart's idiom, and for its reason)
    call kbm_btn                ; the same one action Ins and Space take
.out:
    pop ax
    ret

kbm_rclick:                     ; latches too - menu_popup ends on a level poll
    push ax                     ; of bit 1 exactly as menu_track does on bit 0
    xor byte [mouse_btn], 0x02
    test byte [mouse_btn], 0x02
    jz .out                     ; a release queues nothing (SPEC.md 9)
    mov ax, EVT_RDOWN
    call kbm_post
.out:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; kbm_poll - service the keyboard from a loop that has taken over the UI task
;
; menu_track, ui_drag and ui_grow spin on a LEVEL poll of mouse_btn and do not
; return to ui_task until it comes up. So a button latched by kbm_hold could
; never be released: the machine would sit inside a menu no key could close.
; This is what those loops call instead, beside the task_yield they already
; make.
;
; The key is PEEKED first, because int 16h has no way to put one back. What
; happens to a key we do NOT claim is the whole of the difference between the
; two entry points, and getting it wrong wedges the machine:
;
;   kbm_pollm   MODAL - eat it. menu_track, ui_drag and ui_grow read no keys
;               of their own, so nothing else is ever going to take it out of
;               the buffer.
;   kbm_poll    PEEK ONLY - leave it. osapi_mouse's callers are packages whose
;               loops poll int 16h themselves (Paint's pt_wait, Missile
;               Command), and eating their keystrokes would be worse than the
;               bug below.
;
; THE BUG, because a peek-only poll in a modal loop is a machine you cannot
; get out of: int 16h AH=01h reports the HEAD of the BIOS buffer. Leave an
; unclaimed key there and every following key queues behind it, invisible -
; so the very button press that would close the menu can never be seen, and
; neither can the next, or ScrollLock, or Escape. ui_task's own poll cannot
; show this because it always removes the key it peeked. It reached the field
; as a Compaq Portable III stuck in a menu with a live machine underneath it
; (BIOS ticks still advancing at 18.3Hz), because keypad 5 is unclaimed there
; - and it reproduces here in three keystrokes: open a menu, press 'a', try
; to close it.
;
; A key typed into a menu or a drag is discarded, which is what modal means.
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
kbm_pollm:
    mov byte [kbm_eat], 1
    jmp short kbm_poll0
kbm_poll:
    mov byte [kbm_eat], 0
kbm_poll0:
    push ax
    mov ah, 0x01
    int 0x16
    jz .p5                      ; nothing waiting
    call kbm_key
    jc .take                    ; ours, and acted on: take it out
    cmp byte [kbm_eat], 0
    je .p5                      ; not ours, and somebody else will read it
.take:
    mov ah, 0x00
    int 0x16
.p5:
    call kbm_p5spend            ; LAST, so int 16h gets first refusal: on a
                                ; BIOS that delivers keypad 5 the call above
                                ; has already claimed it and cleared the flag
                                ; (SPEC.md 9.6.4), and this finds nothing
    call kbd_ovspend            ; ...and a modal loop is exactly where the
                                ; buffer overruns unheard otherwise: ui_task's
                                ; ladder is not running (SPEC.md 9.8)
    pop ax
    ret

; -----------------------------------------------------------------------------
; kbm_ui - the end of a ui_task pass: the ScrollLock edge, then the click latch
;
; Called once from ui_task's deferred ladder, where no lock is held. Two jobs,
; both of which have to happen HERE rather than where the key was seen:
;
;   the raise   ScrollLock is a LEVEL and there is no keystroke to hang this
;               off (see kbm_slock), so the edge is found by remembering it.
;               And wm_front takes the gfx lock, which kbm_poll's callers
;               hold - so even a keystroke could not have done it in place.
;   the release kbm_btn latches the button so that menu_track, ui_drag,
;               ui_grow and a package's tracking loop - none of which return
;               to ui_task until it comes up - can see it held. Whatever
;               consumed the mouse-down has finished by the time this runs, so
;               a press that opened no tracking loop becomes a plain click.
;
; The release is gated on the pass having popped THAT mouse-down, because a
; press arriving while an older event is still queued is dispatched a pass
; later - and releasing the latch in between is the flashing menu again, from
; the other end. An empty pop leaves [ui_ev] stale, which cannot matter: a
; live latch always has its own EVT_MDOWN in the queue or already dispatched.
;
; out: nothing. CLOBBERS EVERYTHING on the raise path - wm_hit and wm_front
;      spend AX/SI/DI/BP/ES - which is the contract every other step of that
;      ladder already has (fdlg_reap, app_launch, loader_run, ui_cmd), and is
;      stated rather than tightened because SPEC.md 7.1.4.1 is what a routine
;      promising more than it keeps costs here.
; -----------------------------------------------------------------------------
kbm_ui:
    cmp byte [mou_seen], 0
    jne .out                    ; a machine with a mouse pays this compare
    xor al, al                  ; AL = the ScrollLock level, 0 or 1. kbm_slock
    call kbm_slock              ; preserves AX and answers in ZF, and the pops
    jz .lvl                     ; inside it do not disturb it
    inc al
.lvl:
    cmp al, [kbm_slast]
    je .relax                   ; unchanged, which is nearly every pass
    mov [kbm_slast], al         ; banked whichever way it went, so the next
    or al, al                   ; pass sees no edge
    jz .relax                   ; the FALLING edge raises nothing - the pointer
                                ; is simply live again
    call gfx_lock
    mov cx, [mouse_x]
    mov dx, [mouse_y]
    call wm_hit                 ; BX = the window under the pointer, 0 = none
    or bx, bx
    jz .unlock
    call wm_front               ; ...which activates it too, so the menu bar
.unlock:                        ; and the keyboard end up on the same window
    call gfx_unlock
.relax:
    cmp word [ui_ev + EV_TYPE], EVT_MDOWN
    jne .p5                     ; .p5 and NOT .out: nothing to release is the
    test byte [mouse_btn], 0x01 ; commonest pass there is, and it is exactly
    jz .p5                      ; the pass a fresh keypad-5 press arrives in
    push ax
    and byte [mouse_btn], 0xFE  ; bit 1 is the right button's own latch, and is
    mov ax, EVT_MUP             ; released by its own key (SPEC.md 12.4)
    call kbm_post
    pop ax
.p5:
    call kbm_p5spend            ; LAST, and after .relax: a press spent above
                                ; the release would be taken back by the very
                                ; pass that made it (SPEC.md 9.6.4)
.out:
    ret

; kbm_post - AX = event type, at the pointer, stamped now
kbm_post:
    push si
    mov si, kbm_ev
    mov [si], ax
    mov ax, [mouse_x]
    mov [si+2], ax
    mov ax, [mouse_y]
    mov [si+4], ax
    mov ax, [ticks]             ; birth time: two of these close together are
    mov [si+6], ax              ; a double-click, exactly as a real one is
    call evq_push
    pop si
    ret

kbm_old9    dd 0                ; the BIOS's int 09h, chained by kbm_isr
                                ; (SPEC.md 9.6.4). .text with a real
                                ; initialiser: -f bin zeroes nothing, and the
                                ; ISR reads this through CS
kbm_p5      db 0                ; ...and a keypad-5 press it saw, not yet spent
kbm_step    db KBM_MIN          ; px the current run of repeats is worth
kbm_lastk   db 0                ; ...and which direction it is a run OF
kbm_last    dw 0                ; tick of the last movement key
kbm_btick   dw 0                ; ...and of the last button key, for the repeat
kbm_slast   db 0                ; the ScrollLock level kbm_ui last saw (9.6.2)
kbm_eat     db 0                ; kbm_pollm: eat a key we did not claim
kbm_ev      times 8 db 0        ; the record kbm_post fills for evq_push
kbd_armed   db 0                ; SPEC.md 9.7: has anything ever ASKED for key
                                ; state? Until it has, kbm_isr does not read
                                ; the port for it. .text with a real
                                ; initialiser for kbm_old9's reason - the ISR
                                ; reads it on the machine's first keystroke,
                                ; and a 1 here would let it write a map
                                ; kbd_down has not cleared yet
kbd_dnmap   times KBD_MAPSZ db 0 ; ...and the bit per key. Zeroed again at the
                                ; arm, so this initialiser is belt and braces
kbd_ovn     db 0                ; SPEC.md 9.8: beeps this overrun burst has
                                ; already spent, reset when the buffer next
                                ; reads empty
kbd_ovb     db 0                ; ...and one the ISR has asked for and no pass
                                ; has sounded yet. Both .text with a real
                                ; initialiser for kbd_armed's reason - the ISR
                                ; reads them on the machine's first keystroke

; -----------------------------------------------------------------------------
; mou_isr / mou_isr3 - IRQ4 and IRQ3. Microsoft
;           protocol: byte 0 has bit 6 set: 1 LB RB Y7 Y6 X7 X6; bytes 1/2
;           have bit 6 clear: low 6 bits of X then Y. Any bit-6 byte restarts
;           the packet (resync). Runs with IF=0 throughout; never sti (SPEC 7).
;
; The two vectors share one body, because WHICH LINE FIRED SAYS NOTHING ABOUT
; WHICH PORT HAS THE BYTE (SPEC.md 9.5.2). Both land on the MASTER 8259, so
; the EOI at the bottom is the same non-specific one it always was.
;
; The body scans every live port for a waiting byte and hands each one to
; mou_byte, where BX is the port row all the way through - because every port
; decodes INDEPENDENTLY: phase, b0, b1 and the run are per-row arrays, not one
; shared set. Sharing them was tried and is wrong in both directions - a
; modem's byte resynced the MOUSE's half-built packet, and a modem's protocol
; violation reset the MOUSE's run, so a chattering modem on the other port
; could starve the real mouse out of ever being found (SPEC.md 9.5).
; -----------------------------------------------------------------------------
mou_isr:                        ; IRQ4, and...
    push bx
    mov bl, MOU_L4              ; ...the ONE thing the two entries disagree
    jmp short mou_isr_body      ; about: which LINE fired. Not which PORT has
mou_isr3:                       ; the byte - that is scanned for below - but
    push bx                     ; which 8259 bit must SURVIVE retirement, and
    mov bl, MOU_L3              ; nothing else can tell mou_lockon that
mou_isr_body:
    push ax
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    mov ax, KERNEL_SEG
    mov ds, ax
    mov [mou_lfired], bl        ; BL survives the pushes above; AX does not, so
    cld                         ; this is the first instruction that can store

    ; --- whoever is holding a byte, take it ---------------------------------
    ; Not "the port this vector belongs to": there is no such thing. A card at
    ; 2F8 jumpered to IRQ4 raises the COM1 vector, and reading COM1's receive
    ; register there finds nothing, leaves the byte at 2F8 sitting in its own,
    ; and leaves that UART asserting its line - after which the line never
    ; makes another EDGE and the mouse is silent for the rest of the session.
    ; Measured on a Compaq Portable III (docs/FIELD-MACHINES.md): a perfect
    ; mouse, 81 packets and no protocol violations to a POLLED reader, and the
    ; kernel never saw one byte of it.
    ;
    ; So every hooked line services every live port. It costs one LSR read per
    ; live port per interrupt - two IN instructions on a two-port machine, at
    ; ~120 interrupts a second - and it makes the base-to-IRQ mapping stop
    ; being an assumption the hardware has to honour.
    xor bx, bx
.scan:
    mov dx, [bx+mou_bases]
    or  dx, dx
    jz .next                    ; the probe rejected this row
    add dx, 5
    in  al, dx                  ; LSR: is anything waiting here?
    test al, 0x01
    jz .next
    sub dx, 5
    in  al, dx                  ; ...and THIS read is what drops the request
    push bx                     ; ...which clobbers BX - the decode spends it
    call mou_byte               ; on the packet's own bytes - and BX is this
    pop bx                      ; loop's index
.next:
    add bx, 2
    cmp bx, MOU_PEND
    jb .scan
    jmp mou_eoi

; -----------------------------------------------------------------------------
; mou_byte - one received byte, from port BX
; in:  AL = the byte, BX = the port row; ISR context, IF = 0
; -----------------------------------------------------------------------------
mou_byte:
    cmp byte [mou_seen], 0      ; is the mouse's port settled?
    je .drain
    cmp bl, [mou_port]          ; then every other port belongs to somebody
    jne .bout                    ; else's device and is dropped unheard. This
                                ; also closes the window between the latch
                                ; below and mou_lockon masking the loser
.drain:
    cmp byte [mou_drain], 0     ; a hot-plug raise just happened (below):
    je .live                    ; this is the identify/PnP burst, and its
    mov dx, [ticks]             ; bit-6 bytes would resync as headers. DX is
    sub dx, [mou_dstamp]        ; free - the base has been read - and BX is
    cmp dx, MOU_DRAINT          ; NOT: it is the port row, and every array
    jb .bout                     ; below is indexed on it
    mov byte [mou_drain], 0     ; window over: this byte is live
.live:
    test al, 0x40               ; bit 6 set -> first byte of a packet
    jz .cont
    cmp byte [bx+mou_phase], 0  ; ...but MID-PACKET it is a resync, and a real
    je .b0                      ; mouse never causes one: its bytes come in
    mov byte [bx+mou_run], 0    ; threes with nothing between them. Break the
.b0:                            ; run of clean packets (SPEC.md 9.5)
    mov [bx+mou_b0], al
    mov byte [bx+mou_phase], 1
    jmp .bout

.cont:
    cmp byte [bx+mou_phase], 1
    jne .chk2
    mov [bx+mou_b1], al         ; second byte: X low 6 bits
    mov byte [bx+mou_phase], 2
    jmp .bout

.chk2:
    cmp byte [bx+mou_phase], 2  ; anything else is a stray byte - drop it, and
    je .pkt                     ; break the run for the same reason: a byte
    mov byte [bx+mou_run], 0    ; with bit 6 clear arriving between packets is
    jmp .bout                    ; a thing the protocol cannot produce
.pkt:
    mov byte [bx+mou_phase], 0  ; third byte: Y low 6 bits, packet complete
    call mou_claim              ; ...and a RUN of them is PROOF OF MOUSE: the
                                ; port is settled and the hot-plug poller
                                ; stands down for the session
    cmp byte [mou_seen], 0      ; still unsettled? then this packet COUNTS and
    je .bout                     ; does nothing else. A device on its way to
                                ; losing the contest must not move the cursor
                                ; or queue a click while it loses - a modem's
                                ; 'no carrier' is lowercase, and bit 5 of a
                                ; lowercase letter is the LEFT BUTTON. The
                                ; packet that SETTLES the port is acted on, so
                                ; a one-port machine loses nothing at all
    mov dh, [bx+mou_b0]         ; stage the packet out of this port's slot: BX
    mov dl, [bx+mou_b1]         ; belongs to the decode from here on
    mov bl, al                  ; BL = Y low bits, decoded below

    ; --- dx = sign-extended {X7X6 from b0, X5..X0 from b1} -------------------
    mov al, dh
    and al, 0x03
    mov cl, 6
    shl al, cl                  ; X7X6 into bits 7:6
    mov ah, dl
    and ah, 0x3F
    or  al, ah
    cbw                         ; AX = dx, signed
    mov si, ax                  ; ...banked, because mou_apply below wants both
                                ; deltas at once. SI is the ISR's to spend - it
                                ; is pushed - and it is the one register the
                                ; y decode does not need

    ; --- dy = sign-extended {Y7Y6 from b0, Y5..Y0 from b2}; positive = down --
    mov al, dh
    and al, 0x0C
    mov cl, 4
    shl al, cl                  ; Y7Y6 into bits 7:6
    and bl, 0x3F
    or  al, bl
    cbw                         ; AX = dy, signed
    mov bx, ax

    ; --- buttons: b0 bit 5 = left, bit 4 = right -----------------------------
    ; Into mouse_btn's OWN two bits here rather than inside mou_apply, because
    ; this is the whole of what THIS protocol has to say about them - and the
    ; PS/2 packet already carries them in that order (SPEC.md 9.9.3), so the
    ; shared back end never has to know which mouse it is serving.
    xor cl, cl
    test dh, 0x20
    jz .nol
    or  cl, 0x01
.nol:
    test dh, 0x10
    jz .nor
    or  cl, 0x02
.nor:
    mov ax, si
    jmp mou_apply               ; ...which ends in the ret this needs
.bout:
    ret

; -----------------------------------------------------------------------------
; mou_apply - one decoded report, whatever hardware carried it (SPEC.md 9.9.3)
;
; From the moment a packet is a signed dx, a signed dy and two button bits,
; NOTHING about it is protocol-specific - so both mice end here: the staging
; through mou_nx, mou_clamp's 2-D answer (SPEC.md 39.15.4), the event rules
; including the binding fall-through that makes a both-buttons packet queue the
; LEFT event only (SPEC.md 9), [blk_act] (SPEC.md 64.1), [cur_mvt] (SPEC.md
; 7.1.4.3) and SPEC.md 7's draw-or-defer decision.
;
; A second copy of this would be a second place for that fall-through to be got
; wrong, and it would be got wrong.
;
; ins:  AX = dx signed, BX = dy signed (POSITIVE IS DOWN - the PS/2 decoder
;       negates its own), CL = the buttons (bit 0 left, bit 1 right).
;       ISR context, IF = 0; the caller has already settled the contest.
; outs: none
; -----------------------------------------------------------------------------
mou_apply:
    mov byte [blk_act], 1       ; a packet on a SETTLED port IS user input
                                ; (SPEC.md 64.1), and only a settled one: both
                                ; callers do the contest before they get here,
                                ; so a modem losing it cannot keep the screen
                                ; awake all night. One store, no register - the
                                ; UI task spends it, so nothing in an ISR
                                ; touches a video port or reads a clock
    add ax, [mouse_x]
    mov [mou_nx], ax            ; STAGED, not stored: the clamp is 2-D on a
                                ; machine with two displays (SPEC.md 39.15.4)
                                ; and cannot answer about x until y is known
    mov ax, bx
    add ax, [mouse_y]
    mov bx, ax
    mov ax, [mou_nx]
    call mou_clamp              ; ...and CX is untouched by it, which the
    mov [mouse_x], ax           ; button decode below relies on
    mov [mouse_y], bx
%ifdef CURFIX
    mov ax, [ticks]             ; THE HAND IS MOVING (SPEC.md 7.1.4.3), which
    mov [cur_mvt], ax           ; is what decides whether the deferred hide is
                                ; worth keeping. AX is dead here - the clamped
                                ; x is already stored - and CL is NOT: it is
                                ; still the buttons. Gated with its reader, so
                                ; a DEFAULT build is the pre-7.1.4.2 ISR too
                                ; and not that one with a dead store in it - an
                                ; A/B disk carrying half of what it is testing
                                ; is not an A/B
%endif

    mov bl, cl                  ; the buttons, in mouse_btn's own two bits
    mov bh, [mouse_btn]
    mov [mouse_btn], bl
    xor bh, bl                  ; changed bits
    test bh, 0x01               ; left button changed?
    jz .rbtn

    mov ax, EVT_MUP             ; queue EVT_MDOWN / EVT_MUP, a=x b=y (SPEC 10)
    test bl, 0x01
    jz .fill
    mov ax, EVT_MDOWN
    jmp short .fill

    ; The right button is tested on the LEFT test's fall-through, and that
    ; ordering is binding (SPEC.md 9): a packet reporting both buttons
    ; changed queues the left event ONLY, so a chord can never steal a left
    ; press from the drag loop or the double-click detector. Only the press
    ; edge is queued - the context menu it opens lives on a level poll of
    ; mouse_btn bit 1 (SPEC.md 12.4), so an EVT_RUP would be a record
    ; nobody ever pops.
.rbtn:
    test bh, 0x02               ; right button changed?
    jz .move
    test bl, 0x02
    jz .move                    ; a right RELEASE queues nothing
    mov ax, EVT_RDOWN

.fill:
    mov si, mou_evrec
    mov [si], ax
    mov ax, [mouse_x]
    mov [si+2], ax
    mov ax, [mouse_y]
    mov [si+4], ax
    mov ax, [ticks]             ; c = birth time: double-click detection must
    mov [si+6], ax              ; not use processing time - clicks queued
                                ; behind a slow disk mount would collapse
                                ; into one instant and read as double-clicks
    call evq_push               ; guards internally with pushf/cli; IF stays 0

.move:
    ; --- cursor: draw only if the lock is free AND cur_level >= 0 (SPEC 7) ---
    cmp byte [gfx_lock_flag], 0
    jne .dirty
    mov al, [cur_level]
    or  al, al
    js .dirty

    mov ax, [mouse_x]           ; hotspot = drawn position; the cell is
    mov bx, [mouse_y]           ; clipped at the screen edges in cur_draw
    cmp ax, [cur_drawn_x]       ; button-only packet: nothing to redraw
    jne .redraw
    cmp bx, [cur_drawn_y]
    je .out
.redraw:
    call cur_move               ; SPEC.md 7.1.2: erase-old and draw-new as one
    mov byte [cur_dirty], 0
    ret

.dirty:
    mov byte [cur_dirty], 1     ; cursor_show catches up later
.out:
    ret

mou_eoi:
    mov al, 0x20                ; EOI - the BIOS handles neither IRQ4 nor
    out 0x20, al                ; IRQ3 for us, and both are on the master
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    pop bx
    iret

; -----------------------------------------------------------------------------
; cursor_show - increment cur_level; on the -1 -> 0 transition, save under and
;               draw the cursor at the latest mouse position (clipped) and
;               clear cur_dirty. Interrupt-safe: the critical section runs
;               under pushf/cli so the ISR never sees a half-drawn state.
; ins:  none
; outs: none (all registers preserved)
; -----------------------------------------------------------------------------
cursor_show:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    pushf
    cli
    inc byte [cur_level]
    jnz .done                   ; only the -1 -> 0 edge draws

    mov ax, [mouse_x]           ; hotspot = drawn position; the cell is
    mov bx, [mouse_y]           ; clipped at the screen edges in cur_draw
    mov [cur_drawn_x], ax
    mov [cur_drawn_y], bx
%ifdef KERN_BIG
    push ax                     ; ...and WHICH display that is (SPEC.md
    call vid_disp_of            ; 39.15.2) - the one place besides cur_move's
    mov [cur_disp], al          ; jump where the arrow can change display
    pop ax
%endif
    mov byte [cur_dirty], 0
    call cur_put
.done:
    popf
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cursor_hide - decrement cur_level; on the 0 -> -1 transition, restore the
;               save-under (the buffer is only valid while the cursor is
;               drawn). Same pushf/cli guard as cursor_show.
; ins:  none
; outs: none (all registers preserved)
; -----------------------------------------------------------------------------
cursor_hide:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    pushf
    cli
    dec byte [cur_level]
    cmp byte [cur_level], -1
    jne .done                   ; only the 0 -> -1 edge erases
    call cur_get
.done:
    popf
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cur_put / cur_get - draw the cursor over what it covers, and put that back
;
; The pair is the whole of what gfx_lock and gfx_unlock cost (SPEC.md 7.1):
; the mutex itself is one flag and six instructions, and everything else in
; those two routines is here. A field log of Missile Command put the pair at
; 21.8% of a session with no pixel of the game in it (PERFORMANCE.md Part 9
; Set 4), which is what this shape is for.
;
; Both dispatch on the adapter, and the two sides are NOT symmetrical:
;
;   1bpp   cur_put_mono / cur_get_mono, below. The framebuffer IS the
;          renderer's target (SPEC.md 39.5), so the byte under the cursor can
;          be read, banked, painted and written back in ONE pass - where the
;          old path made three, through the generic rect machinery, twice.
;   VGA    cur_saveu + cur_draw / cur_restoreu, unchanged in shape: the save
;          is four planes through Read Map Select and the draw is Set/Reset
;          through the Bit Mask, so there is no single byte to fuse.
;
; Internal; the caller guarantees IF = 0 or the gfx lock.
; clobbers: ax, bx, cx, dx, si, di, bp, es
; -----------------------------------------------------------------------------
%ifdef KERN_BIG
; --- the display bracket (SPEC.md 39.15.2) -----------------------------------
; cur_put/cur_get/cur_move own this themselves rather than inheriting it,
; because cur_get is reached from gfx_lock's deferred hide AND from the mouse
; ISR - neither of which is inside a 39.14 drawing hook, so the display live at
; either moment is whatever the last primitive left.
;
; CUR_DBEGIN banks the outgoing display on the stack and makes [cur_disp]
; current; CUR_DEND puts it back, preserving every register and the flags.
%macro CUR_DBEGIN 0
    push ax
    mov al, [vid_cur]
    mov [cur_dprev], al
    mov al, [cur_disp]
    call vid_ctx_act
    pop ax
%endmacro

%macro CUR_DEND 0
    pushf
    push ax
    mov al, [cur_dprev]
    call vid_ctx_act
    pop ax
    popf
%endmacro
%else
%macro CUR_DBEGIN 0
%endmacro
%macro CUR_DEND 0
%endmacro
%endif

cur_put:
    CUR_DBEGIN
    call cur_put_1
    CUR_DEND
    ret

cur_put_1:
    cmp byte [vid_mono], 0
    jne cur_put_mono
    call cur_saveu
    jmp cur_draw

cur_get:
    CUR_DBEGIN
    call cur_get_1
    CUR_DEND
    ret

cur_get_1:
    cmp byte [vid_mono], 0
    jne cur_get_mono
    ; fall through to cur_restoreu

; -----------------------------------------------------------------------------
; cur_restoreu - write cur_save back under (cur_drawn_x, cur_drawn_y).
;                VRAM-direct (SPEC.md 32) - the cursor lives on the screen
;                only, and this runs from the ISR
;                where the bb dispatch must not apply.
; clobbers: ax, bx, cx, dx, si
; -----------------------------------------------------------------------------
cur_restoreu:
    push es
    call cur_rect
    push ds
    pop es
    mov si, cur_save
    call vga_restore_vram
    pop es
    ret

; -----------------------------------------------------------------------------
; cur_saveu - copy the cell under (cur_drawn_x, cur_drawn_y) into cur_save via
;             vga_save_vram. vga_save_vram clips the rect at the screen edges
;             the same way vga_restore_vram does, so partial cells stay
;             coherent - which is the whole reason these two share cur_rect.
; clobbers: ax, bx, cx, dx, di
; -----------------------------------------------------------------------------
cur_saveu:
    push es
    call cur_rect
    push ds
    pop es                      ; buffer lives in kernel .bss
    mov di, cur_save
    call vga_save_vram
    pop es
    ret

; cur_rect - the cell the two above save and restore, as a rect. ONE place,
; because a save and a restore that disagree about it by a byte smear the
; cursor across the screen. CUR_GW/CUR_GH, not CUR_W/CUR_H: the tables are
; 16x16 and the arrow is not.
; out: AX/BX/CX/DX = x1/y1/x2/y2
cur_rect:
    mov ax, [cur_drawn_x]
    mov bx, [cur_drawn_y]
%ifdef KERN_BIG
    sub ax, [vid_ox]            ; ...into the ACTIVE display's own coordinates
    sub bx, [vid_oy]            ; (SPEC.md 39.15.1). Unconditional rather than
%endif                          ; gated on [vid_ndisp]: display 0 is always at
                                ; the virtual origin, so on a one-card machine
                                ; this subtracts zero
    sub ax, [cur_hx]            ; ...and off the HOT SPOT, clamped at the
    jns .xok                    ; display origin rather than clipped there
    xor ax, ax                  ; (SPEC.md 7.2.2)
.xok:
    sub bx, [cur_hy]
    jns .yok
    xor bx, bx
.yok:
    mov cx, ax
    add cx, CUR_GW-1
    mov dx, bx
    add dx, CUR_GH-1
    ret

; -----------------------------------------------------------------------------
; cur_geom - where the cursor cell is, and how much of it is on the screen
;
; out:      DI = framebuffer offset of the cell's top-left byte, CL = x & 7,
;           [cur_rows] = rows on screen (0..CUR_GH), [cur_b1ok] = may the byte
;           at DI+1 be written. The offset is BANKED in [cur_off] as well,
;           because cur_get_mono has to put back exactly the bytes
;           cur_put_mono took and re-deriving them is a second place that can
;           disagree - which, for a save-under, means the cursor smeared
;           across the screen rather than a pixel out of place.
; clobbers: AX, BX, DX, DI, CL
; -----------------------------------------------------------------------------
cur_geom:
    mov ax, [cur_drawn_y]       ; DI = rowbase(y) + x/8
%ifdef KERN_BIG
    sub ax, [vid_oy]            ; cur_rect's subtraction, and for cur_rect's
%endif                          ; reason: these two are the ONLY places a
                                ; position becomes an ADDRESS (SPEC.md 39.15.1)
    sub ax, [cur_hy]            ; ...and cur_rect's hot spot, clamped the same
    jns .yok                    ; way, or a centred shape near the top edge
    xor ax, ax                  ; addresses the row above the framebuffer
.yok:
    mov [cur_celly], ax         ; BANKED: the bottom clip below and
                                ; cur_move_mono's row test both want the
                                ; CELL's top row, which is no longer the
                                ; pointer's y (SPEC.md 7.2.2)
    call gfx_rowbase
    mov di, ax
    mov bx, [cur_drawn_x]
%ifdef KERN_BIG
    sub bx, [vid_ox]
%endif
    sub bx, [cur_hx]
    jns .xok
    xor bx, bx
.xok:
    mov [cur_cellx], bx         ; ...and its left column, for cur_move_mono
    mov ax, bx
    mov cl, 3
    shr ax, cl
    add di, ax                  ; AX = the byte column, for the clip below
    mov cl, bl
    and cl, 7                   ; CL = shift (x & 7)

    ; --- clip the cell at the right and bottom screen edges ------------------
    ; bytes-in-row = stride - column; skip the DI+1 write that would otherwise
    ; wrap onto the next scan line. There is no DI+2: an 8px arrow spans two
    ; bytes however far it is shifted (CUR_SPAN).
    mov dx, [vid_stride]
    sub dx, ax                  ; DX = bytes available in this row (>= 1)
    mov byte [cur_b1ok], 0
    cmp dx, 2
    jb .cols_done
    mov byte [cur_b1ok], 1
.cols_done:
    mov ax, [vid_ch]            ; UNSIGNED - vid_apply homes the cursor into
    sub ax, [cur_celly]         ; the new screen precisely so this cannot
                                ; borrow. rows on screen at this y (>= 1).
                                ; [cur_celly] is already this DISPLAY's and
                                ; already hot-spot-relative, so the two
                                ; corrections that used to be spelled out
                                ; here are spent above, once
    cmp ax, CUR_GH
    jbe .rows_set
    mov ax, CUR_GH
.rows_set:
    mov [cur_rows], al
    mov [cur_off], di
    ret

; -----------------------------------------------------------------------------
; cur_put_mono - save the cell under the cursor and draw the arrow into it, in
;                ONE pass (SPEC.md 7.1)
;
; The three passes this replaces - vga_save_vram's row loop, then cur_pass_mono
; for the white outline, then cur_pass_mono again for the black body - read the
; same framebuffer byte three times and wrote it twice, each time through its
; own row walk, its own gfx_nextrow call and, for the save, the whole of
; vga_rect_setup's generic clipping. They are the same bytes in the same order,
; and the two masks compose: (under | white) & ~black is what drawing one pass
; on top of the other always produced. So the byte is read once, banked once,
; painted once and written once.
;
; The row advance is gfx_nextrow INLINED, which is PERFORMANCE.md Part 9 Set
; 3's rule: at two framebuffer bytes a row, a near call and a ret are most of
; the loop.
;
; in:       cur_drawn_x/y; IF = 0 or the gfx lock held
; clobbers: ax, bx, cx, dx, si, di, bp, es
; -----------------------------------------------------------------------------
cur_put_mono:
    call cur_geom
    mov ch, [cur_rows]
    or ch, ch
    jz .out                     ; wholly off the bottom: nothing to bank
    mov ax, [vid_seg]
    mov es, ax
    mov si, [cur_tabp]
    mov bp, [cur_sptr]          ; the live save buffer (SPEC.md 7.1.2)
.row:
    mov ax, [si]                ; the white pass's row...
    mov dx, [si+CUR_TABSZ]      ; ...and the black one's, one table along
    add si, 2
    shr ax, cl                  ; AH:AL = white bits, byte 0 : byte 1
    shr dx, cl                  ; DH:DL = black bits, likewise...
    not dx                      ; ...as an AND mask, so the body clears to 0

    mov bl, [es:di]             ; ONE read of the byte under the arrow...
    mov [ds:bp], bl             ; ...banked (SS is LOW_SEG, so ds: is not
                                ; decoration - SPEC.md 2.1)
    or  bl, ah                  ; ...white outline...
    and bl, dh                  ; ...black body over it...
    mov [es:di], bl             ; ...and ONE write

    cmp byte [cur_b1ok], 0      ; the second byte, unless the screen edge is
    je .adv                     ; between them
    mov bl, [es:di+1]
    mov [ds:bp+1], bl
    or  bl, al
    and bl, dl
    mov [es:di+1], bl
.adv:
    add bp, CUR_SPAN            ; RESERVED whether or not byte 1 was taken, so
                                ; cur_get_mono's walk needs no clip of its own
    add di, [cs:vid_rowadd]     ; gfx_nextrow, inlined (SPEC.md 39.3)
    test di, [cs:vid_wrapbit]
    jz .nowrap
    add di, [cs:vid_wrapfix]
.nowrap:
    dec ch
    jnz .row
.out:
    ret

; -----------------------------------------------------------------------------
; cur_get_mono - put cur_put_mono's bytes back, replaying its geometry
;
; [cur_off], [cur_shift], [cur_rows] and [cur_b1ok] are not recomputed: the
; cursor cannot have moved between the two, because every caller that moves it
; (the ISR's .redraw path) erases at the OLD position before it writes the new
; one. Recomputing would be a second answer to a question that already has one.
; clobbers: ax, cx, si, di, es
; -----------------------------------------------------------------------------
cur_get_mono:
    mov ch, [cur_rows]
    or ch, ch
    jz .out
    mov ax, [vid_seg]
    mov es, ax
    mov di, [cur_off]
    mov si, [cur_sptr]
.row:
    lodsb                       ; both bytes are always CONSUMED - the stride
    mov [es:di], al             ; is CUR_SPAN whatever the clip said - and only
    lodsb                       ; the second one is conditionally written
    cmp byte [cur_b1ok], 0
    je .adv
    mov [es:di+1], al
.adv:
    add di, [cs:vid_rowadd]     ; gfx_nextrow, inlined
    test di, [cs:vid_wrapbit]
    jz .nowrap
    add di, [cs:vid_wrapfix]
.nowrap:
    dec ch
    jnz .row
.out:
    ret


; -----------------------------------------------------------------------------
; The DEFERRED hide (SPEC.md 7.1.4)
;
; gfx_lock used to erase the cursor unconditionally, so the pointer was absent
; for the whole of every lock hold - and a hold that repaints a window takes a
; visible amount of time on a 4.77MHz machine. With the mouse sitting still
; that reads as the cursor blinking off and on, once per refresh, forever.
;
; So gfx_lock only PROMISES the hide, in [cur_lazy], and something has to spend
; it before any pixel is written where the cursor is. Two things do:
;
;   cur_unlazy   take it now, unconditionally. Every path that can draw
;                ANYWHERE calls this: the unclipped branch of GFXCLIP, the
;                primitives SPEC.md 11.3 keeps off the clip list, the flush,
;                the save-under, a mode change and the fsx bracket.
;   cur_lazyck   take it only if the cursor could be reached. wm_clip_set
;                calls this, because once a region is armed the clipped
;                primitives are CONFINED to it - so a cursor outside the
;                window's frame is provably safe and the hide is not owed.
;
; The default is therefore "hide", and the win is the exception: a background
; painter that arms a region away from the pointer. That is the twelve
; wm_clip_set callers - the Task Manager's refresh, the menu bar clock,
; Fractal, Bounce, every package worker - and they get it without asking.
;
; [cur_lazy] is meaningful ONLY inside a lock hold: gfx_lock sets it, gfx_unlock
; clears it, and cursor_show is owed exactly when it was spent.
; -----------------------------------------------------------------------------
cur_unlazy:
    cmp byte [cur_lazy], 0      ; one-way within a hold, so the common case
    je .out                     ; after the first call is three instructions
    mov byte [cur_lazy], 0
    call cursor_hide
.out:
    ret

; cur_lazyck - spend the deferred hide unless the ARMED REGION provably cannot
;              reach the cursor.
;
; THE TEST IS THE REGION, NOT THE WINDOW'S FRAME, and that distinction is the
; whole of SPEC.md 7.1.4.2. The frame is the cheaper question and it answers a
; different one: the region is the content rect minus every window ON TOP,
; so a pointer resting on the window in FRONT of the painter is outside every
; fragment while sitting squarely inside the frame. The frame test called that
; reachable and spent the hide - so a Task Manager refreshing twice a second
; behind a Control Panel blinked the arrow forever, which is 7.1.4's own
; reported defect seen one window along.
;
; The walk is not the sixteen rect tests the frame test was chosen to avoid,
; either. A background painter with nothing on top of it - the ordinary case,
; and the one the frame test was measured on - arms exactly ONE fragment, so
; this is four compares against that rect's four words where the frame test
; was four compares against a rect wm_win_rect had to build first. It costs
; more only when the region is genuinely fragmented, which is exactly when
; some of those fragments are somebody else's window and the answer is worth
; having.
;
; in:  the region is armed in wm_clip_tab / [wm_clip_n]
; out: nothing (all registers preserved)
cur_lazyck:
    cmp byte [cur_lazy], 0
    je .out
%ifdef CURFIX
    push ax                     ; IS THE HAND MOVING? (SPEC.md 7.1.4.3) If it
    mov ax, [ticks]             ; is, keeping the arrow lit is the WRONG trade:
    sub ax, [cur_mvt]           ; the ISR cannot move it under the lock either
    cmp ax, CUR_MVGRACE         ; way, so lit means the pointer sits still
    pop ax                      ; while the hand moves and then teleports -
    jbe cur_unlazy              ; which is what the region test was reported
                                ; as. Hidden, the same freeze is a blink the
                                ; eye does not track. Modular subtraction, so
                                ; the wrap is a 2-tick false "moving" per hour
                                ; and its cost is one redundant hide. `pop`
                                ; leaves the flags the `cmp` set
%endif
%ifndef CURFIX
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, [wm_clipwin]
    call wm_win_rect            ; AX/BX/CX/DX = the frame, drop shadow included
    mov si, [cur_drawn_x]       ; separated on x?  cell is [x, x+CUR_GW-1]
    sub si, [cur_hx]
    cmp si, cx
    jg .clear                   ; ...wholly right of the frame
    add si, CUR_GW-1
    cmp si, ax
    jl .clear                   ; ...wholly left of it
    mov si, [cur_drawn_y]       ; ...and the same on y
    sub si, [cur_hy]
    cmp si, dx
    jg .clear
    add si, CUR_GH-1
    cmp si, bx
    jl .clear
    pop si                      ; they overlap: the hide is owed after all
    pop dx
    pop cx
    pop bx
    pop ax
    jmp short cur_unlazy
.clear:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
%else
    cmp word [wm_clip_n], 0
    je cur_unlazy               ; NOTHING ARMED BOUNDS NOTHING, so the hide is
                                ; owed - the safe direction, and the same
                                ; answer every unarmed painter already gets
                                ; from its own cur_unlazy. wm_clip_set only
                                ; reaches here with a region built, so this is
                                ; the contract stated rather than a live path
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [cur_drawn_x]       ; the cell, as x1/y1/x2/y2 in AX/BX/CX/DX
    sub ax, [cur_hx]            ; ...off the HOT SPOT, and deliberately NOT
                                ; clamped (SPEC.md 7.2.2): unclamped is a
                                ; superset of where the cell really is, so the
                                ; error can only ever be a redundant hide.
                                ; Forgetting it entirely is the other
                                ; direction and does not show up for weeks -
                                ; a painter told the cursor was out of reach
                                ; draws through the part of the cell that
                                ; reaches further left than this test does,
                                ; and 7.1.4's smear is permanent
    mov bx, [cur_drawn_y]
    sub bx, [cur_hy]
    mov cx, ax
    add cx, CUR_GW-1
    mov dx, bx
    add dx, CUR_GH-1
    mov si, wm_clip_tab         ; VIRTUAL rects (SPEC.md 39.14.9), and so is
    mov di, [wm_clip_n]         ; [cur_drawn_x] - the display subtraction
                                ; happens where a position becomes an ADDRESS
                                ; (cur_rect, cur_geom) and nowhere else, so
                                ; the two are already in one space
.frag:                          ; OVERLAP, not containment: wm_clip_test asks
    cmp ax, [si+WCR_X2]         ; whether a whole shape fits one fragment and
    jg .next                    ; this asks whether any pixel of the cell
    cmp cx, [si+WCR_X1]         ; could be written at all
    jl .next
    cmp bx, [si+WCR_Y2]
    jg .next
    cmp dx, [si+WCR_Y1]
    jge .hit
.next:
    add si, WCR_SZ
    dec di
    jnz .frag
    pop di                      ; outside every fragment: the region cannot
    pop si                      ; reach the arrow, so it stays on screen
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.hit:
    pop di                      ; a fragment covers part of the cell: owed
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    jmp short cur_unlazy
%endif
.out:
    ret

; cur_lazyend - gfx_unlock, when the hide was never spent. The cursor is still
;               on screen and cursor_show must NOT run (it would bank the
;               drawn arrow as background), but the ISR refused to move it all
;               through the hold, so any motion is owed here.
;
; It saves EVERYTHING, like cursor_show and cursor_hide either side of it, and
; for the same reason (SPEC.md 7.1.4.1): it is reached from gfx_unlock, whose
; contract is "clobbers: flags" and whose callers hold long-lived state in
; registers across the call - a background task keeps its instance record in BP
; and its state block in SI for the LIFE of the task (SPEC.md 14). cur_move
; clobbers ax, bx, cx, dx, si, di, bp and es, and this pushed only the first
; two, so one mouse packet landing inside a hold whose promise SURVIVED - the
; pointer parked away from the window, which is the case the promise exists to
; serve - silently destroyed the caller's world. Bounce read [ds:bp+I_WIN] out
; of a wrecked BP, got a window record whose visible bit was clear, and took
; the blind branch for the rest of the session: the ball stepped invisibly
; while the last-drawn square sat on screen forever, and the Timer stopped
; counting the same way. Nothing faulted, nothing froze, and the task table
; showed both tasks sleeping and waking on schedule - which is what made it
; read as "a worker is broken" rather than as a register bug.
cur_lazyend:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    pushf
    cli
    mov byte [cur_lazy], 0
    cmp byte [cur_level], 0
    jl .out                     ; hidden by refcount: nothing is on screen
    mov ax, [mouse_x]
    mov bx, [mouse_y]
    cmp ax, [cur_drawn_x]
    jne .move
    cmp bx, [cur_drawn_y]
    je .out
.move:
    call cur_move
    mov byte [cur_dirty], 0
.out:
    popf
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; The SHAPE (SPEC.md 7.2)
;
; A window names one of CUR_NSHAPE built-in pictures and the pointer wears it
; over that window and nowhere else. Three things make it as cheap as it is:
;
;   - every shape shares the 8x12 cell, so nothing in the draw loops changed
;     but `mov si, cur_and` becoming `mov si, [cur_tabp]` in three places;
;   - the shape lives in the SPARE HIGH BYTE of W_FLAGS, so no window record
;     grew, no template changed, and wm_create/wm_destroy already zero it -
;     which is the arrow, so the default falls out rather than being arranged;
;   - the question "which window is the pointer over" is asked on the UI TASK,
;     once per pass and only when something moved, never in the ISR.
;
; That last one is not just a cost argument. The UI task is the ONLY mutator
; of wm_zord and the window rects, so wm_hit answers from it without the gfx
; lock and without a race - which is exactly what the ISR could not do.
; -----------------------------------------------------------------------------

; cur_shape_set - wear shape AL from now on.
; in:  AL = CUR_*SH; the gfx lock held by the caller
; out: nothing (all registers preserved)
;
; The picture may only change while the cursor is OFF THE GLASS, and
; cur_unlazy is the whole of what that costs. gfx_lock merely PROMISES the
; hide (SPEC.md 7.1.4); a promise that survives would leave the old picture on
; screen with [cur_shape] naming the new one, and the move inside cur_lazyend
; would then take pass 1's background from one shape's save buffer and draw
; pass 2 from another's. Spending it here also hands cur_move_mono its
; standing precondition - within one move the shape, and so the hot spot, is
; fixed - and needs no hide/show pair of its own: gfx_unlock already owes a
; cursor_show for the hide this just took, and that show draws the new shape.
cur_shape_set:
    cmp al, [cur_shape]
    je .out                     ; the common answer, and the reason the UI
                                ; pass may call this every time it looks
    cmp al, CUR_NSHAPE
    jae .out                    ; a shape nobody ships: keep the one we have
                                ; rather than index off the end of the table
    push ax
    push bx
    call cur_unlazy
    mov [cur_shape], al
    mov bl, al                  ; BX = shape * 2, which indexes BOTH tables -
    xor bh, bh                  ; cur_shtab is a word a shape and cur_shhot is
    add bx, bx                  ; two bytes a shape
    mov ax, [bx + cur_shtab]
    mov [cur_tabp], ax
    mov al, [bx + cur_shhot]
    xor ah, ah
    mov [cur_hx], ax
    mov al, [bx + cur_shhot + 1]
    xor ah, ah
    mov [cur_hy], ax
    pop bx
    pop ax
.out:
    ret

; cur_shape_pass - the UI task's end-of-pass service: which window is the
;                  pointer over, and does it want a different picture?
; in:  nothing; NO lock held (this is a .yield-tail caller)
; out: nothing (all registers preserved)
;
; One compare per pass when nothing moved, which is toast_pass's discipline
; and for toast_pass's reason: this runs as fast as the scheduler lets ui_task
; go round, and wm_hit is a z-order walk with a multiply per candidate.
; [cur_shchk] covers the case the position compare cannot see - the pointer
; standing still while the window under it is raised, hidden, closed or
; dragged out from under it.
cur_shape_pass:
    push ax
    push bx
    push cx
    push dx
    mov ax, [mouse_x]
    mov bx, [mouse_y]
    cmp byte [cur_shchk], 0
    jne .look
    cmp ax, [cur_shx]
    jne .look
    cmp bx, [cur_shy]
    je .out
.look:
    mov byte [cur_shchk], 0
    mov [cur_shx], ax
    mov [cur_shy], bx
    mov cx, ax
    mov dx, bx
    call wm_hit                 ; BX = the topmost visible window over the
                                ; point (0 = none), AL = which part of it
    mov ah, CUR_ARROWSH
    or bx, bx
    jz .set                     ; the desktop, the dock and the menu bar are
                                ; the kernel's, and the kernel points with an
                                ; arrow
    or al, al
    jnz .set                    ; the title bar and its three boxes are the
                                ; kernel's too, drawn by the kernel and
                                ; clicked through the kernel - only a window's
                                ; CONTENT is the application's to dress
    mov ah, [bx + W_FLAGS + 1]  ; the high byte (SPEC.md 7.2.1), whose TOP bit
    and ah, ~((WF_STALE | WF_NOANIM) >> 8) & 0xFF ; TWO bits are the kernel's
                                ; - WF_STALE and WF_NOANIM - and neither is a
                                ; shape (SPEC.md 11.99.2)
.set:
    cmp ah, [cur_shape]
    je .out                     ; THE COMMON ANSWER, and the compare has to be
                                ; HERE rather than only inside cur_shape_set:
                                ; a pointer moving INSIDE the window it was
                                ; already over would otherwise pay a
                                ; gfx_lock/gfx_unlock pair per move for a
                                ; shape that did not change - which is
                                ; PERFORMANCE.md Part 9 Set 4's 21.8%, the
                                ; single most expensive thing in this kernel,
                                ; charged to every mouse move on every
                                ; machine. It also leaves a window in which
                                ; the arrow is off the glass, which is how it
                                ; was caught: the no-regression A/B against a
                                ; reference kernel found the pointer MISSING
                                ; from 5 of 18 captured frames
    mov al, ah
    call gfx_lock               ; cur_shape_set draws, so it needs the lock -
    call cur_shape_set          ; and this is a .yield caller, which holds
    call gfx_unlock             ; none
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cur_move - move the cursor to (AX, BX), writing every byte EXACTLY ONCE
;
; SPEC.md 7.1.2. A move used to be `cur_get` then `cur_put`: two walks, and
; every framebuffer byte where the old and new cells overlap was written twice
; - once with the background, once with the arrow. The glass can catch the
; value in between, and on a real Hercules that reads as the whole arrow
; washing out to white while the mouse moves (docs/FIELD-NOTES.md 6). It is
; ~6.5% of every frame, on every mouse packet.
;
; The fix is not a union walk and does NOT need a gate on how far the cursor
; moved: the two passes still walk the OLD cell and the NEW cell exactly as
; before, and each byte is written once because
;
;   pass 1 SKIPS the bytes pass 2 is going to write, and
;   pass 2 takes their background from the SAVE BUFFER rather than the screen.
;
; Cells that do not overlap degenerate to the old behaviour on their own - the
; skip never fires and the background always comes from the screen - so there
; is nothing to bound and nothing to get wrong at the extremes.
;
; Which bytes those are is decided ONCE, before either walk, from the byte-
; column difference d = newcol - oldcol (mv_p1s0/1 and mv_p2s0/1), because it
; is the same answer for every row.
;
; in:       AX = new x, BX = new y; [cur_drawn_x/y] and the banked geometry
;           still describe where the cursor IS. IF = 0 (ISR, or the gfx lock).
; clobbers: ax, bx, cx, dx, si, di, bp, es
; -----------------------------------------------------------------------------
%ifdef KERN_BIG
; -----------------------------------------------------------------------------
; cur_move_multi - the same move, on a machine with more than one display
;
; in:       AX = new x, BX = new y, VIRTUAL; [cur_disp] and [cur_drawn_x/y]
;           still describe where the arrow IS. IF = 0.
; out:      as cur_move
; clobbers: as cur_move
;
; SPEC.md 39.15.3. cur_move_mono writes every framebuffer byte exactly once by
; walking the old cell and the new one together and taking the overlap's
; background from the save buffer (7.1.2) - and a cell straddling the seam
; would span two cards with two strides and two segments, and would need that
; loop rewritten. So it does not straddle: the arrow leaves the old display
; WHOLE and arrives on the new one WHOLE, one cell in flight at a time.
;
; Same display, which is nearly every packet, is the fused pass unchanged -
; made current first, because the display live when the ISR fires is whatever
; the last primitive left (39.15.2).
; -----------------------------------------------------------------------------
cur_move_multi:
    push cx
    push dx
    mov dl, [vid_cur]           ; the display to come back to...
    mov dh, [cur_disp]          ; ...and the one the arrow is on
    mov [cur_dprev], dl
    push ax
    call vid_disp_of            ; AL = the display the NEW position is on
    mov cl, al
    pop ax
    cmp cl, dh
    jne .jump

    push ax                     ; --- same display: the fused pass ----------
    mov al, dh
    call vid_ctx_act
    pop ax
    call cur_move_1
    jmp short .out

.jump:                          ; --- erase whole, then draw whole ----------
    push ax
    push bx
    push cx
    mov al, dh
    call vid_ctx_act
    call cur_get_1              ; the arrow leaves the display it was on...
    pop cx
    pop bx
    pop ax
    mov [cur_disp], cl
    mov [cur_drawn_x], ax
    mov [cur_drawn_y], bx
    mov al, cl
    call vid_ctx_act
    call cur_put_1              ; ...and arrives on the one it crossed onto
.out:
    mov al, [cur_dprev]
    call vid_ctx_act
    pop dx
    pop cx
    ret
%endif

cur_move:
%ifdef KERN_BIG
    cmp byte [vid_ndisp], 1
    ja cur_move_multi           ; SPEC.md 39.15.3: it does not straddle
%endif
cur_move_1:
    cmp byte [vid_mono], 0
    jne cur_move_mono
    push ax                     ; planar: the save is four planes through Read
    push bx                     ; Map Select and cannot take a background from
    call cur_restoreu           ; a buffer, so it stays erase-then-draw
    pop bx
    pop ax
    mov [cur_drawn_x], ax
    mov [cur_drawn_y], bx
    call cur_saveu
    jmp cur_draw

cur_move_mono:
    push ax
    push bx
    ; --- bank the OLD geometry, which cur_geom is about to overwrite ---------
    ; [cur_cellx]/[cur_celly] and NOT [cur_drawn_x/y]: with a hot spot the two
    ; differ, and everything below is about where the CELL is (SPEC.md 7.2.2).
    ; A shape may only change while the cursor is hidden, so both cells here
    ; are the same shape's and the row test stays an honest subtraction.
    mov ax, [cur_celly]
    mov [mv_oy], ax
    mov ax, [cur_cellx]
    mov cl, 3
    shr ax, cl
    mov [mv_d], ax              ; the OLD byte column, banked in the word that
                                ; will hold the DELTA: cur_geom clobbers DX
                                ; and the new column is not known until it has
                                ; run, so the subtraction moved below it
    mov al, [cur_rows]
    mov [mv_orows], al
    mov al, [cur_b1ok]
    mov [mv_ob1ok], al
    mov ax, [cur_off]
    mov [mv_ooff], ax
    mov ax, [cur_sptr]          ; the buffer holding the OLD background...
    mov [mv_osave], ax
    mov bx, cur_save            ; ...and the other one takes the new, so pass 2
    cmp ax, bx                  ; can read one while it fills the other
    jne .flip
    mov bx, cur_savex
.flip:
    mov [mv_nsave], bx
    mov [cur_sptr], bx
    pop bx
    pop ax
    mov [cur_drawn_x], ax
    mov [cur_drawn_y], bx
    call cur_geom               ; DI = new offset, CL = shift, [cur_rows],
                                ; [cur_b1ok], [cur_off], [cur_cellx/y] - all
                                ; NEW now
    mov ax, [cur_cellx]         ; d = newcol - oldcol, and the shifts are
    shr ax, 1                   ; single-bit because CL is cur_geom's answer
    shr ax, 1                   ; and the pass-2 loop below still wants it -
    shr ax, 1                   ; `mov cl, 3` here would eat the shift
    sub ax, [mv_d]
    mov [mv_d], ax
    call cur_mvcols             ; ...and the four per-column answers

    ; --- pass 1: the old background back, except where pass 2 will draw ------
    mov ch, [mv_orows]
    or ch, ch
    jz .p2
    mov ax, [vid_seg]
    mov es, ax
    mov di, [mv_ooff]
    mov si, [mv_osave]
    mov bx, [mv_oy]
.p1row:
    mov ax, bx                  ; is this row one pass 2 will draw into?
    sub ax, [cur_celly]         ; the NEW cell's top row, which cur_geom has
                                ; just published - not the pointer's y
    mov dl, [cur_rows]
    xor dh, dh
    cmp ax, dx
    mov dl, 0                   ; DL = 1 = "pass 2 covers this row"
    jae .p1w                    ; unsigned: a row above the new cell is huge
    mov dl, 1
.p1w:
    lodsw                       ; AL/AH = the two saved bytes; the stride is
                                ; CUR_SPAN whatever the clip said
    or dl, dl
    jnz .p1s0                   ; row covered: consult the column answers
    mov [es:di], al
    cmp byte [mv_ob1ok], 0
    je .p1next
    mov [es:di+1], ah
    jmp short .p1next
.p1s0:
    cmp byte [mv_p1s0], 0
    jne .p1c1                   ; pass 2 writes this column: leave it alone
    mov [es:di], al
.p1c1:
    cmp byte [mv_ob1ok], 0
    je .p1next
    cmp byte [mv_p1s1], 0
    jne .p1next
    mov [es:di+1], ah
.p1next:
    inc bx
    add di, [cs:vid_rowadd]     ; gfx_nextrow, inlined (SPEC.md 39.3)
    test di, [cs:vid_wrapbit]
    jz .p1nw
    add di, [cs:vid_wrapfix]
.p1nw:
    dec ch
    jnz .p1row

    ; --- pass 2: save and draw the new cell, background from either place ----
.p2:
    mov ch, [cur_rows]
    or ch, ch
    jz .out
    mov ax, [vid_seg]
    mov es, ax
    mov di, [cur_off]
    mov si, [cur_tabp]
    mov bp, [mv_nsave]
.p2row:
    call cur_mvrow              ; [mv_orp] = this row's slot in the OLD save,
                                ; or 0 if the old cell does not reach it
    mov ax, [si]                ; the white row...
    mov dx, [si+CUR_TABSZ]      ; ...and the black one, one table along
    add si, 2
    shr ax, cl
    shr dx, cl
    not dx                      ; DH:DL = the black body as an AND mask

    push ax                     ; --- byte 0 ---
    mov al, [mv_p2s0]
    call cur_mvbg               ; BL = the background, from the buffer or the
    pop ax                      ; screen, whichever this byte's answer says
    mov [ds:bp], bl
    or  bl, ah
    and bl, dh
    mov [es:di], bl

    cmp byte [cur_b1ok], 0      ; --- byte 1 ---
    je .p2adv
    push ax
    mov al, [mv_p2s1]
    inc di
    call cur_mvbg
    dec di
    pop ax
    mov [ds:bp+1], bl
    or  bl, al
    and bl, dl
    mov [es:di+1], bl
.p2adv:
    add bp, CUR_SPAN
    inc word [mv_prow]
    add di, [cs:vid_rowadd]
    test di, [cs:vid_wrapbit]
    jz .p2nw
    add di, [cs:vid_wrapfix]
.p2nw:
    dec ch
    jnz .p2row
.out:
    ret

; cur_mvcols - the four per-column answers, from d = newcol - oldcol
;
; mv_p1s0/1: pass 1 must SKIP old column 0/1, because pass 2 writes it.
;            Old column j is a new column when j-d is 0 or 1, and column 1 of
;            the new cell only exists if the new cell's own clip kept it.
; mv_p2s0/1: pass 2 takes new column 0/1's background from the old save at
;            this offset - d+j, when that is 0 or 1 and the OLD clip kept it -
;            or 0FFh, meaning "read the screen".
; clobbers: ax, bx, flags
cur_mvcols:
    xor bx, bx                  ; --- pass 2, column 0 and 1 -----------------
.p2:
    mov ax, [mv_d]
    add ax, bx                  ; AX = d + j
    mov byte [bx + mv_p2s0], 0xFF
    or ax, ax
    jz .p2take                  ; offset 0: always saved
    cmp ax, 1
    jne .p2next
    cmp byte [mv_ob1ok], 0      ; offset 1 only exists if the OLD clip kept it
    je .p2next
.p2take:
    mov [bx + mv_p2s0], al
.p2next:
    inc bx
    cmp bx, 2
    jb .p2
    xor bx, bx                  ; --- pass 1, column 0 and 1 -----------------
.p1:
    mov ax, bx
    sub ax, [mv_d]              ; AX = j - d
    mov byte [bx + mv_p1s0], 0
    or ax, ax
    jz .p1skip                  ; new column 0: pass 2 always writes it
    cmp ax, 1
    jne .p1next
    cmp byte [cur_b1ok], 0      ; new column 1 only if the NEW clip kept it
    je .p1next
.p1skip:
    mov byte [bx + mv_p1s0], 1
.p1next:
    inc bx
    cmp bx, 2
    jb .p1
    mov word [mv_prow], 0
    ret

; cur_mvrow - does the OLD cell reach the new cell's row [mv_prow]?
; out: [mv_orp] = a pointer into the old save buffer, or 0
; clobbers: ax, bx, flags
cur_mvrow:
    push dx
    mov ax, [cur_celly]         ; the NEW cell's top row (SPEC.md 7.2.2), not
    add ax, [mv_prow]           ; the pointer's y - [mv_oy] is the OLD cell's
    sub ax, [mv_oy]             ; AX = the row's index in the OLD cell
    mov word [mv_orp], 0
    mov bl, [mv_orows]
    xor bh, bh
    cmp ax, bx
    jae .out                    ; unsigned: a row above it is a huge number
    add ax, ax                  ; CUR_SPAN bytes a row
    add ax, [mv_osave]
    mov [mv_orp], ax
.out:
    pop dx
    ret

; cur_mvbg - one byte's background: the old save buffer if this column's
;            answer names an offset AND the old cell reaches this row,
;            otherwise the screen.
; in:  AL = the column answer (0, 1 or 0FFh), ES:DI = the screen byte
; out: BL = the background byte
; clobbers: ax, bx, flags
cur_mvbg:
    cmp al, 0xFF
    je .screen
    mov bx, [mv_orp]
    or bx, bx
    jz .screen
    xor ah, ah
    add bx, ax
    mov bl, [bx]
    ret
.screen:
    mov bl, [es:di]
    ret

; -----------------------------------------------------------------------------
; cur_draw - paint the arrow at (cur_drawn_x, cur_drawn_y) on a PLANAR screen:
;            white pass with the cur_and outline mask, black pass with the
;            cur_data body. Set/Reset supplies the color; the Bit Mask register
;            carries the row bits, shifted across the cell's two VRAM bytes; a
;            read of each target byte loads the latches before the masked
;            write. Leaves the GC in the default state (SPEC 1 rule 7).
;            Internal; caller guarantees IF=0 and default GC state on entry.
; clobbers: ax, bx, cx, dx, si, di, bp, es
; -----------------------------------------------------------------------------
cur_draw:
    call cur_geom               ; DI = the top-left byte, CL = shift, and
    mov ch, [cur_rows]          ; [cur_rows]/[cur_b1ok] = how much is on screen
    or ch, ch
    jz .done                    ; wholly off the bottom
    mov ax, [vid_seg]
    mov es, ax
    mov si, [cur_tabp]
    mov dx, VGA_GC              ; the INDEX port: index and data go as one word
.row:
    mov ax, [si]                ; AX = white: every pixel the arrow touches...
    mov bx, [si+CUR_TABSZ]      ; ...and BX = the black body inside it
    add si, 2
    not bx
    and bx, ax                  ; BX = white & ~black = the pixels that go WHITE
    shr ax, cl                  ; AX = the Bit Mask, shifted into place
    shr bx, cl                  ; BX = the plane data, likewise
    mov bp, ax                  ; bank it: AX is the port register

    or ah, ah                   ; --- byte 0 ---
    jz .b1
    mov al, 8                   ; GC8 = Bit Mask = this byte's arrow bits
    out dx, ax
    mov al, [es:di]             ; the READ is the latch load; its value is
    mov al, bh                  ; discarded, and the byte written in its place
    mov [es:di], al             ; carries BOTH colours at once
.b1:
    mov ax, bp
    or al, al                   ; --- byte 1 ---
    jz .next
    cmp byte [cur_b1ok], 0      ; clipped off the right screen edge?
    je .next
    mov ah, al
    mov al, 8
    out dx, ax
    mov al, [es:di+1]           ; latch load, as above
    mov al, bl
    mov [es:di+1], al
.next:
    add di, ROW_BYTES
    dec ch
    jnz .row
    mov ax, 0xFF08              ; GC8 back to all bits (SPEC.md 1 rule 7)
    out dx, ax
.done:
    ret

; -----------------------------------------------------------------------------
; cursor bitmap - classic Mac arrow, hot spot (0,0), black body 11 rows tall
; with a 1px white outline. cur_and is the white mask (outline + fill),
; cur_data the black body inside it. MSB = leftmost pixel.
;
; ONE list, expanded TWICE. Both tables come out of CUR_ARROW, which carries
; each row's white and black words side by side and emits whichever the pass
; wants - because the invariant the drawing code now depends on is a statement
; about the PAIR, and a pair split across two tables thirty lines apart is a
; pair nobody checks. Three facts are accumulated and asserted below: the
; widest bit set anywhere (CUR_GW), the last row that sets one (CUR_GH), and
; that every black bit is also a white one. All three fail silently otherwise
; - a wider arrow loses its right-hand column on the two mono adapters, a
; taller one leaves its bottom rows on screen for the rest of the session, and
; an escaped black bit simply does not get drawn on VGA - and none of them
; shows up anywhere but on the glass.
;
; cur_bits and cur_hi are accumulated from the WHITE rows alone, which the
; subset assertion is what licenses: a black bit can neither be wider nor
; lower than the white bit that must accompany it.
; -----------------------------------------------------------------------------
%assign cur_bits 0              ; every bit any WHITE row sets, ORed together
%assign cur_hi   0              ; ...and one past the last one that sets any
%macro CUR_ROW 2                ; white row, black row
  %if cur_tab == 0
    dw %1
    %assign cur_bits cur_bits | (%1)
    %if (%1) != 0
      %if cur_row_i + 1 > cur_hi
        %assign cur_hi cur_row_i + 1
      %endif
    %endif
    ; ...and the third fact, which only the PLANAR draw needs: every black bit
    ; must also be a white one. cur_draw writes both colours in ONE masked
    ; store now - Bit Mask = the white row, plane data = white & ~black - so a
    ; black pixel outside its white row would be masked off and simply not
    ; drawn, where the old two-pass draw painted it in a pass of its own.
    ; It is checked HERE because here is the only place both rows are in view.
    %if ((%2) & ~(%1)) & 0xFFFF
      %error "mouse: a black cursor row escapes its white row - cur_draw's fused write would drop those pixels"
    %endif
  %else
    dw %2
  %endif
  %assign cur_row_i cur_row_i + 1
%endmacro

; white (outline + fill)        black (the body)
%macro CUR_ARROW 0
    CUR_ROW 0xE000,             0x8000      ; ***.....  #.......
    CUR_ROW 0xF000,             0xC000      ; ****....  ##......
    CUR_ROW 0xF800,             0xE000      ; *****...  ###.....
    CUR_ROW 0xFC00,             0xF000      ; ******..  ####....
    CUR_ROW 0xFE00,             0xF800      ; *******.  #####...
    CUR_ROW 0xFF00,             0xFC00      ; ********  ######..
    CUR_ROW 0xFF00,             0xFE00      ; ********  #######.
    CUR_ROW 0xFF00,             0xF800      ; ********  #####...
    CUR_ROW 0xFE00,             0xD800      ; *******.  ##.##...
    CUR_ROW 0xFE00,             0x8C00      ; *******.  #...##..
    CUR_ROW 0xDE00,             0x0C00      ; **.****.  ....##..
    CUR_ROW 0x1E00,             0x0000      ; ...****.  ........
    CUR_ROW 0x0000,             0x0000
    CUR_ROW 0x0000,             0x0000
    CUR_ROW 0x0000,             0x0000
    CUR_ROW 0x0000,             0x0000
%endmacro

; The crosshair (SPEC.md 7.2), hot spot at its centre. A 1px cross dilated by
; a pixel is a 3px white bar with the black cross inside it - the same
; outline-and-body construction the arrow uses, and the only one that reads on
; both a white desktop and a black sky. It is 8x11 inside the arrow's own
; 8x12 cell, which is the whole reason it costs the drawing code nothing:
; CUR_GW/CUR_GH/CUR_SPAN are what every walk in this file is bounded by, and a
; shape that needed a bigger cell would give SPEC.md 7.1's third-byte and
; row-count savings back on EVERY lock hold in the machine, for every app.
; The gap in the horizontal stroke must fall on the SAME column as the
; vertical one - both are the hot spot, (CUR_XHX, CUR_XHY), and drawn a column
; apart the cross reads as broken rather than as aiming at anything. Nothing
; can assert that: it is a statement about which column means the hot spot,
; and the tables do not know. Look at it.
%macro CUR_CROSS 0
    CUR_ROW 0x3800,             0x1000      ; ..***...  ...#....
    CUR_ROW 0x3800,             0x1000
    CUR_ROW 0x3800,             0x1000
    CUR_ROW 0x3800,             0x1000
    CUR_ROW 0xFF00,             0x0000      ; ********  ........
    CUR_ROW 0xFF00,             0xEF00      ; ********  ###.####
    CUR_ROW 0xFF00,             0x0000      ; ********  ........
    CUR_ROW 0x3800,             0x1000
    CUR_ROW 0x3800,             0x1000
    CUR_ROW 0x3800,             0x1000
    CUR_ROW 0x3800,             0x1000
    CUR_ROW 0x0000,             0x0000
    CUR_ROW 0x0000,             0x0000
    CUR_ROW 0x0000,             0x0000
    CUR_ROW 0x0000,             0x0000
    CUR_ROW 0x0000,             0x0000
%endmacro

; --- the shape table (SPEC.md 7.2) -------------------------------------------
; One entry per shape, each a white table immediately followed by its black
; one, so [cur_tabp] points at the white table and [si+CUR_TABSZ] still reads
; the black row - the idiom every draw loop here already uses, unchanged.
;
; CUR_SHAPE_END asserts the SAME three facts per shape that the arrow alone
; used to assert: the widest bit set, the last row that sets one, and that
; every black bit is also a white one (that last inside CUR_ROW). All three
; fail silently - a wider shape loses its right-hand column on the two mono
; adapters, a taller one leaves its bottom rows on screen for the rest of the
; session, and an escaped black bit is simply not drawn on VGA - so a shape
; added here is checked at assembly time or it is not checked at all.
%macro CUR_SHAPE_END 1
  %if cur_hi > CUR_GH
    %error %strcat("mouse: cursor shape ", %1, " is taller than CUR_GH")
  %endif
  %if (cur_bits & ((1 << (16 - CUR_GW)) - 1)) != 0
    %error %strcat("mouse: cursor shape ", %1, " is wider than CUR_GW")
  %endif
%endmacro

%assign cur_bits 0
%assign cur_hi   0
%assign cur_tab   0
%assign cur_row_i 0
cur_shapes:
cur_and:                        ; white: the body dilated by 1px, right and down
    CUR_ARROW
%assign cur_tab   1
%assign cur_row_i 0
cur_data:                       ; black: the arrow body itself
    CUR_ARROW
cur_tabend:
CUR_SHAPE_END "arrow"

%assign cur_bits 0
%assign cur_hi   0
%assign cur_tab   0
%assign cur_row_i 0
cur_cross_and:
    CUR_CROSS
%assign cur_tab   1
%assign cur_row_i 0
cur_cross_data:
    CUR_CROSS
CUR_SHAPE_END "crosshair"

; --- the hot spot per shape, in the same order (SPEC.md 7.2) -----------------
; Two bytes a shape: the pixel inside the cell that sits under the pointer.
; The arrow's is (0,0), which is what every walk in this file assumed for
; years - cur_geom and cur_rect subtract it now, and clamp the result at the
; screen origin rather than clipping the cell (SPEC.md 7.2.2).
cur_shtab   dw cur_and,       cur_cross_and
cur_shhot   db 0, 0,          CUR_XHX, CUR_XHY

; --- what the drawing code above claims about those tables -------------------
%if cur_data - cur_and != CUR_TABSZ
  %error "mouse: cur_data must sit exactly one table past cur_and - cur_put_mono reads the black row at [si+CUR_TABSZ]"
%endif
%if cur_tabend - cur_data != CUR_TABSZ
  %error "mouse: a cursor table is not CUR_H rows"
%endif
%if cur_cross_data - cur_cross_and != CUR_TABSZ
  %error "mouse: a shape's black table must sit exactly one table past its white one"
%endif
%if (cur_shhot - cur_shtab) != CUR_NSHAPE * 2
  %error "mouse: cur_shtab does not carry one entry per CUR_NSHAPE"
%endif

; -----------------------------------------------------------------------------
; module state (small - stays inline)
; -----------------------------------------------------------------------------
mouse_x     dw SCREEN_W/2       ; public: cursor position, 0..639
mouse_y     dw SCREEN_H/2       ; public: 0..479
mouse_btn   db 0                ; public: bit 0 = left, bit 1 = right

%ifdef CURFIX
cur_mvt     dw 0                ; [ticks] at the last movement packet (SPEC.md
                                ; 7.1.4.3). In .text with a real initialiser
                                ; for cur_sptr's reason below - and 0 is the
                                ; right one either way: it reads as "moved at
                                ; tick 0", so a machine that has never seen a
                                ; packet is STILL, which is what it is
%endif

cur_sptr    dw cur_save         ; -> whichever save buffer is live. In .text
                                ; with a REAL initialiser, not .bss: .bss
                                ; arrives zeroed (kernel.asm - NASM pads the
                                ; gap to OVL_START), and 0 here is a pointer
                                ; at the API jump table
cur_tabp    dw cur_and          ; -> the live shape's white table (SPEC.md
                                ; 7.2). In .text with a real initialiser, for
                                ; cur_sptr's reason above: -f bin zeroes
                                ; nothing and 0 here is the API jump table.
                                ; The three draw loops read it instead of
                                ; naming cur_and, and that is the WHOLE of
                                ; what changing the picture costs
cur_hx      dw 0                ; ...and its hot spot: the pixel inside the
cur_hy      dw 0                ; cell that sits under the pointer. WORDS
                                ; rather than the bytes the table stores,
                                ; because all three readers subtract them from
                                ; a position and the two that matter have no
                                ; scratch register left to widen a byte in
cur_shape   db CUR_ARROWSH      ; which shape that is - compared per UI pass,
                                ; so the swap is what is rare, not the check
cur_shchk   db 0                ; "the window under the pointer may have
                                ; changed without the pointer moving": a
                                ; raise, a hide, a destroy or a drag. One
                                ; byte, the [fm_fchk] shape (SPEC.md 22.8)
cur_shx     dw -1               ; the position cur_shape_pass last answered
cur_shy     dw -1               ; for; -1 cannot be a clamped mouse position
cur_level   db -1               ; refcount; < 0 = hidden (boot state)
cur_dirty   db 0                ; set when the ISR could not move the cursor
cur_drawn_x dw 0                ; where the cursor pixels actually are, in
cur_drawn_y dw 0                ; VIRTUAL coordinates (SPEC.md 39.15) - the
                                ; same space as mouse_x/mouse_y, so every
                                ; comparison in this file is between two
                                ; positions of the same kind. cur_rect and
                                ; cur_geom are the only two places that turn
                                ; one into an ADDRESS, and the only two that
                                ; subtract a display's origin.
                                ; (valid only while cur_level >= 0)
mou_nx      dw 0                ; the candidate x, staged across the y decode
                                ; because mou_clamp is 2-D (SPEC.md 39.15.4)
%ifdef KERN_BIG
cur_disp    db 0                ; which display the arrow is ON, and cur_dprev
cur_dprev   db 0                ; the one to put back afterwards (39.15.2).
                                ; .text with real initialisers, like cur_sptr
                                ; above and for its reason: -f bin zeroes
                                ; nothing, and a garbage display index indexes
                                ; past vid_ctx
%endif

mou_old_off dw 0, 0             ; the vectors we displaced, per port row
mou_old_seg dw 0, 0
; The decoder, PER PORT ROW (stride 2, so [bx+...] indexes it directly - the
; odd byte of each pair is padding and costs 4 bytes in all). The two ports
; decode independently, which is what stops either one corrupting the other's
; packets or resetting the other's run (SPEC.md 9.5).
mou_phase   db 0,0,0,0          ; packet state: 0 idle, 1 got b0, 2 got b1
mou_b0      db 0,0,0,0          ; packet byte 0: 1 LB RB Y7 Y6 X7 X6
mou_b1      db 0,0,0,0          ; packet byte 1: X low 6 bits
mou_evrec   times 8 db 0        ; scratch event record for evq_push (ISR only).
                                ; ABOVE the published block on purpose - it is
                                ; scratch, not state, and it used to sit in
                                ; the middle of what SPEC.md 9.4.2 publishes

; --- the published diagnostic block (SPEC.md 9.4.2) --------------------------
; Everything from mou_run to mou_dstamp is one contiguous span that a test
; package reads through 0060:0006. The offsets are asserted at the bottom of
; this file, because a reordering here would silently republish the block as
; a different thing and the reader has no way to notice.
mou_dbg_blk:
    dw 0x4F4D                   ; 'MO' - the magic, dsk_dbg_blk's 0x4444 idiom
    dw mou_bases                ; -> the probed bases, 2 words, 0 = no UART
    dw mou_run                  ; -> the state span below
mou_dbg_cur:
    dw mouse_x                  ; -> the CURSOR: mouse_x, mouse_y, mouse_btn.
                                ; APPENDED, so every offset above is unmoved
                                ; and an older reader is unaffected.
                                ;
                                ; It is here for the HARNESS rather than for
                                ; the field (SPEC.md 9.4.3): MartyPC's mouse is
                                ; relative, so a scripted click can only be
                                ; aimed by dead reckoning from an edge clamp,
                                ; and every accumulated packet that the guest
                                ; rounded or dropped moves the target. Reading
                                ; where the cursor ACTUALLY is closes the loop:
                                ; the driver reads, computes the exact delta,
                                ; sends it and re-reads.
                                ;
                                ; This is deliberately NOT a way to place the
                                ; cursor. Nothing here is writable by the
                                ; harness in any meaningful sense - a poke to
                                ; mouse_x would skip the UART, mou_isr and the
                                ; packet decoder, which is exactly the path a
                                ; scripted test exists to exercise. The packet
                                ; still does all the work; this only says where
                                ; it landed
mou_dbg_p2:
    dw mou_p2                   ; -> the PS/2 mouse's 7-byte span (SPEC.md
                                ; 9.4.4). APPENDED, and APPENDED MEANS LAST:
                                ; the first draft of this put it in front of
                                ; the word above, which moved the one thing in
                                ; the block that a HOST tool reads by a
                                ; hard-coded offset - tools/os88mouse.py's
                                ; blk+6 - and turned every scripted click into
                                ; a read of two unrelated bytes. The two %errors
                                ; below are what stops that being found again
                                ; by a harness that has quietly stopped working

; --- ...and the WORD POSITIONS, which are as readable-by-offset as the span --
%if mou_dbg_cur - mou_dbg_blk != 6
  %error "mouse: SPEC.md 9.4.3 block - the cursor word moved (os88mouse.py)"
%endif
%if mou_dbg_p2 - mou_dbg_blk != 8
  %error "mouse: SPEC.md 9.4.4 block - the PS/2 word is not the fifth"
%endif

mou_run     db 0,0,0,0          ; clean packets in a row on this port

mou_port    db 0                ; the mouse's row, settled by [mou_need] clean
                                ; packets and never moved again (SPEC.md 9.5).
                                ; A row index (0, 2), not a COM number - or
                                ; MOU_P2ROW = 4, which is no serial row at all
                                ; and means the PS/2 mouse won (SPEC.md 9.9.4)
mou_need    db 1,0,1,0          ; the run a port must manage to be believed:
                                ; 1 where the probe found one port, MOU_LOCKN
                                ; where it found two - and 1 again on the one
                                ; port whose identify burst named it the only
                                ; mouse-shaped one (SPEC.md 9.4.1). PER PORT,
                                ; stride 2 like the decoder state above

; --- the identify burst, per port (SPEC.md 9.4.1) ----------------------------
; Banked by mou_idbyte inside mouse_init's drain window and spent once by
; mou_idjudge. All of it is dead for the life of the machine afterwards.
mou_idn     db 0,0,0,0          ; bytes seen in the window (saturating at 0FFh:
                                ; a wrap would let a stream pass MOU_IDMAX)
mou_idb0    db 0,0,0,0          ; the FIRST of them - rule 2 is this byte
mou_idlast  dw 0, 0             ; [ticks] at the last one, for the quiet test
mou_ident   db 0,0,0,0          ; this port answered like a mouse. DIAGNOSTIC,
                                ; and deliberately kept: nothing reads it, the
                                ; two consequences hanging off [mou_idany] and
                                ; mou_idjudge's own CL/CH instead - but this
                                ; feature can only be confirmed on real iron
                                ; (QEMU's msmouse ignores DTR entirely), and
                                ; per-port "who answered" is the one thing a
                                ; field peek needs that the rest cannot say
mou_idany   db 0                ; ...and at least one did, which is what the
                                ; recovery cycle stands down on
mou_seen    db 0                ; a complete packet has arrived this session;
                                ; set by the ISR, read by mou_hotplug, and
                                ; never cleared - proof the mouse exists
mou_hpst    db 0                ; hot-plug poller state: 0 waiting out the
                                ; interval, 1 DTR/RTS held low, 2 stood down
                                ; for good (the ports lost their contest)
mou_hpt     dw 0                ; tick stamp for whichever wait state 0/1 is in
mou_drain   db 0                ; ISR discards RX while set (armed by a
mou_dstamp  dw 0                ; hot-plug raise; expires MOU_DRAINT later)
mou_line    db 0                ; the 8259 bit of the line the WINNING packets
                                ; came in on, banked by mou_claim beside
                                ; [mou_seen]. 0x10 = IRQ4, 0x08 = IRQ3, 0 =
                                ; not settled. Published because a machine
                                ; whose mouse is at 2F8 on IRQ4 (SPEC.md
                                ; 9.5.2.1) is otherwise indistinguishable from
                                ; an ordinary one, and only on real iron
; --- ...and here the published span ENDS. Scratch below it ------------------
mou_lfired  db 0                ; the line CURRENTLY being serviced, stamped by
                                ; the ISR entry. Live for the length of one
                                ; interrupt, meaningless outside it, and so not
                                ; state anyone off-machine should read

; --- the PS/2 mouse, and the span SPEC.md 9.4.4 publishes --------------------
mou_p2      db 0                ; a PS/2 mouse answered and is LIVE. Cleared
                                ; again by mou_p2_off if a serial port wins
mou_p2st    db 0                ; ...and how far mou_p2_init's handshake got,
                                ; which is what separates "this machine has no
                                ; auxiliary port" from "it has one with nothing
                                ; plugged in": 0 the tier or the 8042 refused
                                ; it outright, 2 no aux port, 4/5/6 the device
                                ; did not answer its reset, 8 int 74h is ours,
                                ; 9 live (SPEC.md 9.9.1). It is the STEP and
                                ; not a flag: mou_p2_off clears [mou_p2] and
                                ; leaves this alone, because the vector is
                                ; still owed back
mou_p2id    db 0                ; the device ID it answered the reset with - 0
                                ; is a plain mouse. Banked and published, never
                                ; tested: a wheel mouse in its 3-byte boot mode
                                ; is exactly the mouse this decoder wants
mou_p2ph    db 0                ; packet state: 0 idle, 1 got b0, 2 got b1
mou_p2b0    db 0                ; byte 0: YV XV YS XS 1 MB RB LB
mou_p2b1    db 0                ; byte 1: the X magnitude
mou_p2cmd0  db 0                ; the 8042's command byte AS THE BIOS LEFT IT,
                                ; which every failure path above puts back and
                                ; which no reading of this source can supply
; --- ...and here THAT span ends. Scratch below it ---------------------------
%ifdef KERN_BIG                 ; ...and this one really is the code's, so it
mou_p2old   dw 0, 0             ; goes with it: int 74h's old vector, restored
%endif                          ; by mouse_unhook

; --- what SPEC.md 9.4.2 says the published block IS --------------------------
; A test package reads these by offset, off a floppy, on a machine with no
; debugger. Every one of them is a number some reader will print as a
; different quantity if it moves, and NASM cannot see any of that - so the
; layout is asserted here rather than trusted to declaration order.
%define MOUDBG(x) ((x) - mou_run)
%if MOUDBG(mou_port)   != 4  || MOUDBG(mou_need)   != 5
  %error "mouse: SPEC.md 9.4.2 block - mou_port/mou_need moved"
%endif
%if MOUDBG(mou_idn)    != 9  || MOUDBG(mou_idb0)   != 13
  %error "mouse: SPEC.md 9.4.2 block - mou_idn/mou_idb0 moved"
%endif
%if MOUDBG(mou_idlast) != 17 || MOUDBG(mou_ident)  != 21
  %error "mouse: SPEC.md 9.4.2 block - mou_idlast/mou_ident moved"
%endif
%if MOUDBG(mou_idany)  != 25 || MOUDBG(mou_seen)   != 26
  %error "mouse: SPEC.md 9.4.2 block - mou_idany/mou_seen moved"
%endif
%if MOUDBG(mou_hpst)   != 27 || MOUDBG(mou_hpt)    != 28
  %error "mouse: SPEC.md 9.4.2 block - mou_hpst/mou_hpt moved"
%endif
%if MOUDBG(mou_drain)  != 30 || MOUDBG(mou_dstamp) != 31
  %error "mouse: SPEC.md 9.4.2 block - mou_drain/mou_dstamp moved"
%endif
%if MOUDBG(mou_line)   != 33
  %error "mouse: SPEC.md 9.4.2 block - mou_line moved"
%endif

; --- ...and what SPEC.md 9.4.4 says the PS/2 span is ------------------------
; Seven bytes, read by the same reader through the fifth word of the block
; above, and asserted here for the identical reason: nothing in NASM notices a
; member that moved, and the program that reads it is on a floppy.
%define MOUP2(x) ((x) - mou_p2)
%if MOUP2(mou_p2st) != 1 || MOUP2(mou_p2id)   != 2
  %error "mouse: SPEC.md 9.4.4 block - mou_p2st/mou_p2id moved"
%endif
%if MOUP2(mou_p2ph) != 3 || MOUP2(mou_p2b0)   != 4
  %error "mouse: SPEC.md 9.4.4 block - mou_p2ph/mou_p2b0 moved"
%endif
%if MOUP2(mou_p2b1) != 5 || MOUP2(mou_p2cmd0) != 6
  %error "mouse: SPEC.md 9.4.4 block - mou_p2b1/mou_p2cmd0 moved"
%endif

section .bss
cur_save    resb CUR_SAVE_SZ    ; save-under: CUR_SPAN bytes x CUR_GH rows,
                                ; x four planes on VGA
cur_savex   resb CUR_SPAN * CUR_GH  ; ...and its twin. A MOVE reads one while
                                ; it fills the other (SPEC.md 7.1.2); 24 bytes,
                                ; and the pointer swap means nothing is copied
cur_lazy    resb 1              ; gfx_lock deferred the hide (SPEC.md 7.1.4):
                                ; the arrow is still on screen this hold
cur_off     resw 1              ; cur_geom's answers, banked for cur_get_mono:
cur_rows    resb 1              ; the cell's top-left framebuffer byte, the
cur_b1ok    resb 1              ; rows of it on screen, and whether the DI+1
                                ; byte of each row is still inside the row
cur_cellx   resw 1              ; ...and where the CELL is, display-local and
cur_celly   resw 1              ; hot-spot-relative (SPEC.md 7.2.2). Banked
                                ; for the same reason the three above are:
                                ; cur_move_mono asks about it three times and
                                ; two derivations of a save-under's geometry
                                ; are two chances to disagree by a byte

mv_oy       resw 1              ; cur_move's banked OLD geometry: top row,
mv_ooff     resw 1              ; framebuffer offset, rows, right-byte clip
mv_orows    resb 1
mv_ob1ok    resb 1
mv_osave    resw 1              ; -> the buffer holding the old background...
mv_nsave    resw 1              ; ...and the one taking the new
mv_d        resw 1              ; newcol - oldcol, signed: the ONE number the
                                ; four column answers below come out of
mv_p1s0     resb 1              ; pass 1 skips this old column (pass 2 has it)
mv_p1s1     resb 1
mv_p2s0     resb 1              ; pass 2's background offset in the old save,
mv_p2s1     resb 1              ; or 0FFh for "read the screen"
mv_prow     resw 1              ; the new cell's row, as pass 2 walks it
mv_orp      resw 1              ; -> that row in the old save, or 0
section .text
