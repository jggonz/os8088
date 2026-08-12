; =============================================================================
; os8088 - tests/benchlib.inc
;
; The measuring and reporting half of the benchmark packages in tests/. It is
; the only shared source under tests/ - gfxbench and sysbench differ entirely
; in WHAT they measure and not at all in how, and a second copy of the timing
; loop is a second chance to get the timing loop wrong (PERFORMANCE.md Part 6
; rule 7: two harnesses that disagree is how three of the four sizing bugs in
; this project were found; two harnesses that were copy-pasted cannot
; disagree).
;
; Include it AFTER your own code and BEFORE OS88_IMAGE_END, then invoke the
; BL_BSS macro after it with the address your own bss ends at:
;
;   %include "os88api.inc"
;       OS88_HEADER 'MYBENCH', my_entry
;       ... your code ...
;   %include "benchlib.inc"
;       ... your data ...
;   MY_BSS_OWN equ 40
;       OS88_BSS MY_BSS_OWN + BL_BSS_SIZE
;       OS88_IMAGE_END
;   my_var equ os88_image_end + 0
;       BL_BSS os88_image_end + MY_BSS_OWN
;
; --- how a row is timed ------------------------------------------------------
;
; Counter 0 of the 8253, latched and read directly. It runs at 1.193182 MHz for
; the system tick whether we look at it or not, so latching costs nothing and
; disturbs nobody (latch is a read command, not a reprogram); one count is
; 838 ns and it reloads every 55 ms. OSAPI_GET_TICKS cannot be used for this -
; a tick is 55 ms and most of what is measured here is a few hundred
; microseconds, so a tick count answers 0 or 1.
;
; THE cli WINDOW IS ONE ITERATION, NOT ONE ROW, and that is a deliberate
; departure from tests/fontbench. Each iteration is
;
;       pushf / cli / read PIT / <body> / read PIT / fold / popf
;
; so interrupts are serviced BETWEEN iterations and never inside a measured
; span. Three things fall out. The tick keeps time, so a long row no longer
; drifts the clock. The mouse UART is drained often enough not to overrun.
; And - the real reason - the tick ISR, the mouse ISR and any sound refill are
; excluded from every measurement instead of landing on whichever row was
; unlucky, which is what made fontbench's rows move by more than the effect it
; was measuring. The cost is one extra PIT read per iteration, which the
; baseline row below pays identically and which therefore cancels.
;
; Elapsed is the MODULAR subtraction start - end (the counter counts DOWN), and
; it must be: the counter reloads every 55 ms whatever we are doing, so
; `end > start` means the iteration straddled the reload, not that it overran.
; What DOES overrun is a single iteration longer than 55 ms, and there are TWO
; guards because the first one is not enough.
;
; [bl_max] is the largest single-iteration delta seen, and a row whose worst
; iteration passed BL_SUSPECT counts is flagged '!' - it is close enough to the
; wrap to be untrustworthy. That catches an iteration APPROACHING a lap.
;
; It cannot catch one that LAPS, and that is the dangerous case: a body taking
; 561 ms reports 561 mod 54.92 = 12 ms, a small plausible number with a small
; plausible max delta and no flag at all. That happened - GFX_BLIT4's striped
; row on a real 5150 lapped ten times and was published at 12 ms, making a
; primitive that does 20x the work look 2.4x FASTER than its own best case
; (PERFORMANCE.md Part 9).
;
; So the row is ALSO bracketed by the TICK counter, read outside the cli
; windows. THE TEST IS NOT "how much time do the ticks say passed" - and that
; mistake cost a debugging round, so it is worth stating. While IF = 0 the 8259
; latches ONE pending IRQ0 however many the PIT raises, so a body that laps ten
; times still yields exactly one tick when interrupts come back on between
; iterations: the tick count under-reports a lapping row by the lap factor and
; cannot measure it.
;
; What it CAN do is detect one. `ticks >= N` says every single iteration
; crossed a tick boundary, i.e. each ran for a whole 54.92 ms - which for
; anything the PIT total claims is shorter than that is a contradiction, and
; the second half of the test (`ticks > the PIT total's high word`) is that
; contradiction stated. When it fires the row is simply RE-RUN with method T,
; where interrupts stay on and the tick counter is not throttled, and flagged
; 'w'. That costs one repeat of a row that was already slow, and it is the only
; way to get a number rather than a warning.
;
; --- the two methods ---------------------------------------------------------
;
;   P (PIT)    the above. Exact, 838 ns resolution, and NET of the loop
;              overhead - the baseline row measures an empty body at
;              BL_BASE_N iterations and every P row has its share subtracted.
;              Only legal while one iteration stays under a 55 ms PIT wrap.
;   T (ticks)  OSAPI_GET_TICKS around the whole row, IF left alone. One tick
;              is EXACTLY 65536 PIT counts, so the two methods report in the
;              same unit and the same columns. Coarse - size the row for two
;              seconds or more - and it includes interrupt and scheduler time,
;              which is the honest answer for something that takes seconds.
;              No overhead subtraction: at these magnitudes it is noise.
;
; A row that reports in method T carries 't' in the flag column, and one whose
; worst P iteration was suspicious carries '!'.
;
; --- why the arithmetic is 48-bit --------------------------------------------
;
; PERFORMANCE.md Part 6 rule 3, twice over. `counts * 838` overflows 32 bits at
; 4.3 seconds and a floppy read is longer than that; `counts * 8381` overflows
; at 430 ms. Both are numbers a 4.77 MHz 8088 reaches and a container never
; does, which is exactly the shape of bug this file exists to avoid, so the
; multiply is 48-bit and the divide walks three words. bl_get32 saturates
; rather than wrapping, so an impossible row prints 4294967295 and is obviously
; wrong instead of plausibly wrong.
; =============================================================================

BL_MAXLINE  equ 78              ; characters in a report line. 78 * 8 = 624 px,
                                ; which fits the content of a 632-wide window
                                ; on the narrowest adapter (CGA, 640 px)
