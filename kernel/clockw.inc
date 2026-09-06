; =============================================================================
; os8088 - clockw.inc : SPEC.md 37's RTC WRITE half (SPEC.md 37.94)
;
; NOT A KERNEL FILE. It is %included from ctrl.inc's `.modc` (ctrl.inc's
; %include line is the only place that says so), so every byte below ships in
; CTRL.DRV: read off the floppy when the Control Panel opens, freed when it
; closes. It has no `section` directive of ITS OWN for exactly that reason -
; the section is the one the includer is in, and adding one here would
; silently move the module header off offset 0.
;
; SO THIS IS THE ONE FILE IN kernel/ WHOSE SECTION CANNOT BE READ FROM THE
; FILE, and every host-side tool that walks `kernel/*.inc` one file at a time
; has to be told. tools/os88ovlchk.py is told, in its EXTRA map - and a size
; pass that was not told measured `.text` jumps landing inside the CTRL.DRV
; image, which assembles and runs wrong. tools/kernsize.py --modules is the
; other end of the same gap: it brackets modules by their %include lines in
; kernel.asm, so this file has no row of its own and its bytes are counted
; under ctrl.inc. Neither is a defect to fix here; both are things to know
; before quoting a number about this file.
;
; WHY IT IS HERE AND NOT IN THE KERNEL. clk_rtc_write has one caller:
; cp_flush_close_x, the Control Panel's teardown (SPEC.md 31.8). It used to
; have a different one - ui_task draining [clk_dirty] on the tick - and that
; was a machine asking itself 18.2 times a second about a flag only this page
; ever sets. SPEC.md 31.5.2 argued the write belongs at the panel's close and
; recorded the argument; SPEC.md 37.94 acts on it, and the code followed the
; decision into the image.
;
; THE CONTRACT, and all of it matters:
;   CS = this module's heap claim, DS = KERNEL_SEG, SS = LOW_SEG.
; DS is the kernel's, so every [clk_*] below is the same reference it was in
; .text and nothing needed rewriting. What DID need rewriting is the six port
; helpers this reaches: they stay RESIDENT because the boot overlay's read
; path shares them (SPEC.md 37.94's table), so each is far-called. Four of
; them end in `retf` and are called by name; the two clk_ns_stamp near-calls
; keep a `ret` and a cw_ thunk. os88ovlchk.py refuses either written wrong.
;
; clk_dowt is the one piece of DATA that came along, and it is read through
; CS per SPEC.md 2.8.6: nothing can ask for a weekday while this image is not
; loaded, because the only things that ask are the three rungs below.
;
; BIOS FROM HERE IS FINE, and the old comment in clock.inc said the opposite.
; Rung 4 (CLK_T_BIOS - the enum, which is what the section headers below
; number by; it is step 5 of clk_probe's WALK) calls int 1Ah with the gfx
; lock held, from the UI task - and
; drv_cfg_save's int 13h, two lines further into cp_flush_close_x, is two
; seconds of the same thing. SPEC.md 31.1's "must not call BIOS" binds
; cp_paint and cp_onclick, which are page callbacks. Teardown is not one.
; =============================================================================


; =============================================================================
; The dispatcher - SPEC.md 37.90's ladder, from the writing end
; =============================================================================

; -----------------------------------------------------------------------------
; clk_rtc_write - push the live time back to the hardware RTC
; in:  nothing; NO gfx lock held (SPEC.md 13/31.1)
; out: nothing (all registers preserved)
;
; A machine without an RTC returns immediately: the clock is the PIT's. The
; "outside the gfx lock" rule predates the ladder - only rung 4 calls BIOS
; now - but it stays, because the caller cannot know which rung answered and
; ui_task step 3 is a perfectly good place to drain [clk_dirty] from.
; -----------------------------------------------------------------------------
clk_rtc_write:
    push ax
    push bx
    cmp byte [clk_rtc], 0
    je .out
    mov bl, [clk_tier]          ; 1..4 whenever [clk_rtc] is 1, so it indexes
    xor bh, bh                  ; the table below directly. The compare chain
    dec bx                      ; this replaces was four `cmp al,CLK_T_X / jne
    shl bx, 1                   ; / call / jmp short` blocks, 9 bytes a rung
    cmp bx, 8                   ; belt as well as braces: [clk_rtc] already
    jae .out                    ; says a rung claimed, and .bss is not zeroed
    call [cs:clk_wtab+bx]       ; THROUGH CS: this is a module (SPEC.md 2.8.6)
.out:                           ; and its data may not be reached through DS,
    pop bx                      ; exactly as clk_dowt below is read
    pop ax
    ret

; THE TABLE IS INDEXED BY THE CLK_T_* ENUM, NOT BY THE ORDER THE LADDER WALKS
; - the same trap ctrl.inc's cp_rtcnam carries by name, and getting it wrong
; writes the WRONG CHIP on a machine nobody here has. The rows are the ENUM's
; order - AT (1), NS (2), RP (3), BIOS (4), less the 1 the `dec bx` takes off -
; while the PROBE walks AT, the int 1Ah reference, RP, NS, BIOS. Two different
; sequences, and this is the first one.
;
; ALL FOUR TARGETS MUST STAY IN .modc. os88ovlchk checks a DIRECT near call's
; section and says outright that it cannot check an indirect one, so this
; converts four checked calls into one it is blind to. A writer that ever
; moves out of this module has to become a far entry here by hand.
clk_wtab:   dw clk_at_write, clk_ns_write, clk_rp_write, clk_bios_write

; =============================================================================
; Rung 1: the MC146818 / DS12885 at 70h/71h
; =============================================================================

; -----------------------------------------------------------------------------
; clk_at_put - write AH to CMOS register AL
; in:  AL = index, AH = value; IF = 0 already
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
clk_at_put:
    push ax
    or al, 0x80
    out CLK_AT_IDX, al          ; the IMMEDIATE port form: 70h and 71h are both
    jmp short $+2               ; below 100h, so DX is not needed and there is
    mov al, ah                  ; nothing to bank (as clk_at_get, clock.inc)
    out CLK_AT_DAT, al
    pop ax
    ret
; -----------------------------------------------------------------------------
; clk_at_enc - a binary field back into the format the chip is running in
; in:  AL = 0..99; out: AL encoded; clobbers AH
;
; The encoding comes from [clk_rbin] - what the READ path actually settled
; on - and NOT from Register B's DM bit. clk_at_read has an explicit
; fallback for a chip whose DM bit misreports the encoding; taking DM at
; face value here would encode the other way round from the way we decoded,
; write a nonsense time into a working clock, and then fail to read it back
; next boot.
; -----------------------------------------------------------------------------
clk_at_enc:
    cmp byte [clk_rbin], 0
    jne .bin
    call KERNEL_SEG:cw_clk_tobcd
.bin:
    ret
; -----------------------------------------------------------------------------
; clk_at_eput - encode a binary field and write it to a CMOS register
; in:  AL = a 0..99 field, CH = its CMOS index
; out: nothing (all registers preserved except AH, which clk_at_enc spends)
;
; Five sites of `mov al,[clk_X] / call clk_at_enc / mov ah,al / mov al,<idx> /
; call clk_at_put` written once. CH AND NOT DX: DX is what clk_at_write does
; not bank any more (F-clockw-02), CL is the divisor 100 later in the same
; routine and BL parks the century across it, so CH is the free half.
; clk_at_enc reaches cw_clk_tobcd, which banks CX; clk_at_put banks AX.
; -----------------------------------------------------------------------------
clk_at_eput:
    call clk_at_enc
    mov ah, al
    mov al, ch
    call clk_at_put
    ret

; -----------------------------------------------------------------------------
; clk_at_hour - a 0..23 hour back into the chip's hours register
; in:  AL = 0..23; out: AL encoded, PM bit included; clobbers AH
; -----------------------------------------------------------------------------
clk_at_hour:
    push bx
    test byte [clk_rb], 0x02
    jnz .h24
    xor bl, bl
    cmp al, 12
    jb .am
    mov bl, 0x80
    sub al, 12
.am:
    or al, al
    jnz .enc
    mov al, 12                  ; midnight and noon both read 12
.enc:
    call clk_at_enc             ; BCD FIRST and the PM bit after - the other
    or al, bl                   ; order feeds 8Ch to clk_tobcd
    pop bx
    ret
.h24:
    call clk_at_enc
    pop bx
    ret
; -----------------------------------------------------------------------------
; clk_at_write - push the live clock back to the AT chip
; out: nothing (all registers preserved)
;
; Register B's SET bit inhibits the update transfer for the length of the
; burst, which is the whole point: without it the once-a-second cycle can
; land between two of the six writes, copy the chip's own counters over what
; we have already stored, and run its carry logic against a half-written
; value. That is a ~0.2% chance per attempt - exactly the frequency that
; survives testing and shows up on someone's machine.
;
; What it deliberately does NOT do is touch Register A's divider bits. Linux
; and most BIOS setup screens reset the divider so the first tick lands a
; known 500 ms later; the cost of getting it wrong is a stopped clock, the
; benefit is sub-second phase this OS never displays, and 86Box does not
; model it - so there is no way to test the fix if it breaks. SET alone.
;
; DM and 24/12 are read and OBEYED, never rewritten: the machine's own BIOS
; was written against whatever it chose, and flipping the chip behind its
; back makes the clock read wrong from DOS afterwards.
; -----------------------------------------------------------------------------
clk_at_write:
    push ax
    push bx
    push cx                     ; ...and NOT DX: this body never names it, and
                                ; neither does anything it calls. clk_at_get,
                                ; clk_at_done and clk_at_put use the IMMEDIATE
                                ; port form and do not touch DX at all now;
                                ; clk_at_enc reaches clk_tobcd, which banks CX;
                                ; clk_at_hour banks BX; clk_dow banks BX/CX/DX
    pushf
    cli
    mov al, 0x0B
    call KERNEL_SEG:clk_at_get
    mov [clk_rb], al            ; save_control, restored whole at the end
    mov ah, al
    or ah, 0x80                 ; SET
    mov al, 0x0B
    call clk_at_put

    mov al, [clk_sec]
    mov ch, 0x00
    call clk_at_eput
    mov al, [clk_min]
    mov ch, 0x02
    call clk_at_eput
    mov al, [clk_hour]          ; the hour goes through clk_at_hour instead -
    call clk_at_hour            ; it carries the PM bit - so it keeps its own
    mov ah, al                  ; sequence
    mov al, 0x04
    call clk_at_put
    mov al, [clk_day]
    mov ch, 0x07
    call clk_at_eput
    mov al, [clk_mon]
    mov ch, 0x08
    call clk_at_eput
    call clk_dow
    inc al                      ; the chip counts 1..7 from Sunday. Nothing
    mov ah, al                  ; here reads it, but a BIOS setup screen and
    mov al, 0x06                ; DOS both might
    call clk_at_put
    mov ax, [clk_year]
    mov cl, 100
    div cl                      ; AL = century, AH = the year within it
    mov bl, al                  ; park it: clk_at_enc clobbers AH
    mov al, ah
    mov ch, 0x09
    call clk_at_eput
    cmp byte [clk_cent], 0
    je .nocent                  ; this BIOS keeps no century byte, so do not
    mov al, bl                  ; invent one. 32h is outside the AT CMOS
    call KERNEL_SEG:cw_clk_tobcd              ; checksum range (10h..2Dh), so writing it
    mov ah, al                  ; needs no checksum fixup - and equally, must
    mov al, 0x32                ; not trigger one. ALWAYS BCD, never clk_at_enc:
    call clk_at_put             ; 32h is BIOS RAM and DM does not describe it,
                                ; which is how the read decodes it too
.nocent:
    mov al, 0x0B
    mov ah, [clk_rb]
    call clk_at_put             ; SET cleared, every other bit as we found it
    popf
    call KERNEL_SEG:clk_at_done
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Rung 2: the National MM58167 at 2C0h
; =============================================================================

; -----------------------------------------------------------------------------
; clk_ns_write - push the live clock back to the MM58167
; out: nothing (all registers preserved)
;
; There is no halt bit and no SET bit on this part - the counters free-run
; while they are written and the oscillator cannot be stopped. All that can
; be done about it is to write coarse to fine so that the window in which a
; carry can land on top of a field we have already stored is as short as
; possible; seconds go last.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; clk_ns_bput - BCD-encode a field and write it to an MM58167 counter
; in:  AL = a 0..99 field, BL = its register
; out: nothing (AH spent, everything else preserved)
;
; Five sites of `mov al,[clk_X] / call cw_clk_tobcd / mov ah,al /
; mov al,CLK_NS_Y / call cw_clk_ns_put` written once. The day-of-week site is
; the sixth and is deliberately NOT BCD, so it keeps its own sequence.
;
; BL is what makes clk_ns_write's `push bx` live again - see its prologue.
; -----------------------------------------------------------------------------
clk_ns_bput:
    call KERNEL_SEG:cw_clk_tobcd
    mov ah, al
    mov al, bl
    call KERNEL_SEG:cw_clk_ns_put
    ret

clk_ns_write:
    push ax
    push bx                     ; ...and NOT CX: this body never names it and
                                ; every callee banks it - clk_tobcd, clk_dow,
                                ; clk_ns_stamp. BX is a different matter: it is
                                ; clk_ns_bput's ARGUMENT, so the bank stays
    push dx
    pushf
    cli
    mov al, [clk_mon]
    mov bl, CLK_NS_MON
    call clk_ns_bput
    mov al, [clk_day]
    mov bl, CLK_NS_DOM
    call clk_ns_bput
    call clk_dow
    inc al                      ; 1..7, and our own probe insists on it. NOT
    mov ah, al                  ; BCD - the day of the week is a plain 3-bit
    mov al, CLK_NS_DOW          ; counter - so this one is written out
    call KERNEL_SEG:cw_clk_ns_put
    mov al, [clk_hour]
    mov bl, CLK_NS_HR
    call clk_ns_bput
    mov al, [clk_min]
    mov bl, CLK_NS_MIN
    call clk_ns_bput
    mov al, [clk_sec]
    mov bl, CLK_NS_SEC
    call clk_ns_bput
    ; --- the year, in both conventions, plus the bookkeeping that says so
    mov ax, [clk_year]
    mov dl, [clk_mon]
    call KERNEL_SEG:clk_ns_stamp
    popf
    pop dx
    pop bx
    pop ax
    ret

; =============================================================================
; Rung 3: the Ricoh RP5C01 / Toshiba TC8521 at 2C0h
; =============================================================================

; -----------------------------------------------------------------------------
; clk_rp_put - write BCD digit AH to RP5C01 port AL
; in:  AL = port 00h..0Fh, AH = the digit; out: nothing (all preserved)
; The read half, clk_rp_get, is resident (the probe needs it) and far-called;
; this one only ever writes, so it came here with the rest of rung 3.
; -----------------------------------------------------------------------------
clk_rp_put:
    push ax
    push dx
    mov dx, CLK_XT_BASE         ; the base whole, the port index into DL alone:
    add dl, al                  ; AL is 00h..0Fh by this routine's contract, so
                                ; DL <= 0xCF and the add cannot carry into DH
    mov al, ah
    and al, 0x0F
    out dx, al
    pop dx
    pop ax
    ret
; -----------------------------------------------------------------------------
; clk_rp_yy - the two year digits to store, in this machine's own convention
; out: AL = 0..99; preserves every other register
; -----------------------------------------------------------------------------
clk_rp_yy:
    push bx                     ; ...and NOT DX: the `div bl` below is the
                                ; 8-BIT form, which writes AL and AH and never
                                ; touches DX
    mov ax, [clk_year]
    cmp byte [clk_rpep], 0
    je .cent
    sub ax, CLK_YMIN            ; convention B: the digits are year - 1980
    cmp ax, 100
    jb .out
    mov ax, 99                  ; ...which runs out in 2080
    jmp short .out
.cent:
    mov bl, 100
    div bl                      ; AH = year mod 100
    mov al, ah
.out:
    xor ah, ah
    pop bx
    ret
; -----------------------------------------------------------------------------
; clk_rp_hour - the hour digits, in the mode [clk_rp24] says the chip is in
; out: nothing (all registers preserved)
;
; In 12-hour mode the PM flag is BIT 1 OF THE 10-HOUR DIGIT, not a separate
; register - so it is or'd into the tens after the split, and midnight and
; noon both store 12 exactly as they do on the AT chip.
; -----------------------------------------------------------------------------
clk_rp_hour:
    push ax
    push bx
    push cx
    mov al, [clk_hour]
    xor bl, bl
    test byte [clk_rp24], 1
    jnz .split                  ; bit 0 set = the chip is running 24-hour
    cmp al, 12
    jb .am
    mov bl, 2
    sub al, 12
.am:
    or al, al
    jnz .split
    mov al, 12
.split:
    aam                         ; AH = tens, AL = units
    or ah, bl
    mov cl, ah
    mov ah, al
    mov al, 0x04
    call clk_rp_put
    mov ah, cl
    mov al, 0x05
    call clk_rp_put
    pop cx
    pop bx
    pop ax
    ret
; -----------------------------------------------------------------------------
; clk_rp_pair - a 0..99 field as two digits, units at CL and tens at CL+1
; in:  AL = 0..99, CL = the units port offset
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
clk_rp_pair:
    push ax
    push bx
    aam                         ; AH = tens, AL = units
    mov bl, ah
    mov ah, al
    mov al, cl
    call clk_rp_put
    mov ah, bl
    mov al, cl
    inc al
    call clk_rp_put
    pop bx
    pop ax
    ret
; -----------------------------------------------------------------------------
; clk_rp_write - push the live clock back to the RP5C01
; out: nothing (all registers preserved)
;
; There is no write-protect, no unlock and no SET bit: a counter write lands
; immediately. What the sequence does need is the timer stopped across it
; (MODE bit 3) so the sub-second divider cannot carry into a field already
; written, and the alarm-enable bit preserved through all three MODE writes -
; blasting a constant there silently disables a machine's alarm or stops its
; clock for good, since the chip is battery-backed and would stay stopped.
;
; TEST (0Eh) and RESET (0Fh) are left alone. RESET is write-only, so its
; other bits cannot be read back first, and two of them ENABLE pulse outputs
; when clear - a divider reset would be bought at the price of driving a pin
; whose wiring is unknown on the machines this is aimed at.
; -----------------------------------------------------------------------------
clk_rp_write:
    push ax
    push bx
    push cx                     ; ...and NOT DX: this body never names it, and
                                ; clk_rp_get, clk_rp_put, clk_rp_pair,
                                ; clk_rp_yy, clk_rp_hour and clk_dow all bank
                                ; or ignore it
    pushf
    cli
    mov al, CLK_RP_MODE
    call KERNEL_SEG:clk_rp_get
    mov bl, al
    and bl, 0x04                ; keep alarm-enable; page 0, timer STOPPED
    mov ah, bl
    mov al, CLK_RP_MODE
    call clk_rp_put
    mov al, [clk_sec]
    mov cl, 0x00
    call clk_rp_pair
    mov al, [clk_min]
    mov cl, 0x02
    call clk_rp_pair
    call clk_dow                ; 0-based here, unlike the MM58167's 1..7
    mov ah, al
    mov al, CLK_RP_DOW
    call clk_rp_put
    mov al, [clk_day]
    mov cl, 0x07
    call clk_rp_pair
    mov al, [clk_mon]
    mov cl, 0x09
    call clk_rp_pair
    call clk_rp_yy              ; the year, in whichever convention the
    mov cl, 0x0B                ; probe found this machine using
    call clk_rp_pair
    ; --- page 1: read the 12/24 bit, write the leap counter ---------------
    ; The mode bit is the MACHINE'S, not ours. Forcing it to 24-hour would
    ; leave every ROM and driver that assumed 12-hour reading the afternoon
    ; twelve hours low - the same "do not flip the chip behind the BIOS's
    ; back" rule the AT rung follows for Register B.
    mov ah, bl
    or ah, 0x01
    mov al, CLK_RP_MODE
    call clk_rp_put
    mov al, CLK_RP_24
    call KERNEL_SEG:clk_rp_get
    mov [clk_rp24], al
    mov ax, [clk_year]
    and ax, 3                   ; this chip's leap rule is a mod-4 counter
    mov ah, al
    mov al, CLK_RP_LEAP
    call clk_rp_put
    ; --- page 0 again for the hour, then the timer back on ---------------
    mov ah, bl
    mov al, CLK_RP_MODE
    call clk_rp_put
    call clk_rp_hour
    mov ah, bl
    or ah, 0x08                 ; nothing above may skip this: the chip is
    mov al, CLK_RP_MODE         ; battery-backed and would stay stopped
    call clk_rp_put
    popf
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Rung 4: int 1Ah, and no way to know whether it did anything
; =============================================================================

; -----------------------------------------------------------------------------
; clk_bios_write - rung 4's write: int 1Ah AH=03h + AH=05h
; out: nothing (all registers preserved)
;
; There is no way to find out whether this did anything. An XT-lineage BIOS
; that does not implement the function simply irets, and the whole point of
; the four rungs above is that a machine whose clock we can name does not
; have to rely on it.
; -----------------------------------------------------------------------------
clk_bios_write:
    push ax
    push bx
    push cx
    push dx

    mov al, [clk_hour]          ; AH=03h: CH/CL/DH = hour/min/sec BCD,
    call KERNEL_SEG:cw_clk_tobcd              ; DL = 0 (no daylight saving, SPEC.md 37)
    mov ch, al
    mov al, [clk_min]
    call KERNEL_SEG:cw_clk_tobcd
    mov cl, al
    mov al, [clk_sec]
    call KERNEL_SEG:cw_clk_tobcd
    mov dh, al
    mov dl, 0
    mov ah, 0x03
    int 0x1a

    mov ax, [clk_year]          ; AH=05h: CH/CL = century/year BCD,
    mov cl, 100                 ; DH/DL = month/day BCD
    div cl                      ; AL = century, AH = year within it
    mov bl, ah                  ; clk_tobcd clobbers AH - park it
    call KERNEL_SEG:cw_clk_tobcd
    mov bh, al
    mov al, bl
    call KERNEL_SEG:cw_clk_tobcd
    mov ch, bh
    mov cl, al
    mov al, [clk_mon]
    call KERNEL_SEG:cw_clk_tobcd
    mov dh, al
    mov al, [clk_day]
    call KERNEL_SEG:cw_clk_tobcd
    mov dl, al
    mov ah, 0x05
    int 0x1a

.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Shared by the three rungs that write a weekday counter
; =============================================================================

; -----------------------------------------------------------------------------
; clk_dow - the day of the week for the live date
; in:  nothing ([clk_year]/[clk_mon]/[clk_day])
; out: AL = 0..6, 0 = Sunday; preserves every other register
;
; Sakamoto's method. Both XT chips have a day-of-week counter that they do
; NOT derive from the date, and the MM58167's must read 1..7 or our own probe
; rejects the card next boot - so writing a real weekday is not decoration.
;
; The `- y/100 + y/400` pair is folded to a single -15. Over the adjusted
; range 1979..2099 the two terms move TOGETHER - 19 and 4 below 2000, 20 and
; 5 from 2000 (400 x 5 = 2000 exactly) - so their difference is the same
; constant on both sides. Folding only y/400 and branching on y/100, which
; is the obvious-looking version, is off by one for every date from March
; 2000 onwards.
; -----------------------------------------------------------------------------
clk_dow:
    push bx
    push cx
    push dx
    mov ax, [clk_year]
    cmp byte [clk_mon], 3
    jae .mar
    dec ax                      ; Jan and Feb belong to the year before
.mar:
    mov cx, ax
    shr ax, 1
    shr ax, 1
    add cx, ax                  ; + y/4
    sub cx, 15                  ; + y/400 - y/100, which is -15 either side
    mov bl, [clk_mon]           ; of 2000 (see the note above)
    xor bh, bh
    dec bx
    mov al, [cs:clk_dowt+bx]
    xor ah, ah
    add cx, ax
    mov al, [clk_day]
    xor ah, ah
    add cx, ax
    mov ax, cx
    xor dx, dx
    mov bx, 7
    div bx
    mov ax, dx                  ; AL = the remainder, 0..6
    pop dx
    pop cx
    pop bx
    ret

; --- Sakamoto's month offsets (SPEC.md 2.8.6: module data, read through CS) --
clk_dowt:   db 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4
