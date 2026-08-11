; =============================================================================
; lptlink - what is on this machine's parallel ports, and how fast is the cable
;
; Step 1 of docs/NET-PLAN.md, and it is deliberately ONE artifact rather than
; the two the plan first proposed (a port survey and a throughput benchmark).
; They want the same code and the same trip: a survey that stops at "there is
; a latch here" cannot answer the question that actually matters - is there a
; COMPUTER on the other end - and answering that means implementing the
; handshake, at which point the throughput figure is a loop around it.
;
; It answers, from the machine itself:
;
;   1. which parallel ports does the BIOS say it found (0040:0008), and which
;      of 3BC / 378 / 278 answer a real probe? The two disagreeing is itself
;      an answer - comscan's warn_outside, in the other connector. FIELD
;      RESULT: the 5150's GB101 is at 3BC and the DOS machine's DIO-500 is at
;      378, which is why neither end of this may assume an address
;      (docs/NET-PLAN.md 1.4.1)
;   2. is there a LapLink cable on one of them with a LIVE PARTNER - as
;      against nothing, or a printer, both of which read as a CONSTANT status
;      byte where a partner reads as whatever we ask it to (NET-PLAN 1.4.4)
;   3. how many bytes a second does it move, EACH WAY, measured on the machine
;      rather than modelled - which is the number NET-PLAN 1.2 does not have
;      and PERFORMANCE.md Part 6 rule 8 will not accept a model for
;
; NEITHER END IS os8088. No kernel byte moves, no driver exists yet, and a
; failure here is a failure of the cable or the protocol and cannot be
; anything else. It is built two ways from this one source, which is
; comscan's shape:
;
;   lptlink.com   a DOS program. Output goes through int 21h, so
;                 `LPTLINK > LPTLINK.TXT` captures the report to a file.
;   lptlink.img   a BOOTABLE 360KB floppy. No DOS, no os8088, no mouse -
;                 the machine boots straight into this. Output via int 10h.
;
; Run it on BOTH machines: one Slave, one Master. START THE SLAVE FIRST - it
; listens, and the Master is the one that sweeps and calls (NET-PLAN 1.3:
; os8088 is the master and never receives unsolicited data).
;
; NOTHING here writes to a disk.
;
; Assemble: nasm -f bin -DCOMFILE -o lptlink.com lptlink.asm   (DOS)
;           nasm -f bin           -o lptlink.bin lptlink.asm   (bootable)
; =============================================================================

    cpu 8086                    ; the 5150 is the point of this

%ifdef COMFILE
    org 0x100
%else
    org 0                       ; loaded at KERNEL_SEG:0000 by boot/boot.asm
%endif

MAXCAND     equ 8               ; the BIOS table (3) plus the three standard
                                ; bases, deduped - so usually 3 or 4
PROTO_VER   equ 1               ; what the reply carries, so a mismatched pair
                                ; says so instead of desyncing

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
                                ; machine. See lp_rnib
CMD_WAIT    equ 180             ; ~10s: how long the slave waits for the next
                                ; command, so the master may print between them

; --- the benchmark -----------------------------------------------------------
; 64 x 256 = 16KB, which is ~1.6s at the 10KB/s NET-PLAN 1.2 models and ~5s at
; a pessimistic 3KB/s. The tick is 54.9ms, so either way the measurement has
; 2-4% resolution - and the report prints the TICK COUNT beside the rate so a
; reader can see that for themselves rather than taking it on trust.
BENCH_BLKS  equ 64
BLK_SIZE    equ 256
BENCH_BYTES equ BENCH_BLKS*BLK_SIZE

; THE CLOCK STARTS AFTER BLOCK 0, and the reason is lp_turn: a direction
; reversal costs a whole system tick by construction, and on the inbound leg
; that guard runs on the FAR side while this one sits in its receive - so
; timing from the first byte would charge the link 55-110ms it did not spend
; moving data. Block 0 is exchanged untimed and the figure is the 63 after it.
TIMED_BYTES equ (BENCH_BLKS-1)*BLK_SIZE

; bytes/second = bytes * 18.2065 / ticks, done as bytes*182 / (ticks*10) in 32
; bits. The numerator is a constant, so it costs nothing at run time.
RATE_NUM    equ TIMED_BYTES*182
RATE_HI     equ (RATE_NUM >> 16) & 0xFFFF
RATE_LO     equ RATE_NUM & 0xFFFF