BL_MAXROWS  equ 380             ; report lines. Sized for the longest suite,
                                ; WITH HEADROOM - it was 190 and sysbench's
                                ; longest report is 190, so folding SPEC.md
                                ; 18.94's disk block and 37.92's clock block
                                ; into every field kernel truncated it on the
                                ; exact run it was built for. The banner says
                                ; so, which is the only reason that was caught
                                ; and not carried off the machine in a photo -
                                ; and it caught the SECOND one too, at 240,
                                ; when 18.95.2's and 18.95.4's blocks landed.
                                ; A report that is one round from its ceiling
                                ; is a report that truncates on somebody
                                ; else's machine, and it happened a THIRD
                                ; time at 300 when SPEC.md 18.4's write block
                                ; landed. 380 for 305, and 337 after Set 35's
                                ; seek and spin-up blocks.
BL_ARENA    equ 16000           ; bytes of packed NUL-terminated line text.
                                ; **THIS CEILING IS INVISIBLE UNTIL IT IS
                                ; HIT, AND IT CANNOT SIMPLY BE RAISED**, which
                                ; together are why it needs a paragraph. The
                                ; row ceiling above was raised three times
                                ; after the fact; this one never has been,
                                ; because `bl_full` says a report TRUNCATED
                                ; and nothing said how close a report that did
                                ; not had come. Set 35's two blocks landed it
                                ; at 14,806 of 16,000 - 7% spare.
                                ;
                                ; And raising it costs TWICE what it looks
                                ; like: `bl_out` below is BL_ARENA-sized too,
                                ; so every byte here is two, and sysbench's
                                ; image plus bss is already within ~5KB of the
                                ; 64KB SEGMENT a package gets (SPEC.md 20.1).
                                ; +3,456 here overflows it, and the failure is
                                ; a wall of `word data exceeds bounds` at
                                ; unrelated lines rather than anything naming
                                ; this constant. The next round wants a row
                                ; DROPPED, or the suite split - not a bigger
                                ; number here. sysbench's trailer prints both
                                ; figures so that decision can be taken before
                                ; a field report truncates rather than after.
BL_OUTSZ    equ BL_ARENA + BL_MAXROWS * 2 + 64   ; ...plus a CR LF each
BL_BASE_N   equ 400             ; iterations of the empty body, for the
                                ; overhead every method-P row is net of
BL_SUSPECT  equ 40000           ; a single iteration past this many counts
                                ; (33 ms) is too close to the 55 ms wrap to
                                ; trust: flag it rather than believe it

; Report column layout, in ONE place so a format change is one edit and the
; heading line below cannot drift from the rows.
BL_C_N      equ 24              ; iterations, width 5
BL_C_CNT    equ 30              ; total PIT counts, width 9
BL_C_US     equ 39              ; us/op: 9 digits, '.', 2 digits. IT WAS 7 AT
                                ; COLUMN 41, AND SEVEN DIGITS WRAP AT TEN
                                ; SECONDS - silently, mod 10,000,000, so a
                                ; 41.96-second row printed 1.96 and read as
                                ; the fastest thing on the page. Found when
                                ; SPEC.md 18.4's write rows came back from the
                                ; 5150 with an append that beat the create it
                                ; was three times slower than. Nine digits is
                                ; 999 seconds; the counts column ends at 38, so
                                ; this starts at 39 and still clears the unit
BL_C_UNIT   equ 52              ; 'us'
BL_C_FLAG   equ 55              ; 't' method T, '!' suspicious iteration

; -----------------------------------------------------------------------------
; bl_pit - latch and read counter 0 of the 8253
; out:      AX = the counter (counts DOWN at 1.193182 MHz, wraps every 55 ms)
; clobbers: AX, flags
;
; Latch (control word 00h) freezes a copy for reading without touching the
; counter's mode or reload value, so the system tick is undisturbed. Every
; caller has IF = 0 already: a latch interrupted between the two byte reads
; returns a torn count.
; -----------------------------------------------------------------------------
bl_pit:
    push dx
    mov al, 0
    out 0x43, al                ; latch counter 0
    jmp short $+2               ; I/O settling, the period-hardware idiom
    in al, 0x40
    mov dl, al
    jmp short $+2
    in al, 0x40
    mov ah, al
    mov al, dl
    pop dx
    ret

; -----------------------------------------------------------------------------
; bl_time - method P: [bl_n] iterations of [bl_body], PIT-timed
; in:       [bl_n], [bl_body]
; out:      DX:AX = total counts; [bl_max] = largest single-iteration delta
; clobbers: AX, DX, flags (the body may clobber anything else)
; -----------------------------------------------------------------------------
bl_time:
    push bx
    push cx
    push si
    push di
    call OSAPI_GET_TICKS        ; the lap detector - see bl_run. Read OUTSIDE
    mov [bl_t0], ax             ; every cli window, and the loop below
                                ; re-enables interrupts between iterations, so
                                ; the tick really does advance across the row
    mov word [bl_acc], 0
    mov word [bl_acc+2], 0
    mov word [bl_max], 0
    mov cx, [bl_n]
    or cx, cx
    jz .out
.loop:
    push cx
    pushf
    cli
    call bl_pit
    mov [bl_prev], ax
    call word [bl_body]
    call bl_pit
    mov bx, [bl_prev]
    sub bx, ax                  ; DOWN-counter: start - end, modular
    add [bl_acc], bx
    adc word [bl_acc+2], 0
    cmp bx, [bl_max]
    jbe .nomax
    mov [bl_max], bx
.nomax:
    popf                        ; ...and the tick, the mouse and any sound
    pop cx                      ; refill are serviced HERE, outside the span
    loop .loop
.out:
    call OSAPI_GET_TICKS
    sub ax, [bl_t0]             ; modular; the tick counter wraps at midnight
    mov [bl_tick], ax
    mov ax, [bl_acc]
    mov dx, [bl_acc+2]
    pop di
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; bl_time_t - method T: [bl_n] iterations of [bl_body], tick-timed
; in:       [bl_n], [bl_body]
; out:      DX:AX = counts (ticks * 65536 - one tick IS 65536 PIT counts, so
;           this is the same unit method P reports in)
; clobbers: AX, DX, flags
; -----------------------------------------------------------------------------
bl_time_t:
    push bx
    push cx
    push si                     ; SI and DI are the BODY's to clobber, and
    push di                     ; bl_run still needs SI - it is the row's
                                ; label. Without these two pushes the one
                                ; tick-timed row per suite came out labelled
                                ; with whatever the body left in SI, which for
                                ; a body that repaints a window is a window
                                ; record read as a string
    call OSAPI_GET_TICKS
    mov [bl_prev], ax
    mov cx, [bl_n]
    or cx, cx
    jz .out
