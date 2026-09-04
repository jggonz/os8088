; =============================================================================
; os8088 - drivers/ether/ethprof.inc - WHERE THE TIME ACTUALLY GOES (SPEC.md 72.15)
;
; The stack was measured at 7 KB/s with the window at its 8192-byte ceiling
; (SPEC.md 72.13.2), which means the window stopped being the constraint and
; nobody could say what had replaced it. Every answer offered after that point
; was arithmetic over a per-byte cost NOBODY HAS EVER MEASURED -
; PERFORMANCE.md Part 6 rule 4 in its exact failure mode, twice in one week.
;
; This is the instrument. Ten stages, each bracketed at its entry and its exit,
; each accumulating THREE numbers: how long, how many bytes, how many calls.
; Divide the first by the second and the answer is us/byte for that stage,
; measured on the machine the question is about.
;
; --- WHY IT IS IN THE DRIVER AND NOT IN A TEST PACKAGE ----------------------
; MartyPC has no network card of any kind (CLAUDE.md), so the one instrument
; this project trusts for time cannot host this driver at all. QEMU can, and
; QEMU is exact about work and useless about time. That leaves the machine
; itself - an 86Box XT, or the 5150 - and on those there is no debugger and no
; automation socket, so the measurement has to be taken BY the guest and read
; ON the guest. tests/netbench is the window that reads it.
;
; What that buys is the whole point: the numbers come off the same 4.77MHz
; machine the 7 KB/s came off, during the same FTP transfer.
;
; --- THE CLOCK --------------------------------------------------------------
; Counter 0 of the 8253, latched, exactly as tests/benchlib.inc reads it: one
; count is 838ns and it reloads every 55ms. A stage that ran longer than one
; reload would be reported modulo 55ms - a small plausible number, which is
; the failure benchlib's header spends a page on - so the count is extended
; upward by the BIOS tick at 0040:006C, which our own int 08h hook chains. One
; tick is EXACTLY 65536 counts, so the two halves are one 32-bit number in one
; unit, and a stage may run for eighteen hours before it wraps.
;
; The tick is read BEFORE and AFTER the counter and the pair is taken again if
; it moved. Without that, a bracket landing on the reload takes the old tick
; with the freshly reloaded counter and reports 55ms that did not happen.
;
; **A BRACKET THAT SPANS A cli LONGER THAN 55ms UNDER-REPORTS**, because the
; tick cannot advance while the ISR cannot run. Nothing here holds interrupts
; off for anything like that, and it is written down because the failure would
; otherwise be silent.
;
; --- THE ACTIVE WINDOW, AND WHY THE WALL CLOCK IS NOT ENOUGH ----------------
; The wall runs from the S key to the X key, and a HUMAN presses both: free
; the mouse from the emulator, start the client, watch for it to finish, click
; back in, press X. That is seconds of nothing at each end, and it lands in
; every percentage in the report.
;
; It showed up the first time the field profiled the same transfer twice: the
; driver's own total fell 8,670ms and the wall fell 11,515ms, and the missing
; 2,845ms was not a change in anything - it was a person being slightly
; quicker with the keyboard.
;
; So the block carries an ACTIVE window as well: prof_end stamps the time of
; the FIRST byte any stage moves and of the LAST, and the difference is the
; transfer with the waiting cut off both ends. It costs one compare per
; bracket - `or cx, cx` - because a stage that moved no bytes neither opens
; the window nor closes it, which is exactly what an idle poll does.
;
; --- WHAT IT COSTS ----------------------------------------------------------
; A bracket is a latch, two INs, two word reads and the accumulate: about 13us,
; twice per stage entry, plus the wrapper's own call and ret (11us,
; PERFORMANCE.md Part 2). Call it 37us. The stages are per FRAME and per VERB,
; never per byte, so a 200KB transfer is a few thousand of them - under half a
; percent of a run that takes half a minute. When [prof_on] is 0 a bracket is
; one compare and a return, and that is what ships.
;
; It is ALWAYS COMPILED IN and switched at runtime, on purpose. A profiler that
; needs a rebuild is a profiler the person with the hardware cannot use, and
; the person with the hardware is the only one who can answer this question.
; =============================================================================

