; =============================================================================
; os8088 - clock.inc
;
; The system clock (SPEC.md 37): one date + time of day for the whole
; system, seeded from the hardware RTC when the machine has one and advanced
; from the PIT afterwards. Prefix clk_ - the built-in Clock APP (SPEC.md 14)
; is unrelated and keeps its own app_clk_ names and its own per-instance
; stopwatch state.
;
; ONLY THE UI TASK writes the fields - clk_tick from its loop, clk_fld_adj
; from a Control Panel click, both on task 0. Readers are not so confined:
; menu_draw_bar reaches clk_fmt from wm_paint_all, which a DYING BACKGROUND
; TASK runs inside wm_destroy (SPEC.md 29.4), so a reader can be pre-empted
; mid-format while the UI task carries a second. Hence the two rules of
; SPEC.md 37: every write that carries (clk_inc_sec, clk_fld_adj) mutates
; under pushf/cli, and every reader takes a clk_snap copy under the same
; guard and formats from that. Nothing in an ISR touches any of it - the
; clock is derived from [ticks], which the PIT hook already owns (SPEC.md 8).
;
; The one BIOS caller is clk_rtc_write, which runs from ui_task step 3
; OUTSIDE the gfx lock (SPEC.md 13) precisely because a window callback may
; not call BIOS (SPEC.md 31.1) - the Control Panel edits the fields and
; posts [clk_dirty] instead. Only the BIOS tier still calls int 1Ah, but the
; rule stays: the caller cannot know which tier answered.
;
; THE RTC IS A LADDER (SPEC.md 37.90). int 1Ah AH=02h..05h is the LAST rung,
; not the only one: an XT BIOS implements AH=00h/01h and nothing else, so on
; a 5150 with a clock card the BIOS knows nothing about a clock that is
; sitting right there - and a BIOS that implements the two READ functions
; may still iret out of the two WRITE ones, which is a clock you can read
; and never set. So clk_probe walks four rungs and clk_rtc_write dispatches
; on which one answered.
; =============================================================================

; --- the no-RTC fallback: 4 July 2026, 00:00:00 (SPEC.md 37) ----------------
CLK_DEF_Y   equ 2026
CLK_DEF_M   equ 7
CLK_DEF_D   equ 4

CLK_YMIN    equ 1980            ; the year range the Date/Time page allows;
CLK_YMAX    equ 2099            ; also the range the leap rule below is exact
                                ; over (see clk_mlen)

; --- month names and lengths (data, .text) ----------------------------------
; One 3-char cell per month, indexed by (month-1)*3. The same table serves
; clk_fmt and the Date/Time page's month field.
clk_mnames: db 'JanFebMarAprMayJunJulAugSepOctNovDec'
clk_mdays:  db 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31

; --- Sakamoto's month offsets, for clk_dow ----------------------------------
clk_dowt:   db 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4

; --- the RTC ladder (SPEC.md 37.90): which chip answered, and where ----------
CLK_T_NONE  equ 0               ; no clock at all: the PIT and the fallback
CLK_T_AT    equ 1               ; MC146818 / DS12885 at 70h/71h
CLK_T_NS    equ 2               ; National MM58167 at CLK_XT_BASE
CLK_T_RP    equ 3               ; Ricoh RP5C01 / Toshiba TC8521 at CLK_XT_BASE
CLK_T_BIOS  equ 4               ; int 1Ah AH=02h..05h, and nothing better

CLK_AT_IDX  equ 0x70            ; index; WRITE ONLY, and bit 7 masks NMI
CLK_AT_DAT  equ 0x71
CLK_XT_BASE equ 0x2C0           ; where an XT add-on clock lives - BOTH chips
CLK_UIP_MAX equ 4000            ; bounded UIP poll: never spin on a stuck bit.
                                ; UIP is high for at most 2.228 ms per second,
                                ; and two ISA accesses cost ~2 us even on a
                                ; 386 - so this covers the window with an
                                ; order of magnitude to spare on every target
CLK_RETRY   equ 10              ; bounded coherent-read retries

; RTC= in the Makefile forces one rung instead of walking the ladder, the
; way VIDEO= forces the adapter (SPEC.md 39.1) - QEMU has an MC146818 and
; nothing else, so rung 1 always wins there and rungs 2/3/4 would otherwise
; be unreachable under the QMP harness.
%ifndef CLK_FORCE
%define CLK_FORCE 0
%endif
%define CLK_TRY(n) (CLK_FORCE == 0 || CLK_FORCE == n)

; -----------------------------------------------------------------------------
; clk_init - fallback date first, then probe the hardware RTC
; in:  nothing (kmain, right after evq_init - SPEC.md 15)
; out: nothing (all registers preserved)
;
; The fallback is stored BEFORE the probe, so a probe that fails - at any of
; its four gates - leaves a sane clock behind rather than a half-written one.
; -----------------------------------------------------------------------------
clk_init:
    push ax
    mov byte [clk_rtc], 0
    mov byte [clk_dirty], 0
    mov byte [clk_h12], 0       ; display settings, boot defaults (SPEC.md
    mov byte [clk_secs], 0      ; 37): 24-hour, no seconds in the menu bar
    mov byte [clk_barq], 0
    mov word [clk_year], CLK_DEF_Y
    mov byte [clk_mon], CLK_DEF_M
    mov byte [clk_day], CLK_DEF_D
    mov byte [clk_hour], 0
    mov byte [clk_min], 0
    mov byte [clk_sec], 0
    mov ax, [ticks]
    mov [clk_last], ax
    mov word [clk_acc], 0
    call clk_probe              ; CF = 1: no usable RTC, keep the fallback
    jc .out
    mov byte [clk_rtc], 1
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; clk_probe - walk the RTC ladder, and seed the clock from whatever answers
; in:  nothing (clk_init, boot)
; out: CF = 0 and the fields updated, [clk_tier] naming the rung that
;      answered; CF = 1 and nothing written. Preserves all registers.
;
; ORDER IS THE WHOLE DESIGN, and it is chosen so that no rung ever writes to
; a chip a later rung might have identified differently:
;
;   1. The AT clock at 70h/71h. Read-only probe, and on an XT 70h-7Fh is
;      simply unused - reads float to FFh and the structural test rejects
;      that. An AT-class machine stops here and never looks at 2C0h, which
;      matters because 2B0h-2DFh is the alternate-EGA range on an AT.
;   2. int 1Ah, which is not itself a rung yet: it is the REFERENCE the next
;      one is checked against.
;   3. The Ricoh RP5C01 / Toshiba TC8521 at 2C0h - sixteen direct-mapped
;      4-bit ports. Claimed ONLY when the digits it holds decode to the same
;      hour, day, month and year the BIOS just reported. That single test
;      confirms the chip, the base, the addressing mode and the page at once,
;      with no writes; a machine whose BIOS cannot read the clock cannot pass
;      it, which is exactly the guard that keeps this rung off a SixPakPlus.
;   4. The National MM58167 at 2C0h - the AST SixPakPlus and its clones. Its
;      probe has one write (a scratch nibble, restored), which is why it runs
;      after rung 3 has had its read-only look at the same address.
;   5. int 1Ah after all, for a machine whose BIOS is the only thing that
;      knows where its clock is. Reads work; writes may silently do nothing,
;      and there is no way to find out from here.
; -----------------------------------------------------------------------------
clk_probe:
    push ax
    mov byte [clk_tier], CLK_T_NONE
    mov byte [clk_cent], 0
    mov byte [clk_ref], 0

%if CLK_TRY(CLK_T_AT)
    call clk_at_probe
    jc .no_at
    call clk_at_read
    jc .no_at
    mov byte [clk_tier], CLK_T_AT
    jmp .found
.no_at:
%endif

    call clk_bios_read          ; the reference, and rung 5's own answer
    jc .no_bios
    mov byte [clk_ref], 1
    mov al, [clk_tb_h]          ; parked, because every rung below writes
    mov [clk_rf_h], al          ; its own answer into the same scratch
    mov al, [clk_tb_d]
    mov [clk_rf_d], al
    mov al, [clk_tb_mo]
    mov [clk_rf_mo], al
    mov ax, [clk_tb_y]
    mov [clk_rf_y], ax
.no_bios:

%if CLK_TRY(CLK_T_RP)
    cmp byte [clk_ref], 0
    je .no_rp                   ; nothing to check the chip against, and an
    call clk_rp_probe           ; unidentified chip is not one we may write
    jc .no_rp
    call clk_bios_read          ; the two agreed, so either source will do
    jc .no_rp
    mov byte [clk_tier], CLK_T_RP
    jmp short .found
.no_rp:
%endif

%if CLK_TRY(CLK_T_NS)
    call clk_ns_probe
    jc .no_ns
    call clk_ns_read
    jc .no_ns
    mov byte [clk_tier], CLK_T_NS
    jmp short .found
.no_ns:
%endif

%if CLK_TRY(CLK_T_BIOS)
    cmp byte [clk_ref], 0
    je .none
    call clk_bios_read
    jc .none
    mov byte [clk_tier], CLK_T_BIOS
    jmp short .found
%endif
.none:
    pop ax
    stc
    ret