.loop:
    push cx
    call word [bl_body]
    pop cx
    loop .loop
.out:
    call OSAPI_GET_TICKS
    sub ax, [bl_prev]           ; modular: the tick counter wraps at midnight
    mov dx, ax                  ; counts = ticks << 16
    xor ax, ax
    mov word [bl_max], 0
    pop di
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; bl_empty - the empty body. The baseline row measures this, and every
;            method-P row is reported net of its share.
; -----------------------------------------------------------------------------
bl_empty:
    ret

; -----------------------------------------------------------------------------
; bl_baseline - measure the loop overhead once, into [bl_ovhx]
; in:       nothing
; out:      [bl_ovhx] = counts per iteration, in 1/256ths; DX:AX = raw counts
; clobbers: AX, DX, flags
;
; Stored in 1/256ths because one iteration is a handful of counts and an
; integer would round a fifth of it away across a 2,000-iteration row.
; -----------------------------------------------------------------------------
bl_baseline:
    push cx
    mov word [bl_n], BL_BASE_N
    mov word [bl_body], bl_empty
    call bl_time
    mov [bl_bcnt], ax
    mov [bl_bcnt+2], dx
    mov cx, 256
    call bl_mul48               ; counts * 256...
    mov cx, BL_BASE_N
    call bl_div48               ; ...over the iterations
    call bl_get32
    mov [bl_ovhx], ax           ; the high word is 0 for any sane machine, and
    pop cx                      ; bl_get32 saturated it if it is not
    mov ax, [bl_bcnt]
    mov dx, [bl_bcnt+2]
    ret

; -----------------------------------------------------------------------------
; bl_run - measure [bl_body] and emit one report row
; in:       SI = label (NUL, <= 23 chars), [bl_n], [bl_body],
;           AL = 0 method P / 1 method T
; out:      [bl_last] = the net counts, [bl_lastus] = hundredths of a us per
;           iteration - both for the caller's own derived arithmetic
; clobbers: flags (every register is preserved)
;
; THE RESULT IS IN [bl_last], NOT IN DX:AX. Every register is preserved, which
; is what lets a block of rows keep its own state in registers across them -
; and it means `call bl_run` followed by `mov [my_total], ax` banks whatever
; the caller happened to have in AX. That reads as a derived figure of zero,
; or of a hundred, rather than as a crash.
; -----------------------------------------------------------------------------
bl_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call bl_mwatch                  ; where is the pointer BEFORE this row?
    mov [bl_meth], al
    or al, al
    jnz .tick
    call bl_time
    ; --- DID A SINGLE ITERATION LAP THE COUNTER? (see the header) The tick
    ; count across the whole row is an upper bound on its true length, so
    ; comparing it against the PIT total's HIGH WORD is the same comparison as
    ; "ticks x 65536 > counts + two laps" and costs three instructions. Two
    ; laps of slack absorbs the tick's own 55 ms quantisation and the
    ; interrupt time between iterations, neither of which is in the PIT total.
    mov byte [bl_lapped], 0
    mov bx, [bl_tick]
    cmp bx, [bl_n]              ; did EVERY iteration cross a tick boundary?
    jb .nolap                   ; no: nothing here ran for a whole 54.92 ms
    cmp bx, dx                  ; ...and are there more ticks than the PIT
    jbe .nolap                  ; total's own high word accounts for?
    mov byte [bl_lapped], 1     ; then some iteration lapped. Redo the row with
    call bl_time_t              ; interrupts ON, where the tick counter is not
    jmp short .fmt              ; throttled - see the header
.nolap:
    ; --- net of the loop overhead: ovhx * N / 256, floored at zero
    push ax
    push dx
    mov ax, [bl_ovhx]
    mov dx, [bl_n]
    mul dx                      ; DX:AX = ovhx * N
    mov cx, 256
    call bl_div32               ; DX:AX = the overhead in whole counts
    mov bx, ax
    mov cx, dx
    pop dx
    pop ax
    sub ax, bx
    sbb dx, cx
    jnc .keep
    xor ax, ax                  ; the body measured faster than an empty loop:
    xor dx, dx                  ; noise. Report zero rather than 4 billion
.keep:
    jmp short .fmt
.tick:
    call bl_time_t
.fmt:
    mov [bl_last], ax
    mov [bl_last+2], dx
    call bl_lclr
    xor di, di
    call bl_lput                ; SI = the label, at column 0
    mov ax, [bl_n]
    xor dx, dx
    mov di, BL_C_N
    mov cx, 5
    call bl_dec
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov di, BL_C_CNT
    mov cx, 9
    call bl_dec
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov cx, [bl_n]
    call bl_us100               ; DX:AX = hundredths of a us per iteration
    mov [bl_lastus], ax
    mov [bl_lastus+2], dx
    mov di, BL_C_US
    call bl_usfield
    mov si, bl_s_us
    mov di, BL_C_UNIT
    call bl_lput
    cmp byte [bl_lapped], 0
    je .mflag
    mov byte [bl_lscr + BL_C_FLAG], 'w'
    jmp short .done
.mflag:
    cmp byte [bl_meth], 0
    je .pflag
    mov byte [bl_lscr + BL_C_FLAG], 't'
    jmp short .done
.pflag:
    cmp word [bl_max], BL_SUSPECT
    jb .done
    mov byte [bl_lscr + BL_C_FLAG], '!'
.done:
    call bl_lcommit
    inc word [bl_mnrow]
    call bl_mwatch                  ; ...and where is it now?
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; bl_mwatch - sample the pointer, and count a row it moved across
; clobbers: flags (every register preserved)
;
; Called at the top and bottom of every bl_run, which is OUTSIDE every timed
; span and outside every cli window, so it perturbs nothing. Two rows exist
; because of one field sighting each, and both are questions about the
; OPERATOR rather than about the machine (PERFORMANCE.md Part 9 Set 11):
;
;   - GFX_UNLOCK+LOCK is the one row in either harness that cannot be timed
;     with interrupts off, and the only variable work in it is cur_lazyend's
;     cur_move - which runs when the pointer has moved since the cursor was
;     drawn. On the 5150 that row came in 9x dearer than on any other
;     machine, and nobody could say afterwards whether a hand had been on the
;     mouse. Now the report says.
;   - and a cursor seen visually corrupted over a title bar mid-run, once, on
;     a machine nobody is sure of. A run where the pointer never moved cannot
;     have been that, which is worth knowing before anyone re-derives it.
;
; Sampling per ROW rather than per iteration is deliberate: per iteration
; would put an API far call inside the thing being measured.
; -----------------------------------------------------------------------------
bl_mwatch:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call OSAPI_MOUSE                ; CX = x, DX = y (AL = buttons)
    cmp byte [bl_mhave], 0
    jne .seen
    mov byte [bl_mhave], 1
    mov [bl_mx0], cx
    mov [bl_my0], dx
    mov [bl_mxlo], cx
    mov [bl_mxhi], cx
    mov [bl_mylo], dx
    mov [bl_myhi], dx
    jmp short .bank