PF_PUMP     equ 0               ; eth_pump: the card drained, budget and timers
PF_CARD     equ 1               ; ne_ring_read: the card's ring -> our frame
PF_FRAME    equ 2               ; eth_frame: one frame dispatched, whole
PF_CKSUM    equ 3               ; ip_ck_part: the ones-complement sum
PF_RXPUT    equ 4               ; sk_rxput: frame payload -> the socket ring
PF_RXGET    equ 5               ; sk_rxget: the socket ring -> the caller
PF_TXIN     equ 6               ; ring_in: the caller -> the send ring
PF_TXOUT    equ 7               ; ring_out: the send ring -> a frame
                                ; **4..7 ARE CONTIGUOUS AND THAT IS LOAD-
                                ; BEARING**: they are the four stages that
                                ; move APPLICATION bytes, and prof_end tests
                                ; the active window's membership with two
                                ; compares against the ends of that run. A
                                ; stage inserted between them changes what
                                ; ACTIVE means without changing a line of
                                ; the test
PF_TX       equ 8               ; ne_tx: our frame -> the card
PF_VERB     equ 9               ; eth_pkg: one socket verb, whole
PF_N        equ 10

PF_REC      equ 18              ; per stage in the report block: dd time,
                                ; dd bytes, dw calls, 8 bytes of name
PF_HDR      equ 12              ; 'PF', version, PF_N, dd wall, dd ACTIVE
PF_BLK      equ PF_HDR + PF_N * PF_REC
PF_CLR      equ PF_N * 14 + 13  ; what prof_start zeroes: the four per-stage
                                ; arrays, the banked wall and the active
                                ; window's two stamps and its flag - all
                                ; contiguous, and in that order

%ifdef ETH_NOEMIT
; --- the DOS partner (SPEC.md 62.11.1) carries none of this -----------------
; It shares every line of inet.inc and tcp.inc and is a .COM with one segment
; to live in, so the aliases below send its callers straight at the bodies and
; not one byte of the profiler is emitted. ne2000.inc is not compiled there at
; all - pktdrv.inc stands in its place and brings its own ne_tx - so the two
; card stages have no alias here and must not have one.
%macro PROF_B 1
%endmacro
%macro PROF_E 2
%endmacro
%define eth_pump   eth_pump_i
%define eth_frame  eth_frame_i
%define ip_ck_part ip_ck_part_i
%define sk_rxput   sk_rxput_i
%define sk_rxget   sk_rxget_i
%define ring_in    ring_in_i
%define ring_out   ring_out_i
%elifndef ETHPROF
; --- AND THE SHIPPED DRIVER DOES NOT CARRY IT EITHER (SPEC.md 72.16.4) ------
; The profiler is an INSTRUMENT, and `make netbench` is where it belongs:
; ETHPROF=1 builds the driver that NETBENCH.O88 can read, and every other build
; sends each caller straight at the body. The reason is not the driver's size,
; though it saves that too - it is the STACK. `prof_end` sits at the bottom of
; the wire path and calls `pit_now` under it, and each wrapper is a call level
; of its own, so the ten stages cost TWELVE BYTES on the deepest chain in a
; 384-byte task slice (docs/KERNEL-MEMORY.md, "Task stacks").
;
; ALL NINE aliases here, unlike the DOS partner above: this build has ne2000.inc
; in it, so the two card stages need one and would otherwise be undefined.
%macro PROF_B 1
%endmacro
%macro PROF_E 2
%endmacro
%define eth_pump      eth_pump_i
%define ne_ring_read  ne_ring_read_i
%define eth_frame     eth_frame_i
%define ip_ck_part    ip_ck_part_i
%define sk_rxput      sk_rxput_i
%define sk_rxget      sk_rxget_i
%define ring_in       ring_in_i
%define ring_out      ring_out_i
%define ne_tx         ne_tx_i

; NETV_PROF still ANSWERS, and it refuses with NETE_VERB - "a verb this driver
; does not have", which is exactly true of this build and needs no new code in
; the ABI. A verb missing from the table entirely reads to a package as an old
; driver; this reads as "no profiler here", which is what NETBENCH.O88 has to
; say to whoever is looking at an empty table.
eth_v_prof:
    mov ax, NETE_VERB
    stc
    ret