.found:
    call clk_commit
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; clk_commit - adopt the scratch values as the live clock (internal)
; in:  clk_tb_* filled and validated by a rung's read
; out: nothing (all registers preserved)
;
; The commit is ONE place for the same reason SPEC.md 37 always gave: a read
; that fails halfway can never leave a half-RTC, half-fallback clock, and
; with four rungs there are now four ways to fail halfway.
; -----------------------------------------------------------------------------
clk_commit:
    push ax
    mov ax, [clk_tb_y]          ; the year is the one field a rung may have
    cmp ax, CLK_YMIN            ; reconstructed rather than read (SPEC.md
    jae .ylo                    ; 37.1: the XT chips carry no year at all),
    mov ax, CLK_YMIN            ; so it is clamped here rather than trusted
.ylo:
    cmp ax, CLK_YMAX
    jbe .yok
    mov ax, CLK_YMAX
.yok:
    mov [clk_year], ax
    mov al, [clk_tb_h]
    mov [clk_hour], al
    mov al, [clk_tb_mi]
    mov [clk_min], al
    mov al, [clk_tb_s]
    mov [clk_sec], al
    mov al, [clk_tb_mo]
    mov [clk_mon], al
    mov al, [clk_tb_d]
    mov [clk_day], al
    pop ax
    ret

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
    mov al, [clk_dowt+bx]
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

; =============================================================================
; Tier CLK_T_AT (1): the MC146818 at 70h/71h (SPEC.md 37.90)
; =============================================================================
; Every access is an index write to 70h followed IMMEDIATELY by 71h, and the
; pair is global mutable state shared with the BIOS: anything that lands
; between the halves and itself touches CMOS leaves the index somewhere else
; and our `in al, 71h` silently returns a different register. In THIS kernel
; that is not theoretical - the int 08h hook pre-empts every 55 ms and swaps
; SP into another task (SPEC.md 7). So every pair runs with IF = 0, and every
; index write carries bit 7 (NMI masked), which is the only defence against
; an NMI splitting the pair - the CPU cannot mask that one.
;
; Port 70h is WRITE ONLY: the NMI bit cannot be read back, so the choice is
; made explicitly at every write rather than inherited. clk_at_done ends each
; burst by parking the index at 0Dh - read-only, so a stray later write to
; 71h cannot corrupt anything - with the NMI bit CLEAR, which is the state
; the BIOS leaves after POST.
; -----------------------------------------------------------------------------
; clk_at_get - read CMOS register AL
; in:  AL = index 00h..3Fh; IF = 0 already
; out: AL = the byte; preserves every other register
; -----------------------------------------------------------------------------
clk_at_get:
    push dx
    or al, 0x80
    mov dx, CLK_AT_IDX
    out dx, al
    jmp short $+2               ; I/O settling: two bytes, and it removes a
    inc dx                      ; whole class of works-here-fails-there bug
    in al, dx
    pop dx
    ret

; -----------------------------------------------------------------------------
; clk_at_put - write AH to CMOS register AL
; in:  AL = index, AH = value; IF = 0 already
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
clk_at_put:
    push ax
    push dx
    or al, 0x80
    mov dx, CLK_AT_IDX
    out dx, al
    jmp short $+2
    inc dx
    mov al, ah
    out dx, al
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; clk_at_lock - one guarded read, for callers not already inside a burst
; in:  AL = index; out: AL = the byte; preserves every other register
; -----------------------------------------------------------------------------
clk_at_lock:
    pushf
    cli
    call clk_at_get
    popf
    ret

; -----------------------------------------------------------------------------
; clk_at_done - leave the index somewhere harmless, NMI back on
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
clk_at_done:
    push ax
    push dx
    pushf
    cli
    mov al, 0x0D                ; read-only, and bit 7 clear = NMI enabled
    mov dx, CLK_AT_IDX
    out dx, al
    jmp short $+2
    inc dx
    in al, dx                   ; a write to 70h must always be followed by
    popf                        ; an access of 71h, and soon (PS/2 TR)
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; clk_at_probe - is there an MC146818 at 70h/71h?
; out: CF = 0 present; preserves all registers
;
; Two structural tests, no writes, both bounded:
;
;  - Register A's divider bits must read 010 - the ONLY pattern that runs the
;    oscillator, and what every BIOS leaves there. This is the cheap early-out,
;    and it has to be a structural test rather than "is it 0FFh", because an
;    absent chip does not reliably float: an unclaimed IN returns the last
;    byte DRIVEN on the bus, and one instruction earlier we drove the index
;    ourselves. So a bare 70h/71h pair on a 5150 answers 8Ah, not 0FFh, and an
;    0FFh test would never fire while the UIP poll below burned its whole
;    budget on a machine that has no clock at all. 0FFh gives 0x70 and the
;    held index gives 0x00; neither is 0x20.
;  - UIP must be observed LOW. On a live chip it is high for at most 2.228 ms
;    per second; on an absent one it appears stuck forever. This is the test
;    modern Linux settled on, after the "Register D bits 6..0 must be zero"
;    one turned out to be too tight on later chipsets - which is why that
;    test is not here.
;
; The poll is bounded by a plain counter, not by [ticks]: clk_init runs
; before the PIT hook has done anything (SPEC.md 15). And it takes the guard
; PER ACCESS rather than around the loop - up to 2.3 ms with interrupts off
; would be 40 times the tick period.
; -----------------------------------------------------------------------------
clk_at_probe:
    push ax
    push cx
    mov al, 0x0A
    call clk_at_lock
    and al, 0x70
    cmp al, 0x20
    jne .no
    mov cx, CLK_UIP_MAX
.uip:
    mov al, 0x0A
    call clk_at_lock
    test al, 0x80
    jz .yes
    loop .uip
.no:
    call clk_at_done
    pop cx
    pop ax
    stc
    ret
.yes:
    call clk_at_done
    pop cx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; clk_at_read - read the AT clock into clk_tb_*
; out: CF = 0 and the scratch filled; preserves all registers
;
; The hybrid guard, which is what Linux converged on after years of bug
; reports: the payload is bracketed BOTH by the hardware's own UIP bit and by
; a seconds comparison. The seconds value is stashed BEFORE the first UIP
; test on purpose - an NMI that lands just after that test could otherwise
; hide a whole update cycle - and both are re-checked afterwards, because
; "UIP is low" only promises 244 us and an NMI can outlast it. Either guard
; alone has a hole; together they do not.
; -----------------------------------------------------------------------------
clk_at_read:
    push ax
    push bx
    push cx
    push dx
    mov cx, CLK_RETRY
.try:
    pushf
    cli
    xor al, al
    call clk_at_get
    mov bh, al                  ; the seconds, stashed before the UIP test
    mov al, 0x0A
    call clk_at_get
    test al, 0x80
    jnz .again                  ; an update is imminent or running
    xor al, al
    call clk_at_get
    cmp al, bh
    jne .again                  ; one happened between the two reads
    mov [clk_rr+0], al
    mov al, 0x02
    call clk_at_get
    mov [clk_rr+1], al
    mov al, 0x04
    call clk_at_get
    mov [clk_rr+2], al          ; raw: bit 7 is PM in 12-hour mode
    mov al, 0x07
    call clk_at_get
    mov [clk_rr+3], al
    mov al, 0x08
    call clk_at_get
    mov [clk_rr+4], al
    mov al, 0x09
    call clk_at_get
    mov [clk_rr+5], al          ; the year, two digits and no more
    mov al, 0x32
    call clk_at_get
    mov [clk_rr+6], al          ; the century byte, if this BIOS keeps one
    mov al, 0x0B
    call clk_at_get
    mov [clk_rb], al            ; Register B: DM and 24/12 live here, and
    mov al, 0x0A                ; reading it here reads it under the guard
    call clk_at_get
    test al, 0x80
    jnz .again                  ; an update started under the burst
    xor al, al
    call clk_at_get
    cmp al, bh
    jne .again
    popf
    call clk_at_done
    mov al, [clk_rb]
    and al, 0x04                ; DM: 0 = BCD, 1 = binary
    call clk_at_dec
    jnc .ok
    mov al, [clk_rb]            ; a DM bit that lies is a known failure mode
    and al, 0x04                ; on PC hardware - Linux ignores it outright
    xor al, 0x04                ; and always decodes BCD. Try the other way
    call clk_at_dec             ; round rather than throw the clock away
    jnc .ok
    jmp short .fail
.again:
    popf
    dec cx                      ; not `loop`: the burst above is well past a
    jz .fail                    ; short jump's reach
    jmp .try
.fail:
    call clk_at_done
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret
.ok:
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; clk_at_num - one raw register byte to binary, in the mode [clk_rbin] says
; in:  AL = the byte; out: AL = the value, CF = 1 = not a legal encoding
; -----------------------------------------------------------------------------
clk_at_num:
    cmp byte [clk_rbin], 0
    jne .bin
    call clk_bcd                ; packed BCD, and it validates both nibbles
    ret
.bin:
    clc
    ret