.seen:
    cmp cx, [bl_mxlo]               ; the bounding box first: it is what catches
    jae .xhi                        ; a jiggle that returns between samples,
    mov [bl_mxlo], cx               ; which the sample counter cannot see
.xhi:
    cmp cx, [bl_mxhi]
    jbe .ylo
    mov [bl_mxhi], cx
.ylo:
    cmp dx, [bl_mylo]
    jae .yhi
    mov [bl_mylo], dx
.yhi:
    cmp dx, [bl_myhi]
    jbe .cmp
    mov [bl_myhi], dx
.cmp:
    cmp cx, [bl_mx]
    jne .moved
    cmp dx, [bl_my]
    je .bank
.moved:
    inc word [bl_mrows]
.bank:
    mov [bl_mx], cx
    mov [bl_my], dx
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; bl_operator - the block that describes the RUN rather than the machine
;
; Emitted at the end of a report, because these are totals. Every field here
; exists because a field set could not be interpreted afterwards without it
; (PERFORMANCE.md Part 9 Set 11) - a benchmark that cannot say what the person
; running it was doing produces numbers nobody can attribute a year later.
;
; `pointer moved` is the load-bearing one. Two samples are taken per row,
; outside every timed span, and this counts the ones that differ from the
; previous. **0 means the mouse was untouched for the whole suite**, which is
; the condition GFX_UNLOCK+LOCK has to be read under, and the condition that
; rules a run out as the cause of a cursor seen corrupting mid-suite.
;
; The two SPAN rows are there because that counter alone is not enough, and
; the first version shipped without them: a nudge that comes back between two
; samples moves no sample at all. The bounding box over the whole suite is
; widened by any excursion however brief, so the honest reading is "moved = 0
; AND both spans = 0" for a genuinely untouched mouse. Both are cheap, and
; neither can be got from the other.
; -----------------------------------------------------------------------------
bl_operator:
    push ax
    push cx
    push dx
    push si
    call bl_blank
    mov si, bl_l_ophdr
    call bl_sline
    mov si, bl_l_opmv
    mov ax, [bl_mrows]
    call bl_opnum
    mov si, bl_l_opsm
    mov ax, [bl_mnrow]
    add ax, ax                      ; two samples a row, top and bottom
    call bl_opnum
    mov si, bl_l_opxs               ; ...and the box, which a jiggle widens
    mov ax, [bl_mxhi]               ; even when it lands back on its start
    sub ax, [bl_mxlo]
    call bl_opnum
    mov si, bl_l_opys
    mov ax, [bl_myhi]
    sub ax, [bl_mylo]
    call bl_opnum
    mov si, bl_l_opx0
    mov ax, [bl_mx0]
    call bl_opnum
    mov si, bl_l_opy0
    mov ax, [bl_my0]
    call bl_opnum
    mov si, bl_l_opx1
    mov ax, [bl_mx]
    call bl_opnum
    mov si, bl_l_opy1
    mov ax, [bl_my]
    call bl_opnum
    pop si
    pop dx
    pop cx
    pop ax
    ret

bl_opnum:
    push cx
    push dx
    xor dx, dx
    mov cx, 9
    call bl_kv
    pop dx
    pop cx
    ret

bl_l_ophdr: db '-- the run: what the person driving it was doing --', 0
bl_l_opmv:  db 'pointer moved (samples)', 0
bl_l_opsm:  db 'pointer samples taken', 0
bl_l_opxs:  db 'pointer x span', 0
bl_l_opys:  db 'pointer y span', 0
bl_l_opx0:  db 'pointer x at start', 0
bl_l_opy0:  db 'pointer y at start', 0
bl_l_opx1:  db 'pointer x at end', 0
bl_l_opy1:  db 'pointer y at end', 0

; -----------------------------------------------------------------------------
; bl_mul48 - DX:AX * CX -> [bl_m], 48 bits
; clobbers: flags (every register preserved)
; -----------------------------------------------------------------------------
bl_mul48:
    push ax
    push bx
    push dx
    mov bx, dx                  ; park the high half
    mul cx                      ; DX:AX = low * CX
    mov [bl_m], ax
    mov [bl_m+2], dx
    mov word [bl_m+4], 0
    mov ax, bx
    mul cx                      ; DX:AX = high * CX, weighted one word up
    add [bl_m+2], ax
    adc word [bl_m+4], dx
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; bl_div48 - [bl_m] /= CX, in place, 48 bits
; clobbers: flags (every register preserved)
;
; Word by word from the top with the remainder carried down. Each `div` is
; DX:AX / CX with DX < CX by construction, so the quotient always fits 16 bits
; and the 8086's divide-overflow interrupt - which is a hang inside a package,
; not a wrong answer - cannot fire.
; -----------------------------------------------------------------------------
bl_div48:
    push ax
    push dx
    xor dx, dx
    mov ax, [bl_m+4]
    div cx
    mov [bl_m+4], ax
    mov ax, [bl_m+2]
    div cx
    mov [bl_m+2], ax
    mov ax, [bl_m]
    div cx
    mov [bl_m], ax
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; bl_get32 - DX:AX = the low 32 bits of [bl_m], SATURATED
; out:      DX:AX; CF = 1 if it did not fit
; -----------------------------------------------------------------------------
bl_get32:
    mov ax, [bl_m]
    mov dx, [bl_m+2]
    cmp word [bl_m+4], 0
    je .ok
    mov ax, 0xFFFF
    mov dx, 0xFFFF
    stc
    ret
.ok:
    clc
    ret