; --- the hello magic, as a NIBBLE sequence ----------------------------------
; 'O','8','8','?' = 4F 38 38 3F, sent low nibble first, so the wire carries
; F,4,8,3,8,3,F,3 - and the slave hunts for exactly that in a sliding 8-nibble
; window (slv_hunt). A window at NIBBLE granularity rather than byte
; granularity is what makes a slave that joined mid-transfer resynchronise:
; the same idea as SPEC.md 9.5's mouse resync, one level down.
MAGQ_HI     equ 0xF483
MAGQ_LO     equ 0x83F3

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
    call survey

.menu:
    call crlf
    mov si, s_menu
    call puts
.key:
    call getkey
    or  al, 0x20                ; fold case
    cmp al, 'm'
    je  .master
    cmp al, 's'
    je  .slave
    cmp al, 'r'
    je  .again
    cmp al, 'q'
    je  .quit
    jmp short .key

.again:
    call crlf
    call survey
    jmp short .menu

.master:
    call crlf
    call role_master
    jmp short .menu

.slave:
    call crlf
    call role_slave
    jmp short .menu

.quit:
    call crlf
%ifdef COMFILE
    mov ax, 0x4C00
    int 0x21
%else
    mov si, s_halt
    call puts
.hang:
    hlt
    jmp short .hang
%endif

; =============================================================================
; THE SURVEY
; =============================================================================

; -----------------------------------------------------------------------------
; survey - build the candidate list, probe every entry, print the table
;
; The list is the BIOS's own table PLUS the three standard bases, deduped. The
; table is TRUSTED here in a way int 11h's floppy count is not (NET-PLAN
; 1.4.1): the POST fills it by PROBING - a write and a read-back, the same
; test lp_latch does below - where the floppy count comes from SW1's DIP
; switches, which is a human's assertion and on the calibration machine a
; wrong one (SPEC.md 18.97). It is still verified, because a clone BIOS may
; scan fewer addresses and a card can sit where the POST never looks.
; -----------------------------------------------------------------------------
survey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov byte [ncand], 0

    ; --- what the POST found: 0040:0008, 000A, 000C - THREE words, 0 = absent
    ;
    ; NOT FOUR. 0040:000E is LPT4 only on a pre-PS/2 BIOS; on everything since
    ; it is the SEGMENT OF THE EBDA, and the field machine duly reported a
    ; parallel port at `9FC0` - which is 0x9FC00, an EBDA sitting just under
    ; 640KB, and not an I/O address at all. The probe rejected it (its latch
    ; column read `--`) so the fail-safe held, but the tool had already WRITTEN
    ; 0xAA and 0x55 to I/O port 9FC0h to find that out, which is exactly the
    ; unprovoked write to an unknown port that NET-PLAN 1.4.3 exists to forbid.
    ;
    ; The range test is the belt to that brace: a parallel port lives low in
    ; ISA I/O space, so anything at or above 0x400 is not one whatever the
    ; table says. A garbage BIOS can put nonsense in the first three too.
    mov ax, 0x0040
    mov es, ax
    xor di, di
.bios:
    mov ax, [es:di+8]
    or  ax, ax
    jz .bnext                   ; absent
    cmp ax, 0x0400
    jae .bnext                  ; not an I/O port - see above
    mov bl, 1                   ; BIOS-listed
    call cand_add
.bnext:
    add di, 2
    cmp di, 6
    jb .bios

    push cs                     ; done with the BIOS data area
    pop es

    ; --- ...and the three the POST itself scans, in its order ----------------
    xor si, si
.std:
    mov ax, [std_base+si]
    xor bl, bl                  ; not BIOS-listed - cand_add ORs the flag in if
    call cand_add               ; the address is already on the list
    add si, 2
    cmp si, 6
    jb .std

    ; --- probe every one of them ---------------------------------------------
    mov cl, [ncand]
    or  cl, cl
    jz .none
    xor ch, ch
    xor di, di
.probe:
    push cx
    mov bx, di
    shl bx, 1
    mov ax, [cand_base+bx]
    call lp_latch               ; CF=0 = a real latch answered
    mov al, 1
    jnc .pyes
    xor al, al
