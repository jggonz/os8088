; =============================================================================
; lplink.inc - the LapLink nibble transport, shared by NET.DRV and lptlink
;
; ONE BODY, included by both, so THE THING MEASURED IS THE THING SHIPPED.
; tests/lptlink is what produced PERFORMANCE.md Part 9 Set 39's 3,741
; bytes/second on the iron; NET.DRV is what a Network volume's sectors travel
; over. Two copies of a bit-banged handshake would drift, and the drift would
; present as a cable fault on somebody else's pair of machines.
;
; docs/plans/completed/NET-PLAN.md 1.1 is the cable, 1.1.1 the handshake and why it is fully
; interlocked, 1.4 the port scan, and Set 39 the accounting - including why
; the deadline is lazy, which is 90% of the throughput.
;
; WHAT IS HERE: the port candidates, the latch probe, the nibble layer and the
; MASTER's connect. WHAT IS NOT: the slave half (slv_hunt, slv_reply) and
; anything that reads a keyboard, because os8088 is always the master
; (NET-PLAN 1.3) and a driver has no console.
;
; WHAT THE INCLUDER MUST PROVIDE:
;
;   lpl_ticks   a near proc: out AX = a rising 18.2 Hz counter, every other
;               register and the flags preserved. lptlink reads 0040:006C
;               itself; NET.DRV calls OSAPI_GET_TICKS. It is the one thing
;               the two includers genuinely disagree about.
;
; It owns no interrupt and hooks nothing, and every wait is bounded IN TICKS -
; NET-PLAN 1.3 rule 1, doubly binding here because DSV_BLK runs with
; [sch_lock] raised and the gfx lock held, so a spin that never ends is a hard
; freeze with the cursor stopped.
; =============================================================================

; --- the hello magic, as a NIBBLE sequence ----------------------------------
; 'O','8','8','?' = 4F 38 38 3F, sent low nibble first, so the wire carries
; F,4,8,3,8,3,F,3 - and the slave hunts for exactly that in a sliding 8-nibble
; window (slv_hunt). A window at NIBBLE granularity rather than byte
; granularity is what makes a slave that joined mid-transfer resynchronise:
; the same idea as SPEC.md 9.5's mouse resync, one level down.
MAGQ_HI     equ 0xF483
MAGQ_LO     equ 0x83F3

MAXCAND     equ 8               ; the BIOS table (3) plus the three standard
                                ; bases, deduped - so usually 3 or 4