; -----------------------------------------------------------------------------
; bl_div32 - DX:AX /= CX
; out:      DX:AX = quotient, BX = remainder
; clobbers: AX, BX, DX, flags
;
; The 8086 divides 32 by 16 only when the quotient fits 16 bits, which it does
; not here, so it is two halves with the first remainder carried into the
; second.
; -----------------------------------------------------------------------------
bl_div32:
    push si
    mov si, ax                  ; park the low half
    mov ax, dx
    xor dx, dx
    div cx                      ; AX = high quotient, DX = carry
    mov bx, ax
    mov ax, si
    div cx                      ; AX = low quotient, DX = remainder
    mov si, dx
    mov dx, bx
    mov bx, si                  ; BX = the remainder
    pop si
    ret

; bl_div10 - DX:AX /= 10, remainder in BX. Preserves CX, SI, DI.
bl_div10:
    push cx
    mov cx, 10
    call bl_div32
    pop cx
    ret

; -----------------------------------------------------------------------------
; bl_us100 - counts -> hundredths of a microsecond per iteration
; in:       DX:AX = PIT counts, CX = iterations
; out:      DX:AX = us * 100 per iteration
; clobbers: AX, BX, DX, flags
;
; One count is 0.8381 us, so us*100 = counts * 83.81 = counts * 8381 / 100.
; Done 48-bit and divided twice rather than by CX*100, because CX*100 leaves
; the wrong half of the range: N would be capped at 655.
; -----------------------------------------------------------------------------
bl_us100:
    push cx
    push si
    mov si, cx
    mov cx, 8381
    call bl_mul48
    mov cx, 100
    call bl_div48
    mov cx, si
    or cx, cx
    jnz .div
    mov cx, 1
.div:
    call bl_div48
    call bl_get32
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; bl_ratio - DX:AX * 100 / CX, the x100 ratio the report quotes
; out:      DX:AX; 0 when CX is 0
; clobbers: AX, BX, DX, flags
; -----------------------------------------------------------------------------
bl_ratio:
    push cx
    push si
    mov si, cx
    mov cx, 100
    call bl_mul48
    mov cx, si
    or cx, cx
    jnz .div
    mov word [bl_m], 0
    mov word [bl_m+2], 0
    mov word [bl_m+4], 0
    jmp short .out
.div:
    call bl_div48
.out:
    call bl_get32
    pop si
    pop cx
    ret

; --- the line scratch --------------------------------------------------------

; bl_lclr - blank the line scratch. Preserves every register.
bl_lclr:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    mov di, bl_lscr
    mov cx, BL_MAXLINE
    mov al, ' '
    cld
    rep stosb
    mov byte [bl_lscr + BL_MAXLINE], 0
    pop es
    pop di
    pop cx
    pop ax
    ret

; bl_lput - copy the NUL string at SI into the scratch at column DI.
; Stops at BL_MAXLINE. Preserves every register.
bl_lput:
    push ax
    push si
    push di
    cld
    add di, bl_lscr
.c:
    cmp di, bl_lscr + BL_MAXLINE
    jae .e
    lodsb
    or al, al
    jz .e
    mov [di], al
    inc di
    jmp short .c
.e:
    pop di
    pop si
    pop ax
    ret

; bl_dec - DX:AX right-aligned in a CX-wide field at column DI of the scratch.
; Preserves every register.
bl_dec:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    or cx, cx
    jz .out
    add di, bl_lscr
    add di, cx
    dec di                      ; DI -> the field's last character
    mov si, 1                   ; SI = "digits still coming"
.d:
    or si, si
    jz .blank
    call bl_div10               ; DX:AX /= 10, BX = the digit
    add bl, '0'
    mov [di], bl
    or ax, ax
    jnz .next
    or dx, dx
    jnz .next
    xor si, si                  ; value exhausted: pad with spaces from here
    jmp short .next
.blank:
    mov byte [di], ' '
.next:
    dec di
    loop .d
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; bl_usfield - DX:AX (hundredths of a us) as NNNNNNN.NN at column DI.
; Preserves every register.
bl_usfield:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov cx, 100
    call bl_div32               ; DX:AX = whole us, BX = hundredths (0..99)
    mov si, bx                  ; park them - bl_dec wants DX:AX
    mov cx, 9
    call bl_dec                 ; the whole part, right-aligned 9 wide
    add di, bl_lscr
    add di, 9
    mov byte [di], '.'
    mov ax, si                  ; AH is 0: SI is under 100
    mov bl, 10
    div bl                      ; AL = tens, AH = units
    add al, '0'
    add ah, '0'
    mov [di+1], al
    mov [di+2], ah
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; bl_hex4 - AX as four hex digits at column DI of the scratch.
; Preserves every register.
bl_hex4:
    push ax
    push bx
    push cx
    push dx
    push di
    add di, bl_lscr
    mov dx, ax
    mov cx, 4
.d:
    push cx
    mov cl, 4
    rol dx, cl                  ; the top nibble around to the bottom
    pop cx
    mov al, dl
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .st
    add al, 'A'-'0'-10
.st:
    mov [di], al
    inc di
    loop .d
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; bl_lcommit - append the scratch to the report, trailing spaces trimmed
; Preserves every register. Silently drops the line once the arena or the row
; table is full and records it in [bl_full], which the trailer reports - a
; benchmark that quietly loses its last twenty rows is worse than one that
; says it did.
; -----------------------------------------------------------------------------
bl_lcommit:
    push ax
    push bx
    push cx
    push si
    push di
    mov cx, BL_MAXLINE          ; find the last non-space
.tr:
    or cx, cx
    jz .have
    mov bx, cx
    dec bx
    cmp byte [bl_lscr + bx], ' '
    jne .have
    dec cx
    jmp short .tr
.have:
    mov ax, [bl_nrow]
    cmp ax, BL_MAXROWS
    jae .full
    mov bx, [bl_used]
    mov si, bx
    add si, cx
    inc si                      ; ...plus the NUL
    cmp si, BL_ARENA
    ja .full
    shl ax, 1
    add ax, bl_idx
    mov di, ax
    mov [di], bx                ; the row's offset into the arena
    mov di, bl_arena
    add di, bx
    mov si, bl_lscr
    push es
    push ds
    pop es
    cld
    jcxz .nul
    rep movsb
.nul:
    mov byte [di], 0
    pop es
    sub di, bl_arena
    inc di
    mov [bl_used], di
    inc word [bl_nrow]
    jmp short .out