.pyes:
    mov bx, di
    mov [cand_ok+bx], al
    inc di
    pop cx
    loop .probe

    ; --- the table ------------------------------------------------------------
    mov si, s_phead
    call puts
    mov cl, [ncand]
    xor ch, ch
    xor di, di
.row:
    push cx
    mov si, s_ind
    call puts
    mov bx, di
    shl bx, 1
    mov ax, [cand_base+bx]
    call puthex16
    mov si, s_gap
    call puts
    mov bx, di
    mov al, [cand_bios+bx]
    call put_yn
    mov si, s_gap
    call puts
    mov bx, di
    mov al, [cand_ok+bx]
    call put_ok
    mov si, s_gap
    call puts
    mov bx, di                  ; the raw status and control bytes, printed
    shl bx, 1                   ; whatever the latch said - an absent port
    mov dx, [cand_base+bx]      ; reading FF FF is exactly as informative as a
    inc dx                      ; live one, and it is what a reader compares
    in  al, dx                  ; against the next machine's report
    call puthex8
    call putsp
    inc dx
    in  al, dx
    call puthex8
    call crlf
    inc di
    pop cx
    loop .row
    jmp short .out
.none:
    mov si, s_noports
    call puts
.out:
    pop es
    pop di
    pop si
    pop dx
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
    call ticks
    mov bx, ax
.e1:
    call ticks
    cmp ax, bx
    je  .e1
    mov bx, ax
.e2:
    call ticks
    cmp ax, bx
    je  .e2
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; lp_wfar - wait for the far end's D4 to reach a level, bounded IN TICKS
; in:  AH = 0x80 wait for high, 0x00 wait for low;  AL = ticks to allow
; out: CF=0 and AL = the XORed status byte that satisfied it - so a nibble is
;      sampled by the very read that detected the strobe, and there is no
;      window in which the sender could have moved on. CF=1 = the deadline.
; clobbers: AX, CF
;
; ONE routine for all four waits in the handshake, and the deadline is in
; TICKS for the reason LP_TMO gives: a poll count means different things on
; the two ends of this cable, and a time does not.
; -----------------------------------------------------------------------------
lp_wfar:
    push cx
    push dx
    mov [lp_wlvl], ah
    xor ah, ah
    push ax                     ; the tick allowance
    call ticks
    pop cx
    add ax, cx
    mov [dl_wf], ax
    mov dx, [lp_stat]
.pass:
    mov cx, LP_SPIN