%else

; --- the brackets ------------------------------------------------------------
; EVERY REGISTER AND THE FLAGS SURVIVE BOTH, which is what lets a wrapper sit
; around a routine whose CF is its answer and whose CX is its byte count.
%macro PROF_B 1
    push ax
    mov al, %1
    call prof_beg
    pop ax
%endmacro
%macro PROF_E 2
    push ax
    push cx
    mov al, %1
    mov cx, %2
    call prof_end
    pop cx
    pop ax
%endmacro

; =============================================================================
; THE WRAPPERS - one per stage, and the reason they are all in THIS file
;
; Each stack file renames its routine's LABEL to `*_i` and changes nothing
; else; every caller keeps the public name and reaches it through the wrapper
; here. Bracketing INSIDE a routine would mean finding every exit - `.out`,
; `.drop`, `.bad` - and a bracket missed on one error path silently carries
; that start value into the next call's total.
;
; The byte count is read AFTER the call, so a routine whose CX is its ANSWER
; (sk_rxput, sk_rxget) reports what it actually moved, and one whose CX is its
; argument reports what it was asked for. Both are the true number for that
; stage.
; =============================================================================
eth_pump:
    PROF_B PF_PUMP
    call eth_pump_i
    PROF_E PF_PUMP, 0
    ret

ne_ring_read:
    PROF_B PF_CARD
    call ne_ring_read_i
    PROF_E PF_CARD, ax          ; AX is preserved by the body: what was asked
    ret

eth_frame:
    PROF_B PF_FRAME
    call eth_frame_i
    PROF_E PF_FRAME, cx
    ret

ip_ck_part:
    PROF_B PF_CKSUM
    call ip_ck_part_i
    PROF_E PF_CKSUM, cx
    ret

sk_rxput:
    PROF_B PF_RXPUT
    call sk_rxput_i
    PROF_E PF_RXPUT, cx         ; ...and here CX is the ANSWER: how many were
    ret                         ; taken, which is the number that moved

sk_rxget:
    PROF_B PF_RXGET
    call sk_rxget_i
    PROF_E PF_RXGET, cx
    ret

ring_in:
    PROF_B PF_TXIN
    call ring_in_i
    PROF_E PF_TXIN, cx
    ret

ring_out:
    PROF_B PF_TXOUT
    call ring_out_i
    PROF_E PF_TXOUT, cx
    ret

ne_tx:
    PROF_B PF_TX
    call ne_tx_i
    PROF_E PF_TX, cx
    ret

; -----------------------------------------------------------------------------
; NETV_PROF - AL = 0 stop / 1 start / 2 read into the CALLER's ES:DI
;
; **THE READ WRITES INTO THE PACKAGE'S SEGMENT AND NOT OURS.** eth_pkg swaps ES
; for its own DS and banks the caller's in [eth_useg], so every verb that hands
; bytes back has to put it back - eth_v_addr does it through usr_write, and
; this one does it directly because it is the DRIVER's verb and has no DOS end
; to share a body with.
; -----------------------------------------------------------------------------
eth_v_prof:
    cmp al, 1
    je  .start
    cmp al, 2
    je  .read
    call prof_stop
    jmp short .ok
.start:
    call prof_start
    jmp short .ok
.read:
    push es
    mov es, [eth_useg]
    call prof_read              ; CX = PF_BLK, which the caller sizes against
    pop es
.ok:
    xor ax, ax
    clc
    ret

%include "os88pit.inc"          ; pit_now - the clock, shared with the FTP
%define prof_now pit_now        ; server's own split (SPEC.md 77.32). It was
                                ; this file's and is not any more: two copies
                                ; of a clock cannot disagree, and a
                                ; disagreement is the only thing two harnesses
                                ; are for (PERFORMANCE.md Part 6 rule 7)

; -----------------------------------------------------------------------------
; prof_beg - AL = a stage: stamp its start
; prof_end - AL = a stage, CX = bytes: accumulate the delta, the bytes and one
;            call
; Both preserve every register and the flags.
; -----------------------------------------------------------------------------
prof_beg:
    pushf                       ; FIRST: the header promises the flags survive,
    cmp byte [prof_on], 0       ; and this cmp writes CF=0 - a bracket sitting
    je  .out                    ; between a verb and its caller was turning
                                ; every refusal into success on the off path
    push ax
    push bx
    push dx
    mov bl, al
    xor bh, bh
    shl bx, 1
    shl bx, 1
    call prof_now
    mov [prof_t0+bx], ax
    mov [prof_t0+bx+2], dx
    pop dx
    pop bx
    pop ax