.full:
    mov byte [bl_full], 1
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; bl_sline - SI = a NUL string -> one report line. Preserves every register.
bl_sline:
    push di
    call bl_lclr
    xor di, di
    call bl_lput
    call bl_lcommit
    pop di
    ret

; bl_blank - an empty report line. Preserves every register.
bl_blank:
    push si
    mov si, bl_s_nul
    call bl_sline
    pop si
    ret

; bl_kv - SI = label, DX:AX = value: "label  <value>" as one line.
; in:       SI = label, DX:AX = value, CX = the value field's width
; Preserves every register.
bl_kv:
    push ax
    push cx
    push dx
    push di
    call bl_lclr
    xor di, di
    call bl_lput
    mov di, BL_C_N
    call bl_dec
    call bl_lcommit
    pop di
    pop dx
    pop cx
    pop ax
    ret

; bl_kvs - SI = label, DI = a second NUL string: "label   string" as one line.
; Preserves every register.
bl_kvs:
    push si
    push di
    push bx
    mov bx, di
    call bl_lclr
    xor di, di
    call bl_lput
    mov si, bx
    mov di, BL_C_N
    call bl_lput
    call bl_lcommit
    pop bx
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; bl_head - the column heading, so a row's numbers cannot be read as the wrong
;           quantity. Emitted by the caller wherever a block of rows starts.
; -----------------------------------------------------------------------------
bl_head:
    push si
    mov si, bl_s_head
    call bl_sline
    pop si
    ret

; -----------------------------------------------------------------------------
; bl_paint - draw the visible page of the report
; in:       SI = window ptr; the gfx lock is HELD (this is a W_PAINT or a
;           callback that has already drawn), content already whatever the
;           caller left there
; clobbers: flags (every register preserved)
;
; Every line is ONE OSAPI_FONT_RUN, space-padded to the full content width, so
; the old row goes straight to the new one a cell at a time: no fill pass, no
; blank instant, and no SPEC.md 11.3 granularity failure to gate against
; (SPEC.md 6.1). With WF_SNAP set - which the callers ask for, and which now
; takes on every adapter (SPEC.md 11.94) - the content origin is a multiple of
; 8, so on mono every one of these takes the single-store path and on VGA the
; fallback's glyphs are at least aligned.
; -----------------------------------------------------------------------------
bl_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bx, si
    call OSAPI_WM_GEOM          ; CX = content w, DX = content h
    jc .out
    mov [bl_cw], cx
    mov [bl_ch], dx
    call OSAPI_WM_CONTENT       ; AX = content left, DX = content top
    mov [bl_cx], ax
    mov [bl_cy], dx
    mov ax, [bl_cw]
    mov cl, 3
    shr ax, cl                  ; content width in 8px cells
    cmp ax, BL_MAXLINE
    jbe .cols
    mov ax, BL_MAXLINE
.cols:
    mov [bl_vcols], ax
    mov ax, [bl_ch]
    mov cl, 3
    shr ax, cl                  ; content height in 8px rows
    or ax, ax
    jnz .rows
    inc ax
.rows:
    mov [bl_vrows], ax
    dec ax                      ; the last row is the status line
    mov [bl_prows], ax
    call bl_clamp

    xor bp, bp                  ; BP = the screen row being drawn
.line:
    mov ax, bp
    cmp ax, [bl_prows]
    jae .status
    add ax, [bl_top]
    call bl_fetch               ; SI -> the row's text, or the empty string
    jmp short .draw
.status:
    call bl_status
    mov si, bl_lscr
.draw:
    call bl_pad                 ; -> bl_draw, space-padded to [bl_vcols]
    mov ax, bp
    mov cl, 3
    shl ax, cl                  ; 8 px per text row - the glyph cell's own
    add ax, [bl_cy]             ; height, so the page holds as many as it can
    mov dx, ax
    mov cx, [bl_cx]
    mov si, bl_draw
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    inc bp
    mov ax, bp
    cmp ax, [bl_vrows]
    jb .line
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; bl_clamp - keep [bl_top] inside the report. Preserves every register.
bl_clamp:
    push ax
    push dx
    mov ax, [bl_nrow]
    sub ax, [bl_prows]
    jnc .max
    xor ax, ax
.max:
    cmp [bl_top], ax
    jbe .lo
    mov [bl_top], ax
.lo:
    pop dx
    pop ax
    ret

; bl_fetch - AX = a row index -> SI = its text (the empty string past the end)
; clobbers: SI, flags
bl_fetch:
    push bx
    cmp ax, [bl_nrow]
    jb .have
    mov si, bl_s_nul
    jmp short .out
.have:
    mov bx, ax
    shl bx, 1
    add bx, bl_idx
    mov si, [bx]
    add si, bl_arena
.out:
    pop bx
    ret

; bl_pad - copy the NUL string at SI into bl_draw, space-padded to [bl_vcols]
; Preserves every register.
bl_pad:
    push ax
    push cx
    push si
    push di
    push es
    push ds
    pop es
    cld
    mov di, bl_draw
    mov cx, [bl_vcols]
.c:
    jcxz .done
    lodsb
    or al, al
    jz .fill
    stosb
    dec cx
    jmp short .c
.fill:
    mov al, ' '
    rep stosb
.done:
    mov byte [di], 0
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; bl_status - build the status line into the scratch
; Preserves every register.
; -----------------------------------------------------------------------------
bl_status:
    push ax
    push cx
    push dx
    push si
    push di
    call bl_lclr
    mov si, bl_s_stat
    xor di, di
    call bl_lput
    mov ax, [bl_top]
    inc ax
    xor dx, dx
    mov di, 5
    mov cx, 4
    call bl_dec
    mov si, bl_s_dash
    mov di, 9
    call bl_lput
    mov ax, [bl_top]
    add ax, [bl_prows]
    cmp ax, [bl_nrow]
    jbe .b
    mov ax, [bl_nrow]
.b:
    xor dx, dx
    mov di, 10
    mov cx, 4
    call bl_dec
    mov si, bl_s_of
    mov di, 15
    call bl_lput
    mov ax, [bl_nrow]
    xor dx, dx
    mov di, 18
    mov cx, 4
    call bl_dec
    mov si, bl_s_keys
    mov di, 24
    call bl_lput
    cmp byte [bl_full], 0
    je .sv
    mov si, bl_s_trunc
    mov di, 58
    call bl_lput
    jmp short .out
