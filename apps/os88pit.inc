; =============================================================================
; os8088 - apps/os88pit.inc - a 32-bit clock in 838ns units (SPEC.md 72.15.1)
;
; Counter 0 of the 8253, latched and read exactly as tests/benchlib.inc reads
; it - one count is 838ns and it reloads every 55ms - EXTENDED UPWARD by the
; BIOS tick at 0040:006C, which our own int 08h hook chains. One tick is
; exactly 65536 counts, so the two halves are one number in one unit and a
; span may run for an HOUR before it wraps - the high half is the BIOS tick's
; low word, which laps in 65536 ticks of 54.92ms, and 2^32 counts of 838ns is
; the same 3600 seconds from the other end.
;
; **A SPAN LONGER THAN 55ms IS THE WHOLE REASON THE HIGH HALF EXISTS.** Read
; the counter alone and a 561ms span reports 561 mod 54.92 = 12ms - a small
; plausible number with nothing wrong-looking about it, which is the failure
; benchlib's own header spends a page on and which once published a primitive
; doing twenty times the work as 2.4x FASTER.
;
; The tick is read BEFORE and AFTER the counter and the pair is taken again if
; it moved: without that, a read landing on the reload takes the old tick with
; the freshly reloaded counter and reports 55ms that did not happen. IF is
; cleared across the latch and the INs and NOWHERE ELSE - holding it down
; across the tick reads as well would defeat the check, because the tick
; cannot move while its ISR cannot run.
;
; **THE RE-READ ALONE DOES NOT CLOSE IT.** A reload that lands INSIDE the cli
; leaves IRQ0 pending, and the CPU need not have taken it by the time the tick
; is read again - so the pair passes with the old tick and the new count, one
; tick EARLY. MEASURED on MartyPC's 5150: 40 such reads in 60,000 over 211
; ticks, and PIXELSTEIN's Auto judged a 98.2 ms frame as 43.3 (98.2 less one
; tick) about one run in twelve of tests/pxsauto.py. So the PIC's IRR is read
; in the same window: IRQ0 pending with the count under half a tick in means
; the reload came before the latch and its tick is owed - the kernel's own
; accounting has done exactly this all along (sched.inc's .cohere).
;
; **A SPAN THAT SITS INSIDE A cli LONGER THAN 55ms UNDER-REPORTS**, for that
; same reason. Nothing that uses this holds interrupts off for anything like
; it, and it is written down because the failure would otherwise be silent.
;
; ONE BODY, TWO USERS: ETHER.DRV's stage profiler (SPEC.md 72.15) and the FTP
; server's own split (SPEC.md 77.32). Two clocks that were copy-pasted cannot
; disagree, which is exactly what makes a disagreement between them useless -
; and this project has found three sizing bugs by making two harnesses argue
; (PERFORMANCE.md Part 6 rule 7). They argue about STAGES here, over one
; clock.
; =============================================================================

; -----------------------------------------------------------------------------
; pit_now - DX:AX = a 32-bit count of 838ns units
; clobbers: AX, DX and the flags
; -----------------------------------------------------------------------------
pit_now:
    push cx
    push es
    mov cx, 0x40
    mov es, cx
.again:
    mov dx, [es:0x6C]           ; the tick, then the counter...
    pushf
    cli
    mov al, 0
    out 0x43, al                ; latch counter 0 - a READ command, so the
    jmp short $+2               ; system tick itself is undisturbed
    in  al, 0x40
    mov cl, al
    jmp short $+2
    in  al, 0x40
    mov ch, al                  ; CX = the latched count
    mov al, 0x0A                ; OCW3: the next read of 0x20 is the IRR
    out 0x20, al
    jmp short $+2
    in  al, 0x20                ; bit 0: IRQ0 raised and not yet TAKEN
    popf
    cmp dx, [es:0x6C]           ; ...then the tick again: unchanged means the
    jne .again                  ; two halves belong to each other
    neg cx                      ; the counter runs DOWN; count up in the tick
    test al, 1
    jz .cohere
    or cx, cx                   ; pending, and under half a tick in: the
    js .cohere                  ; reload came BEFORE the latch and its tick
    inc dx                      ; is not in 006C yet - sched.inc's .cohere
.cohere:
    xchg ax, cx
    pop es
    pop cx
    ret
