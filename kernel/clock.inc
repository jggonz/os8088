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
; posts [clk_dirty] instead.
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
    call clk_rtc_read           ; CF = 1: no usable RTC, keep the fallback
    jc .out
    mov byte [clk_rtc], 1
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; clk_rtc_read - read the hardware RTC into the clock fields
; in:  nothing
; out: CF = 0 and the fields updated, CF = 1 and nothing written
; clobbers: nothing else (flags)
;
; Deliberately paranoid (SPEC.md 37): the machines this OS targets are
; exactly the ones that may have no CMOS clock at all, and a BIOS that does
; not implement an int 1Ah function may simply iret, leaving CF and the
; registers as it found them. So CX/DX are poisoned and CF is set before
; each call, every byte must be valid packed BCD and in range, and the
; values are committed out of scratch only once ALL of that has passed -
; a probe that fails halfway can never leave a half-RTC clock.
; -----------------------------------------------------------------------------
clk_rtc_read:
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
    mov al, ch                  ; century
    call clk_bcd
    jc .none
    cmp al, 19
    jb .none
    cmp al, 20
    ja .none
    mov bl, al
    mov al, cl                  ; year within the century
    call clk_bcd
    jc .none
    mov bh, al
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

    ; --- commit, as one set --------------------------------------------------
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
    mov ax, [clk_tb_y]
    mov [clk_year], ax
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
; in:  nothing; NO gfx lock held (this calls BIOS - SPEC.md 13/31.1)
; out: nothing (all registers preserved)
; A machine without an RTC returns immediately: the clock is the PIT's.
; -----------------------------------------------------------------------------
clk_rtc_write:
    push ax
    push bx
    push cx
    push dx
    cmp byte [clk_rtc], 0
    je .out

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
; --- clk_init, nowhere to persist them (os88fs is read-only)
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

clk_tb_h:   resb 1              ; clk_rtc_read scratch: the probe's values,
clk_tb_mi:  resb 1              ; held here until every gate has passed
clk_tb_s:   resb 1
clk_tb_mo:  resb 1
clk_tb_d:   resb 1
clk_tb_p:   resb 1
clk_tb_y:   resw 1

clk_str:    resb 26             ; the bar's line, longest form 24 glyphs +
                                ; NUL (drawn under the gfx lock, UI task only)
clk_fbuf:   resb 6              ; one field's text, same ownership

section .text