.sv:
    cmp byte [bl_saved], 0
    je .out
    mov si, [bl_savemsg]
    mov di, 58
    call bl_lput
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; bl_progress - overwrite the status row with a message
; in:       SI = a NUL string; [bl_cx]/[bl_cy]/[bl_prows]/[bl_vcols] valid;
;           the gfx lock is HELD
; clobbers: flags (every register preserved)
;
; A suite is ten to forty SECONDS on the machine this is for, and it runs
; inside one window callback - so the gfx lock is held for all of it, the
; cursor is parked and nothing on screen moves. That is PERFORMANCE.md Part 1's
; third defect exactly: a machine that has stopped answering is indistinguishable
; from a machine that has died, and the first thing a user does about the second
; is reach for the power switch. One opaque row per block costs about 80 ms of a
; twenty-second run and is the difference.
;
; It is drawn on the STATUS row, which is the first row below GB_BOXH - the
; drawing sandbox ends at content y 127 and this starts at 128 on the shortest
; adapter - so it can never be scribbled on by the primitives being measured,
; and it is outside every measured span.
; -----------------------------------------------------------------------------
bl_progress:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call bl_pad                 ; SI -> bl_draw, padded to the full width
    mov ax, [bl_prows]
    mov cl, 3
    shl ax, cl
    add ax, [bl_cy]
    mov dx, ax
    mov cx, [bl_cx]
    mov si, bl_draw
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN         ; opaque, so there is no blank instant
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; bl_key - the paging keys
; in:       AL = ascii, AH = scan
; out:      CF = 0 the view moved and the caller should repaint; CF = 1 not
;           one of ours
; clobbers: flags (every register preserved)
; -----------------------------------------------------------------------------
bl_key:
    push ax
    push bx
    push cx
    push dx
    mov bx, [bl_top]
    cmp al, ' '
    je .pgdn
    cmp ah, 0x51                ; PgDn
    je .pgdn
    cmp ah, 0x49                ; PgUp
    je .pgup
    cmp ah, 0x50                ; Down
    je .dn
    cmp ah, 0x48                ; Up
    je .up
    cmp ah, 0x47                ; Home
    je .home
    cmp ah, 0x4F                ; End
    je .end
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret
.pgdn:
    add bx, [bl_prows]
    jmp short .set
.pgup:
    sub bx, [bl_prows]
    jnc .set
    xor bx, bx
    jmp short .set
.dn:
    inc bx
    jmp short .set
.up:
    or bx, bx
    jz .set
    dec bx
    jmp short .set
.home:
    xor bx, bx
    jmp short .set
.end:
    mov bx, 0xFFFF              ; bl_clamp pulls it back to the last page
.set:
    mov [bl_top], bx
    call bl_clamp
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; bl_save - write the whole report to a file (SPEC.md 18.4)
; in:       SI = a NUL 8.3 name
; out:      [bl_saved] = 1, [bl_savemsg] -> the word the status line shows
; clobbers: flags (every register preserved)
;
; UI-task context only, which every caller of this is (a menu command or a key
; handler). The lines go out CR LF terminated so a DOS or a modern host reads
; the file as text without a converter.
;
; --- WHY bl_out IS 512-ALIGNED, AND WHY QEMU WILL NEVER TELL YOU -------------
;
; dsk_xfer issues `int 13h` DIRECTLY TO ES:BX. There is no staging buffer: the
; floppy controller's DMA reads the bytes out of THIS package's memory. An ISA
; bus master cannot carry into the 8237's page port, so a transfer that would
; cross a 64KB PHYSICAL boundary wraps to the start of its page and moves the
; wrong memory instead - which is why every disk-visible base in this system is
; 512-byte aligned (CLAUDE.md; a 512-aligned start plus 512 bytes cannot cross
; one). A package's bss offset is aligned to nothing at all.
;
; So BL_BSS puts bl_out first and the caller passes a base that is a multiple
; of 512: `align 512` before OS88_IMAGE_END makes os88_image_end one, the
; caller's own bss size is rounded up to one, and the loader's region base
; already is one (roundup512 of image+bss, claimed in whole KB). The offset is
; then a multiple of 512 from a 512-aligned base, which is what dskw_norm's
; arithmetic needs.
;
; None of this can fail under QEMU, whose floppy DMA does not model the page
; wrap - PERFORMANCE.md Part 3's shape exactly, one layer down. The symptom on
; real hardware is a saved report full of memory that belongs to something
; else, or a "Disk error" on the save.
; -----------------------------------------------------------------------------
bl_save:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push si                     ; the name, for the write below
    push ds
    pop es
    cld
    mov di, bl_out
    xor bx, bx                  ; BX = the row index
.row:
    cmp bx, [bl_nrow]
    jae .done
    mov ax, bx
    call bl_fetch               ; SI -> the text
.c:
    lodsb
    or al, al
    jz .eol
    cmp di, bl_out + BL_OUTSZ - 4
    jae .eol
    stosb
    jmp short .c
.eol:
    mov al, 13
    stosb
    mov al, 10
    stosb
    inc bx
    cmp di, bl_out + BL_OUTSZ - 4
    jb .row
.done:
    mov cx, di
    sub cx, bl_out              ; CX = bytes; the report cannot reach 64KB
    xor dx, dx
    mov bx, bl_out
    pop si                      ; the name
    call OSAPI_FILE_WRITE       ; ES:BX = the text, DX:CX = the count
    mov byte [bl_saved], 1
    mov word [bl_savemsg], bl_s_wrote
    jnc .out
    mov word [bl_savemsg], bl_s_werr
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
; bl_drive - the current volume's drive letter (SPEC.md 19.2), for the report:
;            a results file is worth very little if you cannot find it
; out:      AL = 'A'..'F'
; clobbers: AX, flags
; -----------------------------------------------------------------------------
bl_drive:
    push bx
    push dx
    call OSAPI_FILE_HERE        ; BL = the drive, DX = the cwd's first cluster
    mov al, bl
    add al, 'A'
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; bl_dbgfind - look a published block up in the debug registry (SPEC.md 57)
; in:       AX = the two-character tag
; out:      CF=0 and ES:BX -> the block; CF=1 = not published in this build
; clobbers: BX, ES, flags
;
; A TEST package reads kernel state this way and shipped software never does
; (SPEC.md 57). A reader that cannot find its block SAYS SO AND CONTINUES,
; which is what lets one build of a harness run against a plain kernel and a
; knob-built one - so every caller here treats CF=1 as a row it cannot fill
; rather than as a failure.
;
; It checks the tag TWICE: once in the registry pair and again as the block's
; own first word, which is SPEC.md 57's whole reason for putting it there. An
; offset followed to the wrong place then answers "not published" instead of
; handing back somebody else's words.
;
; It lives in benchlib because BOTH harnesses want it - sysbench for its four
; state dumps, gfxbench to find out which DISPLAY its sandbox is on (39.14.7's
; report, and the reason gfxbench used to name its file after the primary).
; -----------------------------------------------------------------------------
bl_dbgfind:
    push ax
    push si
    mov bx, KERNEL_SEG
    mov es, bx
    mov si, [es:0x000E]         ; the registry, or 0
    or si, si
    jz .none