.poll:
    in  al, dx
    xor al, 0x80                ; BUSY is inverted, and it is the top bit
    mov ah, al
    and ah, 0x80
    cmp ah, [lp_wlvl]
    je  .got
    loop .poll
    call ticks                  ; LP_SPIN polls without an answer: is the
    sub ax, [dl_wf]             ; DEADLINE up? (modular - SPEC.md 45.15's js)
    js  .pass
    stc
    jmp short .out
.got:
    clc
.out:
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; lp_snib - send AL's low nibble
; out: CF=0 acked, CF=1 timed out
; clobbers: CF
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
    mov ah, 0x80
    mov al, LP_TMO
    call lp_wfar                ; ...wait for the ack to RISE
    jc  .bad
    mov dx, [lp_base]
    mov al, bl
    out dx, al                  ; strobe down, nibble held
    mov ah, 0
    mov al, LP_TMO
    call lp_wfar                ; ...and to FALL. Only then is the nibble
    jnc .done                   ; complete at BOTH ends
.bad:
    mov dx, [lp_base]
    mov al, bl
    out dx, al                  ; leave the strobe DOWN however we left, or
    stc                         ; the next attempt starts mid-handshake
    jmp short .out
.done:
    clc
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
    mov al, LP_TMO
    cmp byte [lp_lastop], 1
    je  .lvl                    ; already receiving: the ordinary wait
    mov al, TURN_RX             ; a REVERSAL: the far end is sitting inside
.lvl:                           ; lp_turn spending a whole tick, so this end
    mov ah, 0x80                ; has to be patient exactly there and nowhere
    call lp_wfar                ; else
    jc  .bad
    shr al, 1                   ; far D3..D0 are status bits 6..3, sampled by
    shr al, 1                   ; the read that saw the strobe
    shr al, 1
    and al, 0x0F
    mov bl, al
    mov dx, [lp_base]
    mov al, 0x10
    out dx, al                  ; ack up
    mov ah, 0
    mov al, LP_TMO
    call lp_wfar                ; wait for the strobe to fall
    jc  .bad
    mov dx, [lp_base]
    xor al, al
    out dx, al                  ; ack down - both ends idle again
    mov al, bl
    clc
    jmp short .out
.bad:
    mov dx, [lp_base]
    xor al, al
    out dx, al                  ; idle on the way out of a timeout too
    stc
.out:
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
; lp_rbyte_w - lp_rbyte, but waits CMD_WAIT ticks for the FIRST nibble
;
; The second nibble keeps the ordinary short bound, which is the point: a
; retry loop around a whole byte would resume in the middle of one and desync
; the phase. Only the gap BETWEEN bytes may be long.
; -----------------------------------------------------------------------------
lp_rbyte_w:
    push bx
    call ticks
    add ax, CMD_WAIT
    mov [dl_cmd], ax
.w:
    call lp_rnib
    jnc .got
    call kbhit
    jc .bad
    call ticks
    sub ax, [dl_cmd]            ; modular compare (SPEC.md 45.15's js, not jg)
    js  .w
    jmp short .bad
.got:
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
    stc
    pop bx
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

; =============================================================================
; THE MASTER - sweeps, calls, benchmarks
; =============================================================================

role_master:
    push ax
    push bx
    push cx
    push si
    push di
    mov si, s_msweep
    call puts
    mov cl, [ncand]
    or  cl, cl
    jz .noports
    xor ch, ch
    xor di, di
.try:
    push cx
    mov bx, di
    cmp byte [cand_ok+bx], 0
    je  .next                   ; no latch there: do not call into nothing
    shl bx, 1
    mov si, s_trying
    call puts
    mov ax, [cand_base+bx]
    call puthex16
    mov ax, [cand_base+bx]
    call lp_setport
    call lp_init
    call mst_hello
    jc  .nope
    mov si, s_linked
    call puts
    mov al, [lp_ver]
    call puthex8
    call crlf
    call mst_bench
    call lp_restore
    pop cx
    jmp short .out
.nope:
    call lp_restore
    mov si, s_noans
    call puts
.next:
    inc di
    pop cx
    loop .try
    mov si, s_nolink
    call puts
    jmp short .out
.noports:
    mov si, s_noports
    call puts
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
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
; mst_bench - 16KB out, 16KB back, each timed and verified
; -----------------------------------------------------------------------------
mst_bench:
    push ax
    push bx
    push cx
    push si

    ; --- outbound -------------------------------------------------------------
    mov si, s_bout
    call puts
    mov al, 'B'
    call lp_sbyte
    jc  .fail
    mov ax, BENCH_BLKS
    call lp_sword
    jc  .fail
    mov word [bk_blk], 0
.oblk:
    cmp word [bk_blk], 1        ; the clock starts after block 0 - see
    jne .onot0                  ; TIMED_BYTES
    call ticks
    mov [t0], ax
.onot0:
    mov cx, BLK_SIZE
    xor bx, bx
.obyt:
    mov al, bl
    add al, [bk_blk]            ; the pattern: (block + index) & FF
    call lp_sbyte
    jc  .fail
    inc bl
    loop .obyt
    inc word [bk_blk]
    mov ax, [bk_blk]
    cmp ax, BENCH_BLKS
    jb  .oblk
    call ticks
    sub ax, [t0]
    mov [dt], ax
    call lp_rword               ; what the far end made of it
    jc  .fail
    mov [bk_err], ax
    call report

    ; --- inbound --------------------------------------------------------------
    mov si, s_bin
    call puts
    mov al, 'R'
    call lp_sbyte
    jc  .fail
    mov ax, BENCH_BLKS
    call lp_sword
    jc  .fail
    mov word [bk_err], 0
    call ticks
    mov [t0], ax
    mov word [bk_blk], 0
.iblk:
    cmp word [bk_blk], 1
    jne .inot0
    call ticks
    mov [t0], ax
.inot0:
    mov cx, BLK_SIZE
    xor bx, bx
.ibyt:
    call lp_rbyte
    jc  .fail
    mov ah, bl
    add ah, [bk_blk]
    cmp al, ah
    je  .iok
    inc word [bk_err]
.iok:
    inc bl
    loop .ibyt
    inc word [bk_blk]
    mov ax, [bk_blk]
    cmp ax, BENCH_BLKS
    jb  .iblk
    call ticks
    sub ax, [t0]
    mov [dt], ax
    call report

    mov al, 'X'                 ; release the far end
    call lp_sbyte
    jmp short .out
.fail:
    mov si, s_bfail
    call puts
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; report - [dt] ticks and [bk_err] errors for BENCH_BYTES bytes
;
; The TICK COUNT is printed beside the rate on purpose: the tick is 54.9ms, so
; a reader can see the measurement's own resolution instead of taking the
; bytes/second on trust. PERFORMANCE.md Part 6 rule 8's habit, in miniature.
; -----------------------------------------------------------------------------
report:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, TIMED_BYTES
    call putdec16
    mov si, s_rticks
    call puts
    mov ax, [dt]
    call putdec16
    mov si, s_rrate
    call puts
    mov ax, [dt]
    or  ax, ax
    jz  .fast
    mov cx, 10
    mul cx                      ; DX:AX = ticks*10 - and DX is 0, because a
    mov cx, ax                  ; benchmark that ran for 6553 ticks is 6 min
    mov ax, RATE_LO
    mov dx, RATE_HI
    call div32
    call putdec32
    jmp short .err
.fast:
    mov si, s_rfast
    call puts
.err:
    mov si, s_rerr
    call puts
    mov ax, [bk_err]
    call putdec16
    call crlf
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE SLAVE - listens, answers, serves
; =============================================================================

role_slave:
    push ax
    push bx
    push cx
    push si
    push di
    mov byte [slv_abort], 0
    mov si, s_slisten
    call puts
    mov cl, [ncand]
    or  cl, cl
    jz  .noports
.cycle:
    mov cl, [ncand]
    xor ch, ch
    xor di, di
.one:
    push cx
    mov bx, di
    cmp byte [cand_ok+bx], 0
    je  .next
    shl bx, 1
    mov ax, [cand_base+bx]
    call lp_setport
    call lp_init
    call slv_hunt
    jc  .quiet
    mov si, s_scalled
    call puts
    mov ax, [lp_base]
    call puthex16
    call crlf
    call slv_reply
    jc  .drop
    call slv_serve
.drop:
    call lp_restore
    pop cx
    mov si, s_sdone
    call puts
    jmp short .out
.quiet:
    call lp_restore
    cmp byte [slv_abort], 0
    jne .stopped
.next:
    inc di
    pop cx
    loop .one
    jmp short .cycle
.stopped:
    pop cx
    mov si, s_sstop
    call puts
    jmp short .out
.noports:
    mov si, s_noports
    call puts
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; slv_hunt - dwell on the current port waiting to be called
; out: CF=0 the magic arrived, CF=1 the dwell expired or a key was pressed
;      ([slv_abort] = 1 for the second)
;
; The window is EIGHT NIBBLES, not four bytes, and that is what makes a slave
; which joined in the middle of a transfer resynchronise: a byte-granular
; window cannot recover from being half a byte out of step. SPEC.md 9.5's
; mouse resync, one level down.
; -----------------------------------------------------------------------------
slv_hunt:
    push ax
    push cx
    mov word [win_lo], 0
    mov word [win_hi], 0
    call ticks
    add ax, SLAVE_DWELL
    mov [dl_hunt], ax
.loop:
    call kbhit
    jc  .key
    call ticks
    sub ax, [dl_hunt]           ; modular compare, so midnight is not a bug
    jns .expired
    call lp_rnib
    jc  .loop                   ; nothing yet - the dwell is what bounds this
    mov cl, 4
.sh:
    shl word [win_lo], 1
    rcl word [win_hi], 1
    dec cl
    jnz .sh
    or  byte [win_lo], al
    mov ax, [win_hi]
    cmp ax, MAGQ_HI
    jne .loop
    mov ax, [win_lo]
    cmp ax, MAGQ_LO
    jne .loop
    clc
    jmp short .out
.key:
    cmp al, 27                  ; ESC stops; any other key just moves on
    jne .expired
    mov byte [slv_abort], 1
.expired:
    stc
.out:
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; slv_reply - answer the magic, and say which protocol this is
; -----------------------------------------------------------------------------
slv_reply:
    push ax
    push cx
    push si
    mov si, magic_r
    mov cx, 4
.s:
    mov al, [si]
    inc si
    call lp_sbyte
    jc  .bad
    loop .s
    mov al, PROTO_VER
    call lp_sbyte
    jc  .bad
    clc
    jmp short .out
.bad:
    stc
.out:
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; slv_serve - the command loop. 'B' take a run, 'R' send one, 'X' finished
; -----------------------------------------------------------------------------
slv_serve:
    push ax
    push bx
    push cx
    push si
.cmd:
    call lp_rbyte_w
    jc  .out
    cmp al, 'B'
    je  .take
    cmp al, 'R'
    je  .give
    cmp al, 'X'
    je  .out
    jmp short .cmd

.take:
    mov si, s_stake
    call puts
    call lp_rword
    jc  .out
    mov [bk_n], ax
    mov word [bk_err], 0
    mov word [bk_blk], 0
.tblk:
    mov cx, BLK_SIZE
    xor bx, bx
.tbyt:
    call lp_rbyte
    jc  .out
    mov ah, bl
    add ah, [bk_blk]
    cmp al, ah
    je  .tok
    inc word [bk_err]
.tok:
    inc bl
    loop .tbyt
    inc word [bk_blk]
    mov ax, [bk_blk]
    cmp ax, [bk_n]
    jb  .tblk
    mov ax, [bk_err]
    call lp_sword
    jc  .out
    mov si, s_sdot
    call puts
    jmp short .cmd

.give:
    mov si, s_sgive
    call puts
    call lp_rword
    jc  .out
    mov [bk_n], ax
    mov word [bk_blk], 0
.gblk:
    mov cx, BLK_SIZE
    xor bx, bx
.gbyt:
    mov al, bl
    add al, [bk_blk]
    call lp_sbyte
    jc  .out
    inc bl
    loop .gbyt
    inc word [bk_blk]
    mov ax, [bk_blk]
    cmp ax, [bk_n]
    jb  .gblk
    mov si, s_sdot
    call puts
    jmp .cmd
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; ARITHMETIC AND OUTPUT
; =============================================================================

; -----------------------------------------------------------------------------
; div32 - DX:AX / CX -> DX:AX
;
; Two 16-bit divides, the first one's remainder becoming the second's high
; half. Neither can overflow: the first has a zero high word, and the second's
; dividend high half is a remainder and so is already less than CX. That
; matters here because a single `div` of 2,981,888 by a small tick count
; DIVIDE-FAULTS, and a fast link is exactly when the tick count is small.
; -----------------------------------------------------------------------------
div32:
    push bx
    push ax
    mov ax, dx
    xor dx, dx
    div cx                      ; AX = high quotient, DX = remainder
    mov bx, ax
    pop ax
    div cx                      ; AX = low quotient
    mov dx, bx
    pop bx
    ret

; div10 - DX:AX / 10 -> DX:AX, BX = the remainder
div10:
    push cx
    mov cx, 10
    push ax
    mov ax, dx
    xor dx, dx
    div cx
    mov bx, ax
    pop ax
    div cx
    mov cx, dx
    mov dx, bx
    mov bx, cx
    pop cx
    ret

putdec32:                       ; DX:AX, no leading zeros
    push ax
    push bx
    push cx
    push dx
    xor cx, cx
.d:
    call div10
    push bx
    inc cx
    mov bx, ax
    or  bx, dx
    jnz .d
.o:
    pop ax
    add al, '0'
    call putc
    loop .o
    pop dx
    pop cx
    pop bx
    pop ax
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

ticks:                          ; out AX = the BIOS tick counter
    push es
    push bx
    mov bx, 0x0040
    mov es, bx
    mov ax, [es:0x006C]
    pop bx
    pop es
    ret

; getkey - wait for a key and RETURN IT
; out: AL = ASCII, AH = the scan code
;
; IT MUST NOT PRESERVE AX, and that is the whole comment. comscan's getkey is
; `push ax / xor ax,ax / int 16h / pop ax / ret` - correct there, because it is
; a "press any key to continue" pause and the key is not wanted. Copied into a
; MENU it is a program that reads a keystroke, throws it away, restores the
; caller's AX, matches nothing, and loops - so it sits at its prompt EATING
; every key while looking hung. Reported off both field machines as "neither
; end responded to keyboard input (might be frozen?)". It was not frozen.
getkey:
    xor ax, ax
    int 0x16
    ret

; kbhit - CF=1 and AL = the key if one is waiting (it is consumed), else CF=0
kbhit:
    mov ah, 1
    int 0x16
    jz  .none
    xor ax, ax
    int 0x16
    stc
    ret
.none:
    xor ax, ax
    clc
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

puts:                           ; DS:SI, NUL-terminated
    push ax
    push si
.l:
    lodsb
    or  al, al
    jz  .out
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
    push ax
    call puthex8
    pop ax
    ret

put_yn:                         ; AL = 0/1
    push si
    mov si, s_no
    or  al, al
    jz  .say
    mov si, s_yes
.say:
    call puts
    pop si
    ret

put_ok:                         ; AL = 0/1
    push si
    mov si, s_dash
    or  al, al
    jz  .say
    mov si, s_ok
.say:
    call puts
    pop si
    ret

; =============================================================================
; DATA
; =============================================================================

s_title:
    db 13,10,'lptlink - parallel port survey and cable benchmark',13,10
    db 'docs/NET-PLAN.md step 1.  Neither end of this is os8088.',13,10,13,10,0
s_phead:
    db '  base  bios  latch  stat ctrl',13,10,0
s_ind:      db '  ',0
s_gap:      db '   ',0
s_yes:      db ' yes',0
s_no:       db '  no',0
s_ok:       db '   ok',0
s_dash:     db '   --',0
s_noports:  db '  no parallel port found',13,10,0
s_menu:
    db '[M]aster - sweep and call    [S]lave - listen and answer',13,10
    db '[R]escan  [Q]uit             START THE SLAVE FIRST',13,10,'> ',0
s_msweep:   db 'Master: sweeping the ports that answered.',13,10,0
s_trying:   db '  calling ',0
s_noans:    db '  - no answer',13,10,0
s_linked:   db '  - LINKED, partner protocol ',0
s_nolink:   db '  no partner found. Is the slave running, and started first?',13,10,0
s_bout:     db '  out: ',0
s_bin:      db '  in : ',0
s_bfail:    db '  link lost mid-benchmark',13,10,0
s_rticks:   db ' bytes in ',0
s_rrate:    db ' ticks = ',0
s_rfast:    db '(under one tick - too fast to time here)',0
s_rerr:     db ' bytes/sec, errors ',0
s_slisten:
    db 'Slave: listening on every port that answered. ESC stops.',13,10,0
s_scalled:  db '  called on ',0
s_stake:    db '  <- ',0
s_sgive:    db '  -> ',0
s_sdot:     db 'ok',13,10,0
s_sdone:    db '  master finished.',13,10,0
s_sstop:    db '  stopped.',13,10,0
%ifndef COMFILE
s_halt:     db 'halted - power off or reset.',13,10,0
%endif

magic_q:    db 'O','8','8','?'  ; master -> slave
magic_r:    db 'O','8','8','!'  ; slave -> master, then PROTO_VER

; The three the POST itself scans, in its order. 3BC is the MDA/Hercules
; family's - the GB101 in the calibration machine (docs/FIELD-MACHINES.md), and
; the reason this tool works in ADDRESSES and never in LPT numbers: on a mono
; machine LPT1 IS 3BC and a card at 378 is LPT2 (NET-PLAN 1.4.1).
std_base:   dw 0x3BC, 0x378, 0x278

; --- state. `db 0` in the image rather than a bss, because -f bin zeroes
; --- nothing and this is a flat binary either way.
ncand:      db 0
cand_base:  times MAXCAND dw 0
cand_bios:  times MAXCAND db 0
cand_ok:    times MAXCAND db 0

lp_base:    dw 0
lp_stat:    dw 0
lp_ctrl:    dw 0
lp_lastop:  db 0                ; 1 = the last link op was a RECEIVE
lp_svdat:   db 0
lp_svctl:   db 0
lp_ver:     db 0

win_lo:     dw 0
win_hi:     dw 0
dl_hunt:    dw 0
dl_cmd:     dw 0
dl_turn:    dw 0
dl_wf:      dw 0
lp_wlvl:    db 0
slv_abort:  db 0

t0:         dw 0
dt:         dw 0
bk_n:       dw 0
bk_blk:     dw 0
bk_err:     dw 0