.out:
    popf
    ret

prof_end:
    pushf                       ; FIRST, for prof_beg's reason - and here the
    cmp byte [prof_on], 0       ; flag being wiped was the VERB's own answer
    je  .out                    ; (eth_pkg's PROF_E sits between the vtab call
                                ; and .out, CF = the refusal)
    push ax
    push bx
    push cx
    push dx
    push si
    mov bl, al
    xor bh, bh
    mov si, bx
    shl si, 1                   ; SI = the stage * 2, the 16-bit call counter
    shl bx, 1
    shl bx, 1                   ; BX = the stage * 4, the 32-bit pairs
    call prof_now               ; DX:AX = now, and it is needed WHOLE before
    or  cx, cx                  ; the subtraction below turns it into a delta
    jz  .nostamp                ; no bytes: an idle poll, not the transfer
    cmp si, PF_RXPUT * 2        ; ...and only the four PAYLOAD stages open the
    jb  .nostamp                ; window or close it. A broadcast the card
    cmp si, PF_TXOUT * 2        ; took and eth_frame dropped moved bytes and
    ja  .nostamp                ; is not the transfer - and on a busy LAN
                                ; there is one of those every second, which
                                ; would stretch the window over the whole run
                                ; and put the operator's hands right back in
                                ; it (SPEC.md 72.18.2)
    cmp byte [prof_fset], 0
    jne .last
    mov [prof_first], ax
    mov [prof_first+2], dx
    mov byte [prof_fset], 1
.last:
    mov [prof_last], ax
    mov [prof_last+2], dx
.nostamp:
    sub ax, [prof_t0+bx]
    sbb dx, [prof_t0+bx+2]
    add [prof_tm+bx], ax
    adc [prof_tm+bx+2], dx
    xor dx, dx
    add [prof_by+bx], cx
    adc [prof_by+bx+2], dx
    inc word [prof_ca+si]
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    popf
    ret

; -----------------------------------------------------------------------------
; prof_start / prof_stop - NETV_PROF's two switches
;
; Starting ZEROES everything, because a profile that silently continued a
; previous one would attribute the last transfer's time to this one. The flag
; goes down FIRST: a bracket firing halfway through the clear would leave a
; start value behind that nothing ever ends.
;
; Stopping BANKS the wall clock. Without that, a report read a minute after the
; run divides the same work by a minute and calls the stack 3% busy.
; -----------------------------------------------------------------------------
prof_start:
    push ax
    push cx
    push di
    push es
    mov byte [prof_on], 0
    push ds
    pop es
    mov di, prof_t0
    mov cx, PF_CLR
    xor al, al
    cld
    rep stosb
    call prof_now
    mov [prof_w0], ax
    mov [prof_w0+2], dx
    mov byte [prof_on], 1
    mov di, prof_t0             ; **AND EVERY STAGE STARTS FROM NOW.** Starting
    mov cx, PF_N                ; the profiler IS a socket verb, so PF_VERB's
.stamp:                         ; bracket is already OPEN around the call that
    mov [di], ax                ; got here - and the clear above just put a
    mov [di+2], dx              ; zero in its start value. Its PROF_E then
    add di, 4                   ; measures from the epoch and reports three
    loop .stamp                 ; billion counts, which is what the first run
                                ; of this instrument did. Re-stamping all of
                                ; them is one loop and cannot be got wrong;
                                ; a stage that is NOT open has its stamp
                                ; overwritten by its own PROF_B anyway
    pop es
    pop di
    pop cx
    pop ax
    ret