; -----------------------------------------------------------------------------
; clk_at_dec - decode clk_rr[] into clk_tb_*
; in:  AL = 0 for BCD, 4 for binary (Register B's DM bit, in place)
; out: CF = 1 = some field is out of range; preserves all registers
; -----------------------------------------------------------------------------
clk_at_dec:
    push ax
    push bx
    push cx
    mov [clk_rbin], al
    mov al, [clk_rr+0]
    call clk_at_num
    jc .no
    cmp al, 59
    ja .no
    mov [clk_tb_s], al
    mov al, [clk_rr+1]
    call clk_at_num
    jc .no
    cmp al, 59
    ja .no
    mov [clk_tb_mi], al

    ; --- the hour: strip the PM bit BEFORE decoding. 1 PM in 12-hour BCD
    ; --- is 81h, which is legal BCD for 81 and then fails a <= 23 test
    mov al, [clk_rr+2]
    mov bl, al
    and bl, 0x80
    and al, 0x7F
    call clk_at_num
    jc .no
    test byte [clk_rb], 0x02
    jnz .h24                    ; 24/12 = 1 means the value is already 0..23
    or al, al
    jz .no                      ; 12-hour hours run 1..12
    cmp al, 12
    ja .no
    jne .h12
    xor al, al                  ; 12 AM is 0 and 12 PM is 12: the conversion
.h12:                           ; is (h mod 12), never a bare +12
    or bl, bl
    jz .hok
    add al, 12
    jmp short .hok
.h24:
    cmp al, 23
    ja .no
.hok:
    mov [clk_tb_h], al

    mov al, [clk_rr+3]
    call clk_at_num
    jc .no
    or al, al
    jz .no
    cmp al, 31
    ja .no
    mov [clk_tb_d], al
    mov al, [clk_rr+4]
    call clk_at_num
    jc .no
    or al, al
    jz .no
    cmp al, 12
    ja .no
    mov [clk_tb_mo], al
    mov al, [clk_rr+5]
    call clk_at_num
    jc .no
    cmp al, 99
    ja .no
    mov bl, al                  ; BL = the two-digit year

    ; --- the century. CMOS 32h is BIOS-maintained RAM, not a register, so
    ; --- it is BCD whatever DM says - and many BIOSes never write it back.
    ; --- Believe it only if it decodes to 19 or 20; otherwise window the
    ; --- two digits over SPEC.md 37's own 1980..2099 range.
    mov al, [clk_rr+6]
    call clk_bcd
    jc .win
    cmp al, 19
    jb .win
    cmp al, 20
    ja .win
    mov [clk_cent], al          ; remembered: clk_at_write puts it back
    jmp short .year
.win:
    mov byte [clk_cent], 0
    mov al, 20
    cmp bl, 80
    jb .year
    mov al, 19
.year:
    mov bh, 100
    mul bh                      ; AX = century * 100
    mov cl, bl
    xor ch, ch
    add ax, cx
    mov [clk_tb_y], ax
    pop cx
    pop bx
    pop ax
    clc
    ret
.no:
    pop cx
    pop bx
    pop ax
    stc
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
    call clk_tobcd
.bin:
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
    push cx
    push dx
    pushf
    cli
    mov al, 0x0B
    call clk_at_get
    mov [clk_rb], al            ; save_control, restored whole at the end
    mov ah, al
    or ah, 0x80                 ; SET
    mov al, 0x0B
    call clk_at_put

    mov al, [clk_sec]
    call clk_at_enc
    mov ah, al
    xor al, al
    call clk_at_put
    mov al, [clk_min]
    call clk_at_enc
    mov ah, al
    mov al, 0x02
    call clk_at_put
    mov al, [clk_hour]
    call clk_at_hour
    mov ah, al
    mov al, 0x04
    call clk_at_put
    mov al, [clk_day]
    call clk_at_enc
    mov ah, al
    mov al, 0x07
    call clk_at_put
    mov al, [clk_mon]
    call clk_at_enc
    mov ah, al
    mov al, 0x08
    call clk_at_put
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
    call clk_at_enc
    mov ah, al
    mov al, 0x09
    call clk_at_put
    cmp byte [clk_cent], 0
    je .nocent                  ; this BIOS keeps no century byte, so do not
    mov al, bl                  ; invent one. 32h is outside the AT CMOS
    call clk_tobcd              ; checksum range (10h..2Dh), so writing it
    mov ah, al                  ; needs no checksum fixup - and equally, must
    mov al, 0x32                ; not trigger one. ALWAYS BCD, never clk_at_enc:
    call clk_at_put             ; 32h is BIOS RAM and DM does not describe it,
                                ; which is how the read decodes it too
.nocent:
    mov al, 0x0B
    mov ah, [clk_rb]
    call clk_at_put             ; SET cleared, every other bit as we found it
    popf
    call clk_at_done
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Tier CLK_T_NS (2): the National MM58167 at 2C0h - AST SixPakPlus and clones
; (probed FOURTH; the tier numbers are the CLK_T_* enum, not the probe order,
; because ctrl.inc's cp_rtcnam is indexed by [clk_tier])
; =============================================================================
CLK_NS_MS   equ 0x00            ; counter: milliseconds, HIGH nibble only
CLK_NS_SEC  equ 0x02
CLK_NS_MIN  equ 0x03
CLK_NS_HR   equ 0x04            ; 24-hour always; this chip has no AM/PM bit
CLK_NS_DOW  equ 0x05            ; 1..7, three bits wide
CLK_NS_DOM  equ 0x06
CLK_NS_MON  equ 0x07
CLK_NS_RMS  equ 0x08            ; RAM: no LOW nibble - the second signature
CLK_NS_LMON equ 0x09            ; RAM: the month we last wrote
CLK_NS_YRA  equ 0x0A            ; RAM: year - 1980, the ASTCLOCK convention
CLK_NS_OK   equ 0x0B            ; RAM: the magic below = those bytes are ours
CLK_NS_SIG  equ 0x0D            ; RAM: no HIGH nibble - the first signature
CLK_NS_YRB  equ 0x0E            ; RAM: year - 1980, what 86Box's cards use
CLK_NS_STAT equ 0x14            ; D0 = a counter rolled during the read
CLK_NS_MAGIC equ 222

; -----------------------------------------------------------------------------
; clk_xt_charge - drive all-ones onto the ISA data bus before a 2C0h probe
; out: nothing (all registers preserved)
;
; An IN from an unimplemented port does not reliably float to FFh on a real
; XT: the bus holds whatever was last driven, which is the byte our own
; previous OUT put there. Every probe below is built on "an absent card
; reads FFh", so the value has to be made true first. Port 80h is the POST
; diagnostic port - an unused DMA page register slot, written by every BIOS
; on the planet as an I/O delay, and harmless on both bus generations. NOT
; GLaTICK's 0BFh: that address is inside the AT's slave-PIC window.
; -----------------------------------------------------------------------------
clk_xt_charge:
    push ax
    mov al, 0xFF
    out 0x80, al
    pop ax
    ret

; -----------------------------------------------------------------------------
; clk_ns_get / clk_ns_put - one register, direct-mapped at base + N
; in:  AL = register 00h..1Fh (clk_ns_put: AH = the value)
; out: clk_ns_get: AL = the byte. Preserves every other register.
; -----------------------------------------------------------------------------
clk_ns_get:
    push dx
    mov dl, al
    xor dh, dh
    add dx, CLK_XT_BASE
    in al, dx
    pop dx
    ret

clk_ns_put:
    push ax
    push dx
    mov dl, al
    xor dh, dh
    add dx, CLK_XT_BASE
    mov al, ah
    out dx, al
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; clk_ns_fields - the six counters into clk_tb_*
; out: CF = 1 = something is not a plausible date; preserves all registers
; -----------------------------------------------------------------------------
clk_ns_fields:
    push ax
    mov al, CLK_NS_SEC
    call clk_ns_get
    call clk_bcd
    jc .no
    cmp al, 59
    ja .no
    mov [clk_tb_s], al
    mov al, CLK_NS_MIN
    call clk_ns_get
    call clk_bcd
    jc .no
    cmp al, 59
    ja .no
    mov [clk_tb_mi], al
    mov al, CLK_NS_HR
    call clk_ns_get
    call clk_bcd
    jc .no
    cmp al, 23
    ja .no
    mov [clk_tb_h], al
    mov al, CLK_NS_DOW
    call clk_ns_get             ; three bits wide, so the whole BYTE must be
    or al, al                   ; 1..7 - which is also what makes this a
    jz .no                      ; structural test and not just a range one
    cmp al, 7
    ja .no
    mov al, CLK_NS_DOM
    call clk_ns_get
    call clk_bcd
    jc .no
    or al, al
    jz .no
    cmp al, 31
    ja .no
    mov [clk_tb_d], al
    mov al, CLK_NS_MON
    call clk_ns_get
    call clk_bcd
    jc .no
    or al, al
    jz .no
    cmp al, 12
    ja .no
    mov [clk_tb_mo], al
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; clk_ns_probe - is there an MM58167 at 2C0h?
; out: CF = 0 present; preserves all registers
;
; Two half-registers physically do not exist on this part - register 00h has
; no units digit and RAM register 0Dh has no high nibble ("No RAM Exists" in
; the datasheet's own table) - and that is what every driver since ASTCLOCK
; has used to recognise it. AST's technical bulletin describes the same test.
;
; The read-only halves run FIRST and the counters have to read as a date
; before either write happens, because 2C0h is a contested address: the
; Toshiba's TC8521 puts its MODE register at 2CDh, and blindly writing AAh
; there would move a stranger's chip onto its NVRAM page. Rung 3 has already
; had its read-only look by the time this runs.
;
; The SECOND write test is what makes the discrimination deterministic rather
; than merely likely. RAM 08h has no LOW nibble on this part, so FFh must read
; back with its low nibble clear; on a TC8521 that same port is the 10-day
; counter, two bits wide, and FFh reads back as 3. Both bytes are saved and
; restored whatever the verdict.
; -----------------------------------------------------------------------------
clk_ns_probe:
    push ax
    push bx
    call clk_xt_charge
    mov al, CLK_NS_MS
    call clk_ns_get
    test al, 0x0F               ; no units digit exists at all...
    jnz .no
    cmp al, 0x90                ; ...and the tens digit is a BCD 0..9
    ja .no
    mov al, CLK_NS_RMS
    call clk_ns_get
    test al, 0x0F               ; RAM 08h likewise has no low nibble
    jnz .no
    ; --- and a veto against the 4-bit part, before ANY write ---------------
    ; Rung 3 having looked is not the same as rung 3 having claimed: it is
    ; skipped outright when the BIOS could not read the clock, and refused
    ; whenever the digits disagree with it. In both of those cases control
    ; arrives here, and the two writes below would land on a TC8521's MODE
    ; register and its 10-day counter. So the veto is structural: if the
    ; ports read as a plausible RP5C01 page 0, this is not our chip.
    ;
    ; It is a veto and not a proof, and it deliberately does NOT include
    ; rung 3's MODE-page-0 test. On an MM58167 that same port is RAM 0Dh -
    ; arbitrary battery-backed bytes - so "the low two bits are not zero"
    ; would refuse a genuine SixPakPlus three boots in four. A chip parked
    ; off page 0 is therefore left to the two half-register gates above and
    ; to the restore, which is where the residual risk sits.
    call clk_rp_fields
    jnc .no
    call clk_ns_scan            ; the counters, with the same rollover
    jc .no                      ; retries the read gets - a ripple caught
                                ; mid-carry is not "no card"
    ; --- 0Dh has no HIGH nibble, so AAh must read back exactly 0Ah --------
    ; IF THE USER'S SIXPAKPLUS IS NEVER DETECTED, THIS IS THE FIRST LINE TO
    ; SUSPECT. The datasheet says unused bits "are held at a logical zero
    ; during a read", 86Box masks reg 0Dh with 0Fh, and GLaTICK's code
    ; requires exactly 0Ah - but GLaTICK's own COMMENT says "if 0FAh or 0AAh,
    ; then likely NS", i.e. its author expected real silicon to float the
    ; absent nibble to ones. Three sources say 0Ah and one comment says
    ; maybe not. Relaxing this to "the low nibble is A" would then let a
    ; 4-bit part through here, and the whole 4-bit rejection would rest on
    ; the 08h test below - which has the same unused-nibble question in
    ; mirror image. So: strict, and documented, rather than quietly loose.
    mov al, CLK_NS_SIG
    mov ah, 0xAA
    call clk_ns_half
    cmp al, 0x0A
    jne .no
    mov al, CLK_NS_RMS          ; 08h: no LOW nibble, so FFh reads back F0h.
    mov ah, 0xFF                ; A 4-bit part at the same address stores the
    call clk_ns_half            ; low nibble instead and fails here
    test al, 0x0F
    jnz .no
    pop bx
    pop ax
    clc
    ret
.no:
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; clk_ns_half - write AH to register AL, read it back, put the original back
; in:  AL = register, AH = the pattern
; out: AL = what it read back; clobbers AH; preserves every other register
;
; The restore is on the ONE path out, so no verdict can skip it.
; -----------------------------------------------------------------------------
clk_ns_half:
    push bx
    push cx
    mov cl, al
    mov ch, ah
    call clk_ns_get
    mov bl, al                  ; the original
    mov al, cl
    mov ah, ch
    call clk_ns_put
    mov al, cl
    call clk_ns_get
    mov bh, al                  ; what it answered
    mov al, cl
    mov ah, bl
    call clk_ns_put
    mov al, bh
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; clk_ns_read - read the MM58167 into clk_tb_*
; out: CF = 0 and the scratch filled; preserves all registers
;
; This is a ripple counter with no read latch and no update-in-progress gate.
; What it has instead is a status bit at 14h that is set when a rollover
; rippled through during a read and CLEARED BY THE READ of 14h itself - so
; the sequence is read-14h-to-clear, read the fields, read 14h again, and a
; set bit means throw the whole set away. Bounded: the loop sits inside a
; critical section, so an unbounded one on a card that is not there would
; hang the machine outright rather than merely spin.
; -----------------------------------------------------------------------------
clk_ns_read:
    push ax
    push dx
    call clk_ns_scan
    jc .out
    call clk_ns_year
    cmp byte [clk_nsrol], 0
    je .clean
    mov ax, [clk_tb_y]
    mov dl, [clk_tb_mo]
    call clk_ns_stamp           ; the roll fired: put the corrected year and
.clean:                         ; a fresh month shadow BACK on the card, or
    clc                         ; the same single +1 applies again next year
.out:                           ; and the date drifts one year low per year
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; clk_ns_scan - the six counters, guarded (internal; the probe uses it too)
; out: CF = 1 = no coherent read; preserves all registers
; -----------------------------------------------------------------------------
clk_ns_scan:
    push ax
    push bx
    push cx
    mov cx, CLK_RETRY
.try:
    pushf
    cli
    mov al, CLK_NS_STAT
    call clk_ns_get             ; read-and-clear, so a stale flag from some
    call clk_ns_fields          ; earlier rollover does not burn a retry
    mov bl, 0
    jnc .clean
    mov bl, 1
.clean:
    mov al, CLK_NS_STAT
    call clk_ns_get
    popf
    and al, 1
    or al, bl
    jz .good
    loop .try
    pop cx
    pop bx
    pop ax
    stc
    ret
.good:
    pop cx
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; clk_ns_year - reconstruct the year, which this chip does not have
; out: [clk_tb_y]; preserves all registers
;
; The MM58167 counts milliseconds up to months and stops. Every vendor put
; the year in one of the eight alarm-compare RAM bytes and none of them
; agreed: ASTCLOCK and GLaTICK use 0Ah, 86Box's own cards use 0Eh, others use
; 09h in BCD. Both of the two that matter are read here and clk_ns_write
; stamps BOTH, so a card we have written reads correctly under either.
;
; 0Bh = 222 is GLaTICK's "these bytes are ours" magic. Only when it is
; present is the last-month shadow at 09h believed - that is what lets a
; machine that was switched off over New Year come back with the right year.
; -----------------------------------------------------------------------------
clk_ns_year:
    push ax
    mov byte [clk_nsrol], 0
    mov al, CLK_NS_OK
    call clk_ns_get
    cmp al, CLK_NS_MAGIC
    jne .other
    mov al, CLK_NS_YRA
    call clk_ns_get
    cmp al, CLK_YMAX - CLK_YMIN + 1
    jae .other
    call clk_ns_yset
    call clk_ns_roll
    jmp short .out
.other:
    mov al, CLK_NS_YRB
    call clk_ns_get
    cmp al, CLK_YMAX - CLK_YMIN + 1
    jae .none
    call clk_ns_yset
    jmp short .out
.none:
    mov ax, CLK_DEF_Y           ; nobody has ever told this card a year. Keep
    mov [clk_tb_y], ax          ; the date and time it does have, and take
.out:                           ; SPEC.md 37's fallback year for the rest
    pop ax
    ret

; -----------------------------------------------------------------------------
; clk_ns_yset - AL = year - CLK_YMIN, into [clk_tb_y] (internal)
; -----------------------------------------------------------------------------
clk_ns_yset:
    push ax
    xor ah, ah
    add ax, CLK_YMIN
    mov [clk_tb_y], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; clk_ns_roll - a month that went backwards means a New Year passed
; out: [clk_tb_y] possibly incremented; preserves all registers
; -----------------------------------------------------------------------------
clk_ns_roll:
    push ax
    mov al, CLK_NS_LMON
    call clk_ns_get
    call clk_bcd
    jc .out
    or al, al
    jz .out
    cmp al, 12
    ja .out
    cmp al, [clk_tb_mo]
    jbe .out
    inc word [clk_tb_y]         ; clk_commit clamps the result
    mov byte [clk_nsrol], 1
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; clk_ns_stamp - write the year bytes and the month shadow back to the card
; in:  AX = the year, DL = the month
; out: nothing (all registers preserved)
;
; The chip has no year register, so both of the conventions in the wild get
; written: 0Ah is ASTCLOCK's and 0Eh is the one 86Box's cards read. 0Bh's
; magic is what says the three are ours, and 09h is the shadow that lets a
; later boot notice a New Year. Shared by clk_ns_write and by the roll
; correction in clk_ns_read.
; -----------------------------------------------------------------------------
clk_ns_stamp:
    push ax
    push bx
    push dx
    pushf
    cli                         ; every other 2C0h burst runs guarded, and
                                ; this one is reached from clk_probe
    cmp ax, CLK_YMIN            ; clamp BEFORE the subtraction: clk_commit's
    jae .ylo                    ; clamp happens later and would not save a
    mov ax, CLK_YMIN            ; card stamped 120 from being unreadable by
.ylo:                           ; both of the readers next boot
    cmp ax, CLK_YMAX
    jbe .yok
    mov ax, CLK_YMAX
.yok:
    sub ax, CLK_YMIN
    mov bl, al                  ; BL = the year byte, both conventions
    mov ah, bl
    mov al, CLK_NS_YRA
    call clk_ns_put
    mov ah, bl
    mov al, CLK_NS_YRB
    call clk_ns_put
    mov al, dl
    call clk_tobcd
    mov ah, al
    mov al, CLK_NS_LMON
    call clk_ns_put
    mov ah, CLK_NS_MAGIC
    mov al, CLK_NS_OK
    call clk_ns_put
    popf
    pop dx
    pop bx
    pop ax
    ret

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
clk_ns_write:
    push ax
    push bx
    push cx
    push dx
    pushf
    cli
    mov al, [clk_mon]
    call clk_tobcd
    mov ah, al
    mov al, CLK_NS_MON
    call clk_ns_put
    mov al, [clk_day]
    call clk_tobcd
    mov ah, al
    mov al, CLK_NS_DOM
    call clk_ns_put
    call clk_dow
    inc al                      ; 1..7, and our own probe insists on it
    mov ah, al
    mov al, CLK_NS_DOW
    call clk_ns_put
    mov al, [clk_hour]
    call clk_tobcd
    mov ah, al
    mov al, CLK_NS_HR
    call clk_ns_put
    mov al, [clk_min]
    call clk_tobcd
    mov ah, al
    mov al, CLK_NS_MIN
    call clk_ns_put
    mov al, [clk_sec]
    call clk_tobcd
    mov ah, al
    mov al, CLK_NS_SEC
    call clk_ns_put
    ; --- the year, in both conventions, plus the bookkeeping that says so
    mov ax, [clk_year]
    mov dl, [clk_mon]
    call clk_ns_stamp
    popf
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Tier CLK_T_RP (3): the Ricoh RP5C01 / Toshiba TC8521 at 2C0h (probed THIRD)
; =============================================================================
; A 4-bit part on an 8-bit bus: sixteen direct-mapped ports, ONE BCD DIGIT
; per port, and the upper nibble of every read is undefined - so every read
; is masked. MODE at 0Dh selects one of four pages for ports 00h..0Ch, which
; is why nothing here may be believed until the page is known to be 0.
CLK_RP_MODE equ 0x0D            ; b3 timer enable, b2 alarm enable, b1-b0 page
CLK_RP_DOW  equ 0x06            ; 0-based on this chip, unlike the MM58167
CLK_RP_24   equ 0x0A            ; page 1: 1 = 24-hour
CLK_RP_LEAP equ 0x0B            ; page 1: the leap counter, year mod 4

clk_rp_get:
    push dx
    mov dl, al
    xor dh, dh
    add dx, CLK_XT_BASE
    in al, dx
    and al, 0x0F                ; only D0-D3 exist; the rest floats
    pop dx
    ret

clk_rp_put:
    push ax
    push dx
    mov dl, al
    xor dh, dh
    add dx, CLK_XT_BASE
    mov al, ah
    and al, 0x0F
    out dx, al
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; clk_rp_digit - one digit, range-checked
; in:  AL = port offset, AH = the largest legal digit
; out: AL = 0..AH, CF = 1 = out of range; preserves every other register
; -----------------------------------------------------------------------------
clk_rp_digit:
    push bx
    mov bl, ah
    call clk_rp_get
    cmp al, bl
    ja .no
    pop bx
    clc
    ret
.no:
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; clk_rp_digits - the thirteen page-0 digits into clk_tb_*
; out: CF = 1 = a digit is outside its range; preserves all registers
;
; The hour tens are allowed the chip's full 0..3 and the joined hour is NOT
; range-checked, which looks lax and is deliberate. In 12-hour mode bit 1 of
; that register is the PM flag, so an afternoon reading gives tens of 2 or 3
; and joins to 20..39; reading the 12/24 bit to tell would mean switching to
; page 1, and a page switch is a WRITE, which this routine may not do before
; the chip is identified. Rung 3's real gate is the BIOS comparison, and no
; joined hour above 23 can ever equal a BIOS hour - so nothing is let
; through. What the wider range buys is rung 2: it uses this routine as its
; "could this be a 4-bit part?" veto, and a range check that rejected every
; PM reading would switch that veto off for nine hours a day.
; -----------------------------------------------------------------------------
clk_rp_digits:
    push ax
    push bx
    push cx
    mov al, 0x01                ; 10-second
    mov ah, 5
    call clk_rp_digit
    jc .no
    mov bl, al
    mov al, 0x00                ; 1-second
    mov ah, 9
    call clk_rp_digit
    jc .no
    call clk_rp_join            ; AL = BL*10 + AL
    mov [clk_tb_s], al
    mov al, 0x03
    mov ah, 5
    call clk_rp_digit
    jc .no
    mov bl, al
    mov al, 0x02
    mov ah, 9
    call clk_rp_digit
    jc .no
    call clk_rp_join
    mov [clk_tb_mi], al
    mov al, 0x05                ; 10-hour: the chip's full 0..3, PM bit and
    mov ah, 3                   ; all (see the note above)
    call clk_rp_digit
    jc .no
    mov bl, al
    mov al, 0x04
    mov ah, 9
    call clk_rp_digit
    jc .no
    call clk_rp_join
    mov [clk_tb_h], al
    mov al, CLK_RP_DOW          ; 0..6, and 0 IS legal here (Sunday)
    mov ah, 6
    call clk_rp_digit
    jc .no
    mov al, 0x08                ; 10-day
    mov ah, 3
    call clk_rp_digit
    jc .no
    mov bl, al
    mov al, 0x07
    mov ah, 9
    call clk_rp_digit
    jc .no
    call clk_rp_join
    or al, al
    jz .no
    cmp al, 31
    ja .no
    mov [clk_tb_d], al
    mov al, 0x0A                ; 10-month
    mov ah, 1
    call clk_rp_digit
    jc .no
    mov bl, al
    mov al, 0x09
    mov ah, 9
    call clk_rp_digit
    jc .no
    call clk_rp_join
    or al, al
    jz .no
    cmp al, 12
    ja .no
    mov [clk_tb_mo], al
    mov al, 0x0C                ; 10-year
    mov ah, 9
    call clk_rp_digit
    jc .no
    mov bl, al
    mov al, 0x0B
    mov ah, 9
    call clk_rp_digit
    jc .no
    call clk_rp_join
    mov [clk_rpyy], al          ; the two digits; clk_rp_probe decides what
    xor ah, ah                  ; they mean (see clk_rp_year)
    mov [clk_tb_y], ax
    pop cx
    pop bx
    pop ax
    clc
    ret
.no:
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; clk_rp_join - AL = BL*10 + AL (internal); clobbers AX
; -----------------------------------------------------------------------------
clk_rp_join:
    push bx
    push dx
    mov dl, al
    mov al, bl
    mov bl, 10
    mul bl
    add al, dl
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; clk_rp_fields - clk_rp_digits with a rollover guard
; out: CF = 1 = unusable; preserves all registers
;
; There is no update-in-progress bit on this chip either, and no rollover
; status bit like the MM58167's - so the guard is the 1-second digit read
; before and after the sweep. A digit that is simply out of range will not
; improve on a retry, so that case gives up at once.
; -----------------------------------------------------------------------------
clk_rp_fields:
    push ax
    push bx
    push cx
    mov cx, CLK_RETRY
.try:
    pushf
    cli
    xor al, al
    call clk_rp_get
    mov bh, al
    call clk_rp_digits
    mov bl, 0
    jnc .clean
    mov bl, 1
.clean:
    xor al, al
    call clk_rp_get
    popf
    or bl, bl
    jnz .no                     ; a bad digit, not a torn read
    cmp al, bh
    je .ok
    loop .try
.no:
    pop cx
    pop bx
    pop ax
    stc
    ret
.ok:
    pop cx
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; clk_rp_probe - is the chip at 2C0h an RP5C01, and is it OUR clock?
; out: CF = 0 present and identified; preserves all registers
;
; **The whole gate is agreement with the BIOS**, and that is what makes this
; rung safe on a machine whose 2C0h holds something else entirely. Matching
; the hour, the day, the month AND the year confirms the chip, the base
; address, the direct (rather than latched) addressing mode and the page in
; one test, without writing a single byte. Seconds and minutes are left out
; because they can legitimately tick between the two reads.
;
; A chip parked on any page but 0 is refused rather than moved: moving it is
; a write, and this runs before we know what we are talking to.
;
; The two year digits are a SOFTWARE convention, not silicon - the chip has
; no century - so both of the conventions in the wild are tried and the one
; that agrees is remembered in [clk_rpep], because clk_rp_write has to store
; the year back the same way the machine's own ROM reads it.
; -----------------------------------------------------------------------------
clk_rp_probe:
    push ax
    call clk_xt_charge
    mov al, CLK_RP_MODE
    call clk_rp_get
    test al, 0x03
    jnz .no
    call clk_rp_fields
    jc .no
    mov al, [clk_tb_h]
    cmp al, [clk_rf_h]
    jne .no
    mov al, [clk_tb_d]
    cmp al, [clk_rf_d]
    jne .no
    mov al, [clk_tb_mo]
    cmp al, [clk_rf_mo]
    jne .no
    mov byte [clk_rpep], 0      ; convention A: the digits are year mod 100,
    call clk_rp_year            ; and the century is the reference's
    mov ax, [clk_tb_y]
    cmp ax, [clk_rf_y]
    je .yes
    mov byte [clk_rpep], 1      ; convention B: the digits are year - 1980,
    call clk_rp_year            ; which is what the one documented model of
    mov ax, [clk_tb_y]          ; a sibling Toshiba machine uses
    cmp ax, [clk_rf_y]
    jne .no
.yes:
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; clk_rp_year - [clk_tb_y] from [clk_rpyy] under convention [clk_rpep]
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
clk_rp_year:
    push ax
    push bx
    push dx
    mov al, [clk_rpyy]
    xor ah, ah
    cmp byte [clk_rpep], 0
    jne .e80
    mov bx, ax
    mov ax, [clk_rf_y]
    mov dl, 100
    div dl                      ; AL = the reference century
    xor ah, ah
    mul dl
    add ax, bx
    jmp short .out
.e80:
    add ax, CLK_YMIN
.out:
    mov [clk_tb_y], ax
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; clk_rp_yy - the two year digits to store, in this machine's own convention
; out: AL = 0..99; preserves every other register
; -----------------------------------------------------------------------------
clk_rp_yy:
    push bx
    push dx
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
    pop dx
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
    push cx
    push dx
    pushf
    cli
    mov al, CLK_RP_MODE
    call clk_rp_get
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
    call clk_rp_get
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
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Tier CLK_T_BIOS (4): int 1Ah, the BIOS - probed for its answer SECOND
; (rung 3 is checked against it) and adopted as a tier LAST
; =============================================================================
; -----------------------------------------------------------------------------
; clk_bios_read - read the BIOS clock into clk_tb_*
; in:  nothing
; out: CF = 0 and the scratch filled; CF = 1 and the scratch is PARTLY
;      written - the fields validated before the failing one are already in
;      it. That is harmless and deliberate: clk_tb_* is module-private and
;      clk_commit is its only reader, and clk_commit runs only after a CF = 0
;      read. The "nothing written" guarantee belongs to the LIVE clock, not
;      to this buffer.
; clobbers: nothing else (flags)
;
; Deliberately paranoid (SPEC.md 37): the machines this OS targets are
; exactly the ones that may have no CMOS clock at all, and a BIOS that does
; not implement an int 1Ah function may simply iret, leaving CF and the
; registers as it found them. So CX/DX are poisoned and CF is set before
; each call, and every byte must be valid packed BCD and in range.
; -----------------------------------------------------------------------------
clk_bios_read:
    push ax
    push bx
    push cx
    push dx

    ; --- time: AH=02h, CH/CL/DH = hour/min/sec BCD ---------------------------
    mov cx, 0xFFFF
    mov dx, 0xFFFF
    mov ah, 0x02
    stc
    int 0x1a
    jc .none                    ; "clock not operating"
    cmp cx, 0xFFFF
    je .none                    ; poison survived: the call did nothing
    mov al, ch
    call clk_bcd
    jc .none
    cmp al, 23
    ja .none
    mov [clk_tb_h], al
    mov al, cl
    call clk_bcd
    jc .none
    cmp al, 59
    ja .none
    mov [clk_tb_mi], al
    mov al, dh
    call clk_bcd
    jc .none
    cmp al, 59
    ja .none
    mov [clk_tb_s], al

    ; --- date: AH=04h, CH/CL = century/year BCD, DH/DL = month/day ----------
    mov cx, 0xFFFF
    mov dx, 0xFFFF
    mov ah, 0x04
    stc
    int 0x1a
    jc .none
    cmp cx, 0xFFFF
    je .none
    mov bl, 0                   ; the century, if this BIOS keeps one. A byte
    mov al, ch                  ; that does not decode is NOT fatal - some
    call clk_bcd                ; BIOSes simply do not maintain it, and the
    jc .cwin                    ; window rule below covers SPEC.md 37's own
    cmp al, 19                  ; 1980..2099 range without it
    jb .cwin
    cmp al, 20
    ja .cwin
    mov bl, al
.cwin:
    mov al, cl                  ; year within the century
    call clk_bcd
    jc .none
    cmp al, 99
    ja .none
    mov bh, al
    or bl, bl
    jnz .cent
    mov bl, 20
    cmp bh, 80
    jb .cent
    mov bl, 19
.cent:
    mov al, dh                  ; month
    call clk_bcd
    jc .none
    or al, al
    jz .none
    cmp al, 12
    ja .none
    mov [clk_tb_mo], al
    mov al, dl                  ; day
    call clk_bcd
    jc .none
    or al, al
    jz .none
    cmp al, 31
    ja .none
    mov [clk_tb_d], al
    mov al, bl                  ; year = century*100 + yy
    mov bl, 100
    mul bl                      ; AX = AL * BL
    mov bl, bh
    mov bh, 0
    add ax, bx
    mov [clk_tb_y], ax
    clc
    jmp .out
.none:
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; clk_rtc_write - push the live time back to the hardware RTC
; in:  nothing; NO gfx lock held (SPEC.md 13/31.1)
; out: nothing (all registers preserved)
;
; A machine without an RTC returns immediately: the clock is the PIT's. The
; "outside the gfx lock" rule predates the ladder - only rung 5 calls BIOS
; now - but it stays, because the caller cannot know which rung answered and
; ui_task step 3 is a perfectly good place to drain [clk_dirty] from.
; -----------------------------------------------------------------------------
clk_rtc_write:
    push ax
    cmp byte [clk_rtc], 0
    je .out
    mov al, [clk_tier]
    cmp al, CLK_T_AT
    jne .n1
    call clk_at_write
    jmp short .out
.n1:
    cmp al, CLK_T_RP
    jne .n2
    call clk_rp_write
    jmp short .out
.n2:
    cmp al, CLK_T_NS
    jne .n3
    call clk_ns_write
    jmp short .out
.n3:
    cmp al, CLK_T_BIOS
    jne .out
    call clk_bios_write
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; clk_bios_write - rung 5's write: int 1Ah AH=03h + AH=05h
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
    call clk_tobcd              ; DL = 0 (no daylight saving, SPEC.md 37)
    mov ch, al
    mov al, [clk_min]
    call clk_tobcd
    mov cl, al
    mov al, [clk_sec]
    call clk_tobcd
    mov dh, al
    mov dl, 0
    mov ah, 0x03
    int 0x1a

    mov ax, [clk_year]          ; AH=05h: CH/CL = century/year BCD,
    mov cl, 100                 ; DH/DL = month/day BCD
    div cl                      ; AL = century, AH = year within it
    mov bl, ah                  ; clk_tobcd clobbers AH - park it
    call clk_tobcd
    mov bh, al
    mov al, bl
    call clk_tobcd
    mov ch, bh
    mov cl, al
    mov al, [clk_mon]
    call clk_tobcd
    mov dh, al
    mov al, [clk_day]
    call clk_tobcd
    mov dl, al
    mov ah, 0x05
    int 0x1a

.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; clk_tick - advance the clock from the PIT tick counter
; in:  nothing (UI task only)
; out: AL = change mask (SPEC.md 37): bit0 = a second was added,
;      bit1 = the MENU BAR'S TEXT changed and may be redrawn
; clobbers: AX only
;
; The SPEC.md 14 accumulator idiom verbatim, which is what makes 18.2 Hz add
; up to real seconds: delta ticks (wrap-safe subtraction) times ten, kept as
; a 32-bit value because one long gfx_lock starvation - a held-open menu -
; can owe more than 65535 tenths, then DIV 182 for whole seconds owed and
; the new remainder. Quotient <= 3602, so the DIV cannot fault.
;
; The two bits are separate because that IS the seconds setting: with the
; seconds hidden the bar's text is unchanged 59 seconds out of 60, and
; ui_task must not take the gfx lock for them - the cursor blink that comes
; with the lock is exactly the flicker the setting removes (SPEC.md 13).
; -----------------------------------------------------------------------------
clk_tick:
    push bx
    push cx
    push dx
    xor bl, bl                  ; BL = the change mask being built
    mov ax, [ticks]
    mov dx, ax
    sub ax, [clk_last]          ; delta, safe across the 65536 wrap
    mov [clk_last], dx
    mov dx, 10
    mul dx                      ; DX:AX = delta * 10 (tenths of a tick)
    add ax, [clk_acc]
    adc dx, 0
    mov cx, 182
    div cx                      ; AX = whole seconds owed, DX = leftover
    mov [clk_acc], dx
    mov cx, ax
    jcxz .pend
    or bl, 1                    ; a second passed
    mov bh, [clk_min]           ; the minute to compare against afterwards
    cmp cx, 60
    jb .adv
    or bl, 2                    ; a whole minute or more owed in one go: the
                                ; minute VALUE may land where it started
                                ; (an hour of held-open menu), so decide now
.adv:
    call clk_inc_sec
    loop .adv

    test bl, 2
    jnz .pend                   ; already decided
    cmp bh, [clk_min]
    jne .barchg                 ; the minute (or something above it) rolled
    cmp byte [clk_secs], 0
    je .pend                    ; seconds hidden: the bar reads the same
.barchg:
    or bl, 2
.pend:
    cmp byte [clk_barq], 0      ; a pending request - an edit or a settings
    je .out                     ; toggle asking for the bar (SPEC.md 37)
    mov byte [clk_barq], 0
    or bl, 2
.out:
    mov al, bl
    xor ah, ah
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; clk_inc_sec - add one second, carrying through the whole date
; in:  nothing
; out: nothing (all registers preserved)
; One whole carry chain per critical section (SPEC.md 37): a reader may only
; ever see the clock before or after a second, never halfway through one.
; Guarding each second separately rather than clk_tick's whole catch-up loop
; keeps the window short even when an hour of held-open menu is owed.
; -----------------------------------------------------------------------------
clk_inc_sec:
    push ax
    pushf
    cli
    inc byte [clk_sec]
    cmp byte [clk_sec], 60
    jb .out
    mov byte [clk_sec], 0
    inc byte [clk_min]
    cmp byte [clk_min], 60
    jb .out
    mov byte [clk_min], 0
    inc byte [clk_hour]
    cmp byte [clk_hour], 24
    jb .out
    mov byte [clk_hour], 0
    call clk_inc_day
.out:
    popf
    pop ax
    ret

; -----------------------------------------------------------------------------
; clk_inc_day - add one day, carrying through month and year
; in:  nothing
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
clk_inc_day:
    push ax
    inc byte [clk_day]
    mov al, [clk_mon]
    call clk_mlen               ; AL = this month's length
    cmp [clk_day], al
    jbe .out
    mov byte [clk_day], 1
    inc byte [clk_mon]
    cmp byte [clk_mon], 12
    jbe .out
    mov byte [clk_mon], 1
    inc word [clk_year]
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; clk_mlen - days in a month of the CURRENT year
; in:  AL = month, 1..12
; out: AL = 28..31
; clobbers: nothing else (flags)
; The leap rule is `year AND 3`, which is exact over CLK_YMIN..CLK_YMAX: the
; only century year in the range is 2000, and it is a leap year.
; -----------------------------------------------------------------------------
clk_mlen:
    push bx
    mov bl, al
    mov bh, 0
    mov al, [clk_mdays+bx-1]
    cmp bl, 2
    jne .out
    test byte [clk_year], 3     ; low byte carries bits 0..1
    jnz .out
    mov al, 29
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; clk_snap - take a consistent copy of the clock for the formatters
; in:  nothing
; out: nothing (clk_sn_* updated)
; clobbers: nothing (flags only)
;
; The reader half of SPEC.md 37's ownership rule. Everything a formatter
; reads comes out of this copy, taken in one pushf/cli window, so a reader
; pre-empted mid-format can never mix a pre-carry field with a post-carry
; one. The pairs are copied as words because the fields are laid out
; adjacent for exactly that reason.
; -----------------------------------------------------------------------------
clk_snap:
    push ax
    pushf
    cli
    mov ax, [clk_sec]           ; sec + min
    mov [clk_sn_sec], ax
    mov ax, [clk_hour]          ; hour + day
    mov [clk_sn_hour], ax
    mov al, [clk_mon]
    mov [clk_sn_mon], al
    mov ax, [clk_year]
    mov [clk_sn_year], ax
    popf
    pop ax
    ret

; -----------------------------------------------------------------------------
; clk_fmt - format the whole clock for the menu bar (SPEC.md 12.1)
; in:  nothing
; out: SI = clk_str, 18..24 glyphs + NUL, in the live display form:
;      'Mmm DD YYYY  HH:MM', plus ':SS' if [clk_secs], and in 12-hour mode
;      the hour 1..12 with NO leading zero and a trailing ' AM'/' PM'
; clobbers: nothing else
; Snapshots first, so the whole line is one instant. The width varies, which
; is why menu_draw_clock right-aligns it in a fixed cell (SPEC.md 12.1).
; -----------------------------------------------------------------------------
clk_fmt:
    push ax
    push bx
    push cx
    push dx
    call clk_snap
    mov bx, clk_str
    mov al, [clk_sn_mon]
    call clk_put_mon
    mov byte [bx], ' '
    inc bx
    mov al, [clk_sn_day]
    call clk_put2
    mov byte [bx], ' '
    inc bx
    mov ax, [clk_sn_year]
    call clk_put4
    mov byte [bx], ' '
    inc bx
    mov byte [bx], ' '
    inc bx

    mov al, [clk_sn_hour]       ; --- the hour, in the live form ---
    cmp byte [clk_h12], 0
    je .h24
    call clk_h12h               ; AL = 1..12
    cmp al, 10
    jae .h24                    ; two digits: the padded path writes it
    add al, '0'                 ; one digit, and NO leading zero: prose, not
    mov [bx], al                ; a fixed-width field (SPEC.md 37)
    inc bx
    jmp .min
.h24:
    call clk_put2
.min:
    mov byte [bx], ':'
    inc bx
    mov al, [clk_sn_min]
    call clk_put2

    cmp byte [clk_secs], 0      ; --- seconds, only if the setting says so ---
    je .mer
    mov byte [bx], ':'
    inc bx
    mov al, [clk_sn_sec]
    call clk_put2

.mer:                           ; --- ' AM' / ' PM' in 12-hour mode ---
    cmp byte [clk_h12], 0
    je .end
    mov byte [bx], ' '
    inc bx
    mov al, [clk_sn_hour]
    call clk_ampm               ; AL = 'A' or 'P'
    mov [bx], al
    inc bx
    mov byte [bx], 'M'
    inc bx
.end:
    mov byte [bx], 0
    mov si, clk_str
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; clk_fld_str - format ONE field for the Date/Time page (SPEC.md 31.5)
; in:  AL = field: 0 month, 1 day, 2 year, 3 hour, 4 minute, 5 second,
;      6 meridiem (12-hour mode only)
; out: SI = clk_fbuf, the field alone, always its full width (zero-padded -
;      a field is a fixed-width editable cell whose highlight box must not
;      change size under it, so unlike clk_fmt the 12-hour hour keeps its
;      leading zero: '12', '01'..'11')
; clobbers: nothing else
; Reads the LAST SNAPSHOT and does not take one: a caller drawing several
; fields (cp_time_rows) calls clk_snap once, so its whole row is one instant.
; -----------------------------------------------------------------------------
clk_fld_str:
    push ax
    push bx
    mov bx, clk_fbuf
    or al, al
    jz .mon
    cmp al, 1
    je .day
    cmp al, 2
    je .year
    cmp al, 3
    je .hour
    cmp al, 4
    je .min
    cmp al, 5
    je .sec

    mov al, [clk_sn_hour]       ; 6: the meridiem
    call clk_ampm               ; AL = 'A' or 'P'
    mov [bx], al
    inc bx
    mov byte [bx], 'M'
    inc bx
    jmp .done
.sec:
    mov al, [clk_sn_sec]
    jmp .two
.min:
    mov al, [clk_sn_min]
    jmp .two
.hour:
    mov al, [clk_sn_hour]
    cmp byte [clk_h12], 0
    je .two
    call clk_h12h               ; AL = 1..12, still zero-padded below
    jmp .two
.day:
    mov al, [clk_sn_day]
.two:
    call clk_put2
    jmp .done
.mon:
    mov al, [clk_sn_mon]
    call clk_put_mon
    jmp .done
.year:
    mov ax, [clk_sn_year]
    call clk_put4
.done:
    mov byte [bx], 0
    mov si, clk_fbuf
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; clk_fld_adj - step one field, with wrap (SPEC.md 31.5)
; in:  AL = field (as clk_fld_str), BL = +1 or -1
; out: nothing (all registers preserved)
;
; A month or year step re-clamps the day to the new month's length, so
; 31 March minus one month is 28 February and never 31 February. The hour
; is always stored and stepped 0..23 - 12-hour mode is a rendering of it,
; not a second representation - so field 6 (the meridiem) is the only thing
; that moves by 12, and either sign of the step flips it. Any edit restarts
; the sub-second accumulator from now (setting the seconds to a value and
; having it advance a tenth of a second later would be wrong), posts
; [clk_dirty] so ui_task writes the hardware RTC outside the lock, and posts
; [clk_barq] so the menu bar follows even when it is not showing seconds.
; -----------------------------------------------------------------------------
clk_fld_adj:
    push ax
    push bx
    push cx
    push dx
    pushf                       ; one edit = one critical section, so a
    cli                         ; reader never catches the day mid-clamp
    mov ah, bl                  ; AH = the step, +1 or -1
    or al, al
    jz .mon
    cmp al, 1
    je .day
    cmp al, 2
    je .year
    cmp al, 3
    je .hour
    cmp al, 4
    je .min
    cmp al, 5
    je .sec

    mov al, [clk_hour]          ; 6: the meridiem - +-12 hours, either sign
    cmp al, 12
    jb .pm
    sub al, 12                  ; PM -> AM
    jmp .merset
.pm:
    add al, 12                  ; AM -> PM
.merset:
    mov [clk_hour], al
    jmp .done
.sec:
    mov cx, 0x3B00              ; second: CH = 59 max, CL = 0 min
    mov al, [clk_sec]
    call clk_step
    mov [clk_sec], al
    jmp .done
.min:
    mov cx, 0x3B00
    mov al, [clk_min]
    call clk_step
    mov [clk_min], al
    jmp .done
.hour:
    mov cx, 0x1700              ; CH = 23, CL = 0
    mov al, [clk_hour]
    call clk_step
    mov [clk_hour], al
    jmp .done
.day:
    mov al, [clk_mon]
    call clk_mlen
    mov ch, al                  ; max = this month's length
    mov cl, 1
    mov al, [clk_day]
    call clk_step
    mov [clk_day], al
    jmp .done
.mon:
    mov cx, 0x0C01              ; CH = 12, CL = 1
    mov al, [clk_mon]
    call clk_step
    mov [clk_mon], al
    jmp .fixday
.year:
    mov bx, [clk_year]
    cmp ah, 0
    jl .ydn
    inc bx
    cmp bx, CLK_YMAX
    jbe .yset
    mov bx, CLK_YMIN
    jmp .yset
.ydn:
    dec bx
    cmp bx, CLK_YMIN
    jae .yset
    mov bx, CLK_YMAX
.yset:
    mov [clk_year], bx

.fixday:                        ; the month (or the leap year) may have got
    mov al, [clk_mon]           ; shorter under the current day
    call clk_mlen
    cmp [clk_day], al
    jbe .done
    mov [clk_day], al

.done:
    mov ax, [ticks]             ; the new second starts now
    mov [clk_last], ax
    mov word [clk_acc], 0
    mov byte [clk_dirty], 1     ; ui_task step 3 writes the RTC (SPEC.md 13)
    mov byte [clk_barq], 1      ; ...and step 4 refreshes the bar, which may
                                ; otherwise be a minute from noticing
    popf
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; clk_step - add a signed step to a byte, wrapping into a closed range
; in:  AL = value, AH = step (+1 / -1), CL = range min, CH = range max
; out: AL = the wrapped value
; clobbers: nothing else (flags)
; Values are small unsigned bytes, so stepping down from 0 lands on 0xFF -
; above the max - and the step's sign is what tells the two overflows apart.
; -----------------------------------------------------------------------------
clk_step:
    add al, ah
    cmp al, ch
    jbe .lo
    cmp ah, 0
    jl .under                   ; 0xFF from a -1: wrap to the top
    mov al, cl                  ; past the max: wrap to the bottom
    ret
.under:
    mov al, ch
    ret
.lo:
    cmp al, cl
    jae .out
    mov al, ch                  ; below the min: wrap to the top
.out:
    ret

; -----------------------------------------------------------------------------
; clk_h12h - a 0..23 hour as it reads on a 12-hour clock
; in:  AL = 0..23
; out: AL = 1..12 (midnight and noon both read 12)
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
clk_h12h:
    cmp al, 12
    jb .am
    sub al, 12
.am:
    or al, al
    jnz .out
    mov al, 12                  ; 00:xx is 12 AM, 12:xx is 12 PM
.out:
    ret

; -----------------------------------------------------------------------------
; clk_ampm - which half of the day an hour is in
; in:  AL = 0..23
; out: AL = 'A' or 'P' (the caller appends the 'M')
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
clk_ampm:
    cmp al, 12
    mov al, 'A'
    jb .out
    mov al, 'P'
.out:
    ret

; -----------------------------------------------------------------------------
; clk_put_mon - write a 3-char month name at DS:BX, advancing BX
; in:  AL = month 1..12, BX = destination
; out: BX advanced by 3
; clobbers: nothing else
; -----------------------------------------------------------------------------
clk_put_mon:
    push ax
    push cx
    push si
    dec al
    mov ah, 0
    mov si, ax
    add si, ax
    add si, ax                  ; SI = (month-1) * 3
    add si, clk_mnames
    mov cx, 3
.c:
    mov al, [si]
    mov [bx], al
    inc si
    inc bx
    loop .c
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; clk_put2 - write a 2-digit zero-padded number at DS:BX, advancing BX
; in:  AL = value 0..99, BX = destination
; out: BX advanced by 2
; clobbers: nothing else
; -----------------------------------------------------------------------------
clk_put2:
    push ax
    aam                         ; AH = tens, AL = ones
    add ax, 0x3030              ; both to ASCII, leading zero for free
    mov [bx], ah
    inc bx
    mov [bx], al
    inc bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; clk_put4 - write a 4-digit zero-padded number at DS:BX, advancing BX
; in:  AX = value 0..9999, BX = destination
; out: BX advanced by 4
; clobbers: nothing else
; -----------------------------------------------------------------------------
clk_put4:
    push ax
    push cx
    mov cl, 100
    div cl                      ; AL = hundreds, AH = remainder
    push ax
    call clk_put2               ; the century pair
    pop ax
    mov al, ah
    call clk_put2               ; the pair within it
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; clk_bcd - packed BCD byte to binary
; in:  AL = BCD
; out: AL = value, CF = 1 if either nibble is not a decimal digit
; clobbers: nothing else
; -----------------------------------------------------------------------------
clk_bcd:
    push cx
    mov ch, al
    and al, 0x0F
    cmp al, 10
    jae .bad
    mov cl, 4
    shr ch, cl
    cmp ch, 10
    jae .bad
    mov cl, ch                  ; AL += CH * 10
    add ch, ch
    add ch, ch
    add ch, ch                  ; CH * 8
    add ch, cl
    add ch, cl                  ; + CH * 2
    add al, ch
    clc
    jmp .out
.bad:
    stc
.out:
    pop cx
    ret

; -----------------------------------------------------------------------------
; clk_tobcd - binary byte to packed BCD
; in:  AL = value 0..99
; out: AL = BCD
; clobbers: AH
; -----------------------------------------------------------------------------
clk_tobcd:
    push cx
    aam                         ; AH = tens, AL = ones
    mov cl, 4
    shl ah, cl
    or al, ah
    pop cx
    ret

; -----------------------------------------------------------------------------
section .bss

clk_sec:    resb 1              ; 0..59      \ the live clock; only the UI
clk_min:    resb 1              ; 0..59       | task writes any of it
clk_hour:   resb 1              ; 0..23       |
clk_day:    resb 1              ; 1..31       |
clk_mon:    resb 1              ; 1..12       |
clk_rtc:    resb 1              ; 1 = a usable hardware RTC was found
clk_dirty:  resb 1              ; 1 = the clock was set; write the RTC
clk_pad:    resb 1              ; keeps the words below even-aligned
clk_year:   resw 1              ; CLK_YMIN..CLK_YMAX

; --- display settings (SPEC.md 37): runtime state, boot defaults in
; --- clk_init, nowhere to persist them (the kernel never writes the data disk)
clk_h12:    resb 1              ; 0 = 24-hour, 1 = 12-hour + AM/PM
clk_secs:   resb 1              ; 0 = the menu bar hides seconds, 1 = shows
clk_barq:   resb 1              ; sticky: the bar's text is stale (an edit or
clk_bpad:   resb 1              ; a settings toggle), folded into the next
                                ; clk_tick's change mask and cleared there
clk_last:   resw 1              ; last [ticks] sample
clk_acc:    resw 1              ; tenth-of-tick remainder

clk_sn_sec:  resb 1             ; clk_snap's copy - the ONLY thing the
clk_sn_min:  resb 1             ; formatters read. Same order and adjacency
clk_sn_hour: resb 1             ; as the live fields above: the snapshot
clk_sn_day:  resb 1             ; copies sec+min and hour+day as words
clk_sn_mon:  resb 1
clk_sn_pad:  resb 1
clk_sn_year: resw 1

clk_tb_h:   resb 1              ; every rung's read lands here first, and
clk_tb_mi:  resb 1              ; clk_commit is the only thing that adopts it
clk_tb_s:   resb 1
clk_tb_mo:  resb 1
clk_tb_d:   resb 1
clk_tb_p:   resb 1
clk_tb_y:   resw 1

; --- the RTC ladder (SPEC.md 37.90) ------------------------------------------
clk_tier:   resb 1              ; CLK_T_*: which rung answered. [clk_rtc] is
                                ; still the yes/no every caller tests
clk_ref:    resb 1              ; 1 = int 1Ah gave a usable answer, so the
                                ; RP5C01 gate below has something to check
clk_cent:   resb 1              ; the AT chip's century byte at CMOS 32h if
                                ; this BIOS keeps one, 0 if it does not -
                                ; clk_at_write must not invent one
clk_rb:     resb 1              ; the AT chip's Register B, as we found it:
                                ; DM and 24/12 are read and obeyed, never set
clk_rbin:   resb 1              ; 0 = decode the raw bytes as BCD, else binary
clk_rr:     resb 7              ; the AT read burst's raw bytes: sec, min,
                                ; hour, day, month, year, century
clk_rf_h:   resb 1              ; the int 1Ah reference the RP5C01 probe has
clk_rf_d:   resb 1              ; to agree with, parked because every rung
clk_rf_mo:  resb 1              ; writes its own answer into clk_tb_*
clk_rf_p:   resb 1
clk_rf_y:   resw 1
clk_nsrol:  resb 1              ; 1 = the MM58167's month went backwards, so
                                ; the year we just corrected must go BACK on
                                ; the card or the correction repeats forever
clk_rpyy:   resb 1              ; the RP5C01's two year digits, raw: the chip
clk_rpep:   resb 1              ; has no century, so 0 = they are year mod 100
                                ; and 1 = they are year - CLK_YMIN. Which one
                                ; is a software convention, and clk_rp_probe
                                ; settles it by asking which one agrees
clk_rp24:   resb 1              ; ...and bit 0 = the chip is running 24-hour,
clk_rp_pd:  resb 1              ; read at write time, never written

clk_str:    resb 26             ; the bar's line, longest form 24 glyphs +
                                ; NUL (drawn under the gfx lock, UI task only)
clk_fbuf:   resb 6              ; one field's text, same ownership

section .text