.scan:
    mov bx, [es:si]             ; the tag
    or bx, bx
    jz .none                    ; end of list: not published here
    cmp bx, ax
    je .hit
    add si, 4
    jmp short .scan
.hit:
    mov bx, [es:si+2]           ; ...and the block behind it
    cmp [es:bx], ax             ; which must lead with the same tag
    jne .none
    pop si
    pop ax
    clc
    ret
.none:
    pop si
    pop ax
    stc
    ret

; --- benchlib data -----------------------------------------------------------

bl_s_nul:   db 0
bl_s_us:    db 'us', 0
bl_s_head:  db 'measurement                 N    counts         us/op', 0
bl_s_stat:  db 'rows', 0
bl_s_dash:  db '-', 0
bl_s_of:    db 'of', 0
bl_s_keys:  db 'Spc/PgDn PgUp Up Dn Home End', 0
bl_s_trunc: db 'REPORT TRUNCATED', 0
bl_s_wrote: db 'saved', 0
bl_s_werr:  db 'SAVE FAILED', 0

; -----------------------------------------------------------------------------
; BL_BSS base - declare benchlib's loader-zeroed bss at the given address.
; Invoke it after OS88_IMAGE_END, past your own bss:
;     BL_BSS os88_image_end + MY_BSS_OWN
; and size OS88_BSS as MY_BSS_OWN + BL_BSS_SIZE.
; -----------------------------------------------------------------------------
%macro BL_BSS 1
bl_out     equ (%1) + 0                 ; the CR LF image bl_save writes - and
                                        ; FIRST, because it is the one buffer
                                        ; here that int 13h touches. The caller
                                        ; must pass a 512-ALIGNED offset; see
                                        ; the note above bl_save.
bl_lscr    equ bl_out + BL_OUTSZ        ; the line under construction
bl_draw    equ bl_lscr + BL_MAXLINE + 4 ; ...padded for one font_run
bl_arena   equ bl_draw + BL_MAXLINE + 4 ; packed NUL-terminated line text
bl_idx     equ bl_arena + BL_ARENA      ; BL_MAXROWS words: offsets into it
bl_nrow    equ bl_idx + BL_MAXROWS * 2  ; word: lines in the report
bl_used    equ bl_nrow + 2              ; word: arena bytes spent
bl_top     equ bl_used + 2              ; word: first line on screen
bl_prows   equ bl_top + 2               ; word: text rows the content holds
bl_vrows   equ bl_prows + 2             ; word: ...including the status line
bl_vcols   equ bl_vrows + 2             ; word: cells across the content
bl_cx      equ bl_vcols + 2             ; word: content left
bl_cy      equ bl_cx + 2                ; word: content top
bl_cw      equ bl_cy + 2                ; word: content width
bl_ch      equ bl_cw + 2                ; word: content height
bl_acc     equ bl_ch + 2                ; dword: the row's folded counts
bl_prev    equ bl_acc + 4               ; word: the previous PIT / tick read
bl_max     equ bl_prev + 2              ; word: worst single-iteration delta
bl_n       equ bl_max + 2               ; word: iterations of the current row
bl_body    equ bl_n + 2                 ; word: the near proc being measured
bl_meth    equ bl_body + 2              ; byte: 0 method P, 1 method T
bl_full    equ bl_meth + 1              ; byte: the arena overflowed
bl_saved   equ bl_full + 1              ; byte: a save has been attempted
bl_savemsg equ bl_saved + 1             ; word: what to say about it
bl_last    equ bl_savemsg + 2           ; dword: the last row's net counts
bl_lastus  equ bl_last + 4              ; dword: ...and its hundredths of a us
bl_ovhx    equ bl_lastus + 4            ; word: loop overhead, 1/256 counts
bl_bcnt    equ bl_ovhx + 2              ; dword: the baseline row's raw counts
bl_t0      equ bl_bcnt + 4               ; word: the tick at the row's start
bl_tick    equ bl_t0 + 2                 ; word: ...and ticks elapsed over it
bl_lapped  equ bl_tick + 2               ; byte: the row lapped the PIT
bl_m       equ bl_lapped + 1             ; 3 words: the 48-bit accumulator
bl_mrows   equ bl_m + 6                  ; word: rows the pointer moved across
bl_mnrow   equ bl_mrows + 2              ; word: rows watched (= rows measured)
bl_mx0     equ bl_mnrow + 2              ; word: where the pointer started...
bl_my0     equ bl_mx0 + 2
bl_mx      equ bl_my0 + 2                ; ...and where it was last looked at
bl_my      equ bl_mx + 2
bl_mxlo    equ bl_my + 2                 ; word: the pointer's bounding box
bl_mxhi    equ bl_mxlo + 2               ; over the whole suite - a jiggle
bl_mylo    equ bl_mxhi + 2               ; that comes back to where it started
bl_myhi    equ bl_mylo + 2               ; moves no SAMPLE but widens this
bl_mhave   equ bl_myhi + 2               ; byte: the box has been seeded
%endmacro

; ...and its size, for the caller's OS88_BSS. The tail past bl_out is 70
; bytes of scalars; 96 is that with the slack a new scalar would want.
BL_BSS_SIZE equ BL_MAXLINE * 2 + 8 + BL_ARENA + BL_MAXROWS * 2 + BL_OUTSZ + 96