prof_stop:
    push ax
    push dx
    cmp byte [prof_on], 0
    je  .out
    call prof_now
    sub ax, [prof_w0]
    sbb dx, [prof_w0+2]
    mov [prof_wall], ax
    mov [prof_wall+2], dx
    mov byte [prof_on], 0
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; prof_read - ES:DI = a buffer of at least PF_BLK bytes; out CX = PF_BLK
;
; The stage NAMES travel with the numbers. A reporter carrying its own copy of
; that table is a reporter that labels the wrong column the day a stage is
; added, and this file is where a stage gets added.
; -----------------------------------------------------------------------------
prof_read:
    push ax
    push bx
    push dx
    push si
    push di
    mov byte [es:di], 'P'
    mov byte [es:di+1], 'F'
    mov byte [es:di+2], 2       ; VERSION 2: the header grew the active window
    mov byte [es:di+3], PF_N
    mov ax, [prof_wall]         ; STOPPED: the wall is the run that was...
    mov dx, [prof_wall+2]
    cmp byte [prof_on], 0
    je  .wall
    call prof_now               ; ...RUNNING: it is how long it has been going
    sub ax, [prof_w0]
    sbb dx, [prof_w0+2]
.wall:
    mov [es:di+4], ax
    mov [es:di+6], dx
    mov ax, [prof_last]         ; ...and the ACTIVE window beside it: the first
    mov dx, [prof_last+2]       ; byte any stage moved to the last
    sub ax, [prof_first]
    sbb dx, [prof_first+2]
    cmp byte [prof_fset], 0
    jne .act
    xor ax, ax                  ; nothing ever moved: say nothing rather than
    xor dx, dx                  ; the whole epoch
.act:
    mov [es:di+8], ax
    mov [es:di+10], dx
    add di, PF_HDR
    xor bx, bx
.l:
    mov ax, [prof_tm+bx]
    mov [es:di], ax
    mov ax, [prof_tm+bx+2]
    mov [es:di+2], ax
    mov ax, [prof_by+bx]
    mov [es:di+4], ax
    mov ax, [prof_by+bx+2]
    mov [es:di+6], ax
    mov si, bx
    shr si, 1
    mov ax, [prof_ca+si]
    mov [es:di+8], ax
    mov si, [prof_names+si]
    add di, 10
    call prof_name
    add di, 8
    add bx, 4
    cmp bx, PF_N * 4
    jb  .l
    mov cx, PF_BLK
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret

; --- prof_name - eight bytes of DS:SI to ES:DI, NUL-padded -------------------
prof_name:
    push ax
    push cx
    push si
    push di
    mov cx, 8
.l:
    xor al, al
    cmp byte [si], 0
    je  .put                    ; past the NUL: pad, and do not walk on into
    mov al, [si]                ; whatever the next string is
    inc si
.put:
    mov [es:di], al
    inc di
    loop .l
    pop di
    pop si
    pop cx
    pop ax
    ret

prof_names: dw pf_s0, pf_s1, pf_s2, pf_s3, pf_s4
            dw pf_s5, pf_s6, pf_s7, pf_s8, pf_s9
pf_s0:      db 'pump', 0
pf_s1:      db 'card', 0
pf_s2:      db 'frame', 0
pf_s3:      db 'cksum', 0
pf_s4:      db 'rxput', 0
pf_s5:      db 'rxget', 0
pf_s6:      db 'txin', 0
pf_s7:      db 'txout', 0
pf_s8:      db 'tx', 0
pf_s9:      db 'verb', 0

; --- the counters, and they ship inside the image like every other driver byte
; prof_t0, prof_tm, prof_by, prof_ca and prof_wall are CONTIGUOUS and in that
; order: prof_start clears PF_CLR bytes of them in one rep stosb, and prof_w0
; is stamped after and so must stay outside that run.
prof_t0:    times PF_N * 4 db 0     ; the open bracket's stamp, per stage
prof_tm:    times PF_N * 4 db 0     ; ...and the total it accumulates into
prof_by:    times PF_N * 4 db 0
prof_ca:    times PF_N * 2 db 0
prof_wall:  dd 0                    ; how long the run was, banked by prof_stop
prof_first: dd 0                    ; ...and the ACTIVE window inside it: when
prof_last:  dd 0                    ; the first byte moved, and the last
prof_fset:  db 0                    ; has prof_first been taken yet?
prof_w0:    dd 0                    ; when prof_start ran - OUTSIDE the clear
prof_on:    db 0                    ; above, because it is stamped after it
%endif