PROTO_VER   equ 1               ; what the reply carries by DEFAULT, so a
                                ; mismatched pair says so instead of desyncing.
                                ; **IT IS A VARIABLE ON THE SLAVE SIDE**
                                ; ([lp_myver]): the version byte is how a far
                                ; end says whether it speaks the socket verbs
                                ; (netsock.inc's NET_VER_SOCK), and OS88NET.COM
                                ; only does when /N brought a stack up. A
                                ; constant here would have to promise sockets
                                ; on every run or on none

; --- every wait is bounded IN TIME, not in polls -----------------------------
; NET-PLAN 1.3 rule 1: a cable that is not plugged in must produce a failed
; link, not a dead machine. The first version counted POLLS, and that is the
; wrong unit for a link between two machines of different speeds: LP_SPIN
; poll iterations is ~75ms on a 4.77MHz 8088 and ~5ms on anything modern, so
; the fast end's patience ran out inside the slow end's ordinary response -
; and the whole point of this cable is that the two ends are NOT alike. So
; LP_SPIN is only the granularity at which the deadline is re-checked (a
; `ticks` read per 5,000 polls costs nothing), and the deadline itself is in
; SYSTEM TICKS, which both machines measure identically.
LP_SPIN     equ 5000           ; polls between deadline checks
LP_TMO      equ 2              ; ticks - ~110ms, the ordinary wait

; --- the two sweep constants, and the rule that relates them -----------------
; NET-PLAN 1.4.5: two sweeps running at similar rates can miss each other
; indefinitely, each arriving just after the other has left, and it presents
; as an intermittent cable fault rather than as a timing bug. The fix is that
; they must NOT be the same speed - the SLAVE's dwell on one candidate has to
; exceed the MASTER's whole sweep, so a full master pass happens inside one
; dwell and cannot be missed twice. The master's per-candidate cost is one
; LP_TMO (2 ticks) waiting for an ack that never comes, so a 7-candidate sweep
; is ~14 ticks and 24 clears it with room. BOTH SIDES OF THAT INEQUALITY ARE
; IN TICKS, which is the only reason it can be checked by reading.
SLAVE_DWELL equ 24              ; ticks (18.2065 Hz) listening on one candidate
TURN_RX     equ 8               ; ticks a RECEIVE will wait for its first
                                ; nibble when it has just reversed direction -
                                ; the far end is inside lp_turn and owes a
                                ; whole tick, and LP_SPIN is ~2ms on a fast
                                ; machine. See lp_rnib. It is the SWEEP's
                                ; figure: [lp_turnw] below is what is actually
                                ; used, and a linked master raises it
REPLY_TMO   equ 182             ; ...to this - 10 seconds - once a partner has
                                ; answered. THE FIELD BUG THIS FIXES: a
                                ; reversal is the master having just finished
                                ; ASKING, so what it is waiting for is not
                                ; lp_turn's tick, it is the FAR SIDE DOING THE
                                ; WORK - and on a redirector that means DOS
                                ; touching a disk. Double-clicking a folder on
                                ; a floppy-served volume spins a motor and
                                ; walks the directory TWICE (the count must
                                ; precede the entries, SPEC.md 62.10.1), which
                                ; is seconds. At 8 ticks os8088 gave up at
                                ; 440ms, dropped the link and blanked the
                                ; listing, while the DOS side finished its walk
                                ; and sent the reply into a wire nobody was
                                ; reading - so it printed nothing and went back
                                ; to listening, which is exactly what it should
                                ; do and says nothing about the fault.
                                ;
                                ; It cannot simply REPLACE TURN_RX: the port
                                ; sweep's per-candidate cost is a reversal that
                                ; times out, so a 7-candidate pass would take
                                ; 70 seconds instead of 3. Short while looking,
                                ; patient once found.
                                ;
                                ; IT GOVERNS BOTH DIRECTIONS, and for one
                                ; round it governed only the receive. A SEND
                                ; waits for the far end's ack, and the far end
                                ; may not be looking yet - srv_write reads the
                                ; length, CREATES THE FILE and only then reads
                                ; a body byte. That put a floppy's directory
                                ; write between our length and our first byte,
                                ; where 110ms was certain to expire: the link
                                ; dropped, the Disk window blanked, and the
                                ; far side was left holding a file it had
                                ; created and never filled - a ZERO-BYTE
                                ; paste, SPEC.md 62.10.4.8. The same bug as
                                ; the paragraph above wearing the other hat,
                                ; which is why lp_snib now reads this word too
                                ;
                                ; 182 and not more because lp_dlchk takes the
                                ; allowance as a BYTE (255 ticks, 14 s) and
                                ; because an unplugged cable costs this much
                                ; frozen UI - the gfx lock is held across a
                                ; file operation
                                ; There is no CMD_WAIT. The gap between
                                ; commands is the USER'S THINKING TIME and has
                                ; no upper bound, so lp_rbyte_w waits for as
                                ; long as it takes and ESC is the way out - see
                                ; its header in lplslv.inc for what a 10-second
                                ; one cost.

; -----------------------------------------------------------------------------
; lp_latch - is there a parallel port at AX? (NET-PLAN 1.4.2)
; out: CF=0 a latch answered, CF=1 nothing there
; clobbers: nothing
;
; base+0 is a readable latch on every parallel port from the original IBM
; Printer Adapter onward - the BIDIRECTIONAL data path is the later extension,
; not the read-back. TWO values, not one, for SPEC.md 9.5's stated reason: an
; unpopulated ISA address answers FF, but a bus that happened to float to
; whatever single value you wrote would otherwise pass.
;
; It restores what it found, which is not politeness: a printer on this port
; sees its data lines change, and because NOTHING IS STROBED, nothing prints.
; -----------------------------------------------------------------------------
lp_latch:
    push ax
    push bx
    push cx
    push dx
    mov dx, ax
    add dx, 2                   ; the CONTROL register first: a bidirectional
    in  al, dx                  ; port left in INPUT mode reads its PINS rather
    mov cl, al                  ; than its latch, so the two-value test below
    and al, 0xDF                ; would probe a port that cannot answer. The
    out dx, al                  ; DOS machine's DIO-500 came up at ctrl = EC,
    sub dx, 2                   ; bit 5 SET - exactly that case, and it passed
                                ; only because that card reads its latch back
                                ; anyway. Read-modify-write, low nibble held:
                                ; bit 2 is INIT and active low (NET-PLAN 1.4.3)
    in  al, dx
    mov bh, al                  ; whatever was there - put back on both paths
    mov al, 0xAA
    out dx, al
    jmp short $+2               ; the period I/O settle idiom; the latch does
    jmp short $+2               ; not need it and a slow bus buffer might
    in  al, dx
    cmp al, 0xAA
    jne .no
    mov al, 0x55
    out dx, al
    jmp short $+2
    jmp short $+2
    in  al, dx
    cmp al, 0x55
    jne .no
    mov al, bh
    out dx, al
    clc
    jmp short .ctl
.no:
    mov al, bh
    out dx, al
    stc
.ctl:
    pushf                       ; the verdict is in CF and `out` does not touch
    add dx, 2                   ; it - but the restore must not either, and a
    mov al, cl                  ; caller reading a probe result through a
    out dx, al                  ; clobbered flag is the shape of bug this file
    popf                        ; has already had once
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE LINK LAYER - nibble mode over a LapLink cable (NET-PLAN 1.1)
;
;   D0..D3 carry the nibble, D4 is the strobe/phase bit.
;   The far end's five lines arrive on our status register in bits 3..7, and
;   BUSY - bit 7 - is INVERTED in hardware. Because the inverted one is the
;   TOP one, one `xor` before the shift fixes it and the whole decode is three
;   instructions; an earlier draft of NET-PLAN called for a 32-entry xlat
;   table on the assumption the bits were scattered, and they are not.
;
;   The handshake is a phase toggle. The sender puts nibble+phase and waits
;   for the far end to echo that phase back; the receiver waits for a phase
;   DIFFERENT from the one it last acked, reads, and acks with the new one.
;   Idle is all lines low with the sender's first phase 1, so the very first
;   nibble differs from the idle line and nothing has to be pre-agreed.
; =============================================================================

; -----------------------------------------------------------------------------
; lp_setport - AX = the base address
; clobbers: AX
; -----------------------------------------------------------------------------
lp_setport:
    mov [lp_base], ax
    inc ax
    mov [lp_stat], ax
    inc ax
    mov [lp_ctrl], ax
    ret

; -----------------------------------------------------------------------------
; lp_init - take the port over, saving what we found
; clobbers: nothing
;
; THE CONTROL REGISTER IS READ-MODIFY-WRITE (NET-PLAN 1.4.3). Bit 2 is INIT
; and it is ACTIVE LOW, so `out base+2, 0` - the obvious way to put a port
; into a known state - pulses reset at whatever printer is on it. Only bit 5
; is touched (direction = out on a bidirectional port, a no-op elsewhere) and
; the low four bits are preserved exactly as found.
; -----------------------------------------------------------------------------
lp_init:
    push ax
    push dx
    mov dx, [lp_base]
    in  al, dx
    mov [lp_svdat], al
    mov dx, [lp_ctrl]
    in  al, dx
    mov [lp_svctl], al
    and al, 0xDF
    out dx, al
    mov dx, [lp_base]
    xor al, al
    mov byte [lp_lastop], 0     ; a fresh port owes no turnaround
    mov byte [lp_turnw], TURN_RX    ; ...and is being LOOKED for, not talked to
    out dx, al                  ; idle: five lines low, strobe down. Both
                                ; roles idle identically, which is what makes
                                ; a direction reversal safe (lp_snib)
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; lp_restore - put the port back exactly as it was found
; clobbers: nothing
; -----------------------------------------------------------------------------
lp_restore:
    push ax
    push dx
    mov dx, [lp_base]
    mov al, [lp_svdat]
    out dx, al
    mov dx, [lp_ctrl]
    mov al, [lp_svctl]
    out dx, al
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; lp_turn - the direction-reversal guard, and the reason it has to exist
;
; Four phases removed the race WITHIN a direction and left it at the seam. The
; old receiver's last act is to drop its ack; its first act as the new sender
; is to raise its strobe - the SAME LINE - and between those two the old
; sender is still in its ack-down poll. Miss the low and it waits for a fall
; that already happened, for ever. Microseconds separate them, and the old
; sender polls every ~15us on a 4.77MHz machine, so on a mismatched pair it is
; not a rare race but the usual outcome.
;
; So the new sender waits out ONE WHOLE SYSTEM TICK before it drives anything.
; Two tick edges, because one may arrive immediately: the second is a full
; 54.9ms later, which is ~3,600 of the far end's polls. It is machine
; independent - a spin count is not, and this has to hold between a 5150 and
; whatever is on the other end of the cable - and it costs nothing where it
; lands, a handful of reversals per run rather than one per nibble.
;
; It is called FROM lp_snib on the [lp_lastop] flag rather than from the six
; places that reverse direction, because a guard a call site can forget is a
; hang that only shows up on somebody else's pair of machines.
; -----------------------------------------------------------------------------
lp_turn:
    push ax
    push bx
    call lpl_ticks
    mov bx, ax
.e1:
    call lpl_ticks
    cmp ax, bx
    je  .e1
    mov bx, ax
.e2:
    call lpl_ticks
    cmp ax, bx
    je  .e2
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; LP_WAIT - wait for the far end's D4 to reach a level, bounded IN TICKS
;   %1 = jnz (wait for it HIGH) or jz (wait for it LOW)
;   %2 = the tick allowance      %3 = where to go when it expires
; in:  DX = the status port.  uses AL, CX.  PRESERVES BX (rnib's nibble).
; out: AL = the XORed status byte that satisfied it, so a nibble is sampled by
;      the very read that detected the strobe.
;
; A MACRO AND NOT A ROUTINE, and the field run is why. This was `lp_wfar`, a
; near proc, called FOUR TIMES A NIBBLE - and the first measurement on real
; hardware came back at 1,536 bytes/second, 325 us a nibble, against a model
; that said 77. Counted against PERFORMANCE.md Part 2's own constants the
; prediction was 360 us, within 11% of the measurement, and it says where:
;
;     lp_wfar, one call        72.2 us   (of which ticks() 23.5)
;     ...4 of them a nibble   288.8 us
;     lp_rnib body + frame     71.2 us
;
; The WIRE is two `in`s and three `out`s a nibble, about 30 us. Everything
; else was the machinery of arriving - and the single worst item was reading
; the BIOS tick counter to build a deadline THAT IS ALMOST NEVER REACHED.
; SPEC.md 5.7's lesson in a new place: one gfx_pixel was 196 instructions of
; generic rect code across eleven routines with no hot spot anywhere.
;
; So: the deadline is LAZY - `ticks` is not read at all until a whole LP_SPIN
; of polls has gone unanswered, which on a working link never happens - the
; poll body is 9 bytes rather than 16 (the level is a BRANCH now, not a
; compare against a variable), and the whole thing is inlined so there is no
; frame and no call.
; -----------------------------------------------------------------------------
%macro LP_WAIT 3
    mov cx, LP_SPIN
    mov byte [lp_dlset], 0
%%poll:
    in  al, dx
    xor al, 0x80                ; BUSY is inverted, and it is the top bit
    test al, 0x80
    %1 %%got
    loop %%poll
    mov al, %2                  ; LP_SPIN polls unanswered: NOW ask the clock
    call lp_dlchk
    jc  %3
    mov cx, LP_SPIN
    jmp %%poll
%%got:
%endmacro

; -----------------------------------------------------------------------------
; lp_dlchk - the cold half of LP_WAIT. AL = the tick allowance.
; out: CF=1 the deadline has passed. Preserves every register.
;
; The FIRST expiry establishes the deadline and never fails, which is what
; keeps `ticks` off the fast path entirely: a link that is working never gets
; here, and one that is not pays a tick read per 5,000 polls.
; -----------------------------------------------------------------------------
lp_dlchk:
    push ax
    push bx
    cmp byte [lp_dlset], 0
    jne .chk
    mov byte [lp_dlset], 1
    mov bl, al
    xor bh, bh
    call lpl_ticks
    add ax, bx
    mov [dl_wf], ax
    clc
    jmp short .out
.chk:
    call lpl_ticks
    sub ax, [dl_wf]             ; modular compare (SPEC.md 45.15's js)
    js  .live
    stc
    jmp short .out
.live:
    clc
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; lp_snib - send AL's low nibble
; out: CF=0 acked, CF=1 timed out
;
; FULLY INTERLOCKED, four phases: nibble+strobe up, wait for ack up, strobe
; down, wait for ack down. Both ends idle at D4 = 0 between nibbles, and
; NEITHER LEAVES UNTIL THE OTHER HAS CONFIRMED.
;
; It was a two-phase toggle first - sender puts nibble+phase, receiver echoes
; the phase back as its ack, one wait each - which is half the polling and is
; RACY AT A DIRECTION REVERSAL. The old receiver acks and then, becoming the
; new sender, immediately drives its own strobe; if the old sender has not
; sampled in between, the ack is gone and it spins for ever. Between two
; machines of similar speed it mostly works, and between a 4.77MHz 8088 and
; anything modern it would hang every time - which is to say it would have
; presented as a cable fault on exactly the pair this exists for. Caught by
; the host-side model before it reached the iron, which is what SPEC.md
; 18.94.3 built that habit for.
;
; DX walks between base and base+1 with inc/dec rather than being re-loaded
; from memory: one byte and two clocks against four bytes and seventeen, three
; times a nibble, which is worth having once the calls are gone.
; -----------------------------------------------------------------------------
lp_snib:
    push ax
    push bx
    push cx
    push dx
    cmp byte [lp_lastop], 0     ; were we RECEIVING? then this is a direction
    je  .go                     ; reversal and it owes a guard - see lp_turn
    call lp_turn
    mov byte [lp_lastop], 0
.go:
    and al, 0x0F
    mov bl, al
    mov dx, [lp_base]
    out dx, al                  ; the nibble FIRST, strobe still low, so the
    or  al, 0x10                ; data is stable before the far end is told
    out dx, al                  ; to look at it
    inc dx                      ; -> status
                                ; ...wait for the ack to RISE, and this is
                                ; [lp_turnw] for lp_rnib's reason seen from
                                ; the other side (SPEC.md 62.10.4.8): the far
                                ; end may not be LOOKING yet. A WRITE is the
                                ; case - srv_write reads the length, calls
                                ; int 21h 3Ch to create the file, and only
                                ; then reads a body byte, so a floppy's
                                ; directory write lands between our length
                                ; and our first byte. At LP_TMO that is 110ms
                                ; against seconds and the send was certain to
                                ; fail
    LP_WAIT jnz, [lp_turnw], .bad
    dec dx                      ; -> data
    mov al, bl
    out dx, al                  ; strobe down, nibble held
    inc dx
    LP_WAIT jz, LP_TMO, .bad    ; ...and to FALL. Only then is the nibble
    clc                         ; complete at BOTH ends
    jmp short .out
.bad:
    mov dx, [lp_base]
    mov al, bl
    out dx, al                  ; leave the strobe DOWN however we left, or
    stc                         ; the next attempt starts mid-handshake
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; lp_rnib - receive one nibble and acknowledge it
; out: AL = the nibble, CF=0; CF=1 timed out (AL undefined)
; clobbers: AX, CF
;
; lp_snib's other half: wait for the strobe, sample the nibble WHILE it is
; high, ack, wait for the strobe to drop, drop the ack. The sample is taken
; from the same read that saw the strobe, so there is no window in which the
; sender could have moved on.
; -----------------------------------------------------------------------------
lp_rnib:
    push bx
    push cx
    push dx
    mov dx, [lp_stat]
    ; WAITING FOR A STROBE IS WAITING FOR THE FAR SIDE TO DO SOMETHING, and
    ; on a redirector that something can be a disk. This used to branch on
    ; [lp_lastop] - a REVERSAL got TURN_RX because the far end owed lp_turn a
    ; whole tick, and a CONTINUATION got LP_TMO because it was already
    ; streaming. The second half is false here: srv_list walks the directory
    ; TWICE (the count must precede the entries) and the second walk's
    ; findfirst happens between the count word and the first entry byte, which
    ; is mid-frame. So both are [lp_turnw] now and the branch is gone with
    ; them - one allowance, raised by a linked master and reset per port by
    ; lp_init. The wait for the strobe to FALL below keeps LP_TMO: the far
    ; side has already committed that nibble and is not going anywhere.
    LP_WAIT jnz, [lp_turnw], .bad
    shr al, 1                   ; far D3..D0 are status bits 6..3, sampled by
    shr al, 1                   ; the read that saw the strobe
    shr al, 1
    and al, 0x0F
    mov bl, al
    dec dx                      ; -> data
    mov al, 0x10
    out dx, al                  ; ack up
    inc dx
    LP_WAIT jz, LP_TMO, .bad    ; wait for the strobe to fall
    dec dx
    xor al, al
    out dx, al                  ; ack down - both ends idle again
    mov al, bl
    clc
    jmp short .fin
.bad:
    mov dx, [lp_base]
    xor al, al
    out dx, al                  ; idle on the way out of a timeout too
    stc
.fin:
    mov byte [lp_lastop], 1     ; the next send is a reversal and owes lp_turn
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; lp_sbyte / lp_rbyte - a byte is two nibbles, LOW ONE FIRST
; -----------------------------------------------------------------------------
lp_sbyte:                       ; AL = the byte. CF=1 timed out
    push ax
    call lp_snib                ; lp_snib masks to the low nibble itself
    jc .bad
    pop ax
    push ax
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    call lp_snib
    jc .bad
    pop ax
    clc
    ret
.bad:
    pop ax
    stc
    ret

lp_rbyte:                       ; out AL. CF=1 timed out
    push bx
    call lp_rnib
    jc .bad
    mov bl, al
    call lp_rnib
    jc .bad
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1
    or  al, bl
    clc
    pop bx
    ret
.bad:
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; lp_sword / lp_rword - low byte first
; -----------------------------------------------------------------------------
lp_sword:                       ; AX. CF=1 timed out
    push ax
    call lp_sbyte
    jc .bad
    pop ax
    push ax
    mov al, ah
    call lp_sbyte
    jc .bad
    pop ax
    clc
    ret
.bad:
    pop ax
    stc
    ret

lp_rword:                       ; out AX. CF=1 timed out
    push bx
    call lp_rbyte
    jc .bad
    mov bl, al
    call lp_rbyte
    jc .bad
    mov ah, al
    mov al, bl
    clc
    pop bx
    ret
.bad:
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; mst_hello - say the magic on the current port and check the answer
; out: CF=0 a partner replied, CF=1 nothing coherent
;
; The handshake alone is NOT the test (NET-PLAN 1.4.4). With no cable the
; status bits are constant, and a constant can accidentally satisfy a phase
; ack; what it cannot do is spell the reply magic. That is why there is a
; magic at all.
; -----------------------------------------------------------------------------
mst_hello:
    push ax
    push bx
    push cx
    push si
    mov si, magic_q
    mov cx, 4
.snd:
    mov al, [si]
    inc si
    call lp_sbyte
    jc  .bad
    loop .snd
    mov si, magic_r
    mov cx, 4
.rcv:
    call lp_rbyte
    jc  .bad
    mov bl, [si]
    cmp al, bl
    jne .bad
    inc si
    loop .rcv
    call lp_rbyte               ; the partner's protocol version
    jc  .bad
    mov [lp_ver], al
    clc
    jmp short .out
.bad:
    stc
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cand_add - AX = a base address, BL = 1 if the BIOS listed it
;            Deduped: an address already on the list keeps its slot and ORs the
;            flag in, so "the BIOS found it AND it is one of the standard
;            three" is one row rather than two.
; clobbers: nothing
; -----------------------------------------------------------------------------
cand_add:
    push bx
    push cx
    push di
    push si
    xor si, si
    mov cl, [ncand]
    or  cl, cl
    jz .append
    xor ch, ch
.look:
    cmp ax, [cand_base+si]
    je  .have
    add si, 2
    loop .look
.append:
    mov cl, [ncand]
    cmp cl, MAXCAND
    jae .out
    xor ch, ch
    mov di, cx
    shl di, 1
    mov [cand_base+di], ax
    mov di, cx
    mov [cand_bios+di], bl
    mov byte [cand_ok+di], 0
    inc byte [ncand]
    jmp short .out
.have:
    mov di, si
    shr di, 1
    or  [cand_bios+di], bl
.out:
    pop si
    pop di
    pop cx
    pop bx
    ret


; -----------------------------------------------------------------------------
; lpl_scan - build the candidate list and probe every entry of it
; out: [ncand] rows in cand_base / cand_bios / cand_ok
; clobbers: nothing
;
; The BIOS's own table plus the three standard bases, deduped. The table is
; TRUSTED here in a way int 11h's floppy count is not (NET-PLAN 1.4.1): the
; POST fills it by PROBING, where the floppy count comes from SW1's DIP
; switches. It is still verified, because a clone BIOS may scan fewer
; addresses and a card can sit where the POST never looks.
;
; THREE words and not four: 0040:000E is LPT4 only on a pre-PS/2 BIOS and is
; the EBDA SEGMENT on everything since - the field run duly reported a
; parallel port at 9FC0, which is an EBDA under 640KB and not an I/O address
; at all. The range test is the belt to that brace.
; -----------------------------------------------------------------------------
lpl_scan:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov byte [ncand], 0
    mov ax, 0x0040
    mov es, ax
    xor di, di
.bios:
    mov ax, [es:di+8]
    or  ax, ax
    jz .bnext
    cmp ax, 0x0400              ; not an I/O port - see the header
    jae .bnext
    mov bl, 1
    call cand_add
.bnext:
    add di, 2
    cmp di, 6
    jb .bios
    push cs
    pop es
    xor si, si
.std:
    mov ax, [std_base+si]
    xor bl, bl
    call cand_add
    add si, 2
    cmp si, 6
    jb .std
    mov cl, [ncand]
    or  cl, cl
    jz .out
    xor ch, ch
    xor di, di
.probe:
    push cx
    mov bx, di
    shl bx, 1
    mov ax, [cand_base+bx]
    call lp_latch
    mov al, 1
    jnc .pyes
    xor al, al
.pyes:
    mov bx, di
    mov [cand_ok+bx], al
    inc di
    pop cx
    loop .probe
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret


; --- state, and the candidate list --------------------------------------------
; `db 0` in the image and not a bss: -f bin zeroes nothing, a driver has no bss
; at all (os88drv.inc), and lptlink is a flat binary either way.
ncand:      db 0
cand_base:  times MAXCAND dw 0
cand_bios:  times MAXCAND db 0
cand_ok:    times MAXCAND db 0

lp_base:    dw 0
lp_stat:    dw 0
lp_ctrl:    dw 0
lp_lastop:  db 0                ; 1 = the last link op was a RECEIVE
lp_turnw:   db TURN_RX          ; ticks a REVERSAL will wait - see REPLY_TMO.
                                ; lp_init puts it back to the SWEEP's figure,
                                ; so patience is something a linked master has
                                ; to ask for and can never be left switched on
                                ; across a port scan
lp_svdat:   db 0
lp_svctl:   db 0
lp_ver:     db 0
lp_myver:   db PROTO_VER        ; ...what WE answer with (see PROTO_VER)
lp_dlset:   db 0
dl_wf:      dw 0

; The three the POST itself scans, in its order. 3BC is the MDA/Hercules
; family's - the GB101 in the calibration machine - and 378 is the DOS box's
; DIO-500 (docs/FIELD-MACHINES.md, both confirmed by a field run). They
; DIFFER, which is why this works in ADDRESSES and never in LPT numbers: on a
; mono machine LPT1 IS 3BC (NET-PLAN 1.4.1).
std_base:   dw 0x3BC, 0x378, 0x278

magic_q:    db 'O','8','8','?'  ; master -> slave
magic_r:    db 'O','8','8','!'  ; slave -> master, then a version byte
