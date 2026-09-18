; =============================================================================
; moudiag.inc - MOUDIAG=1: what mouse_init's identify window actually saw
;               (SPEC.md 9.4.6)
;
; SPEC.md 9.4.5 closes the identify window early when a port has answered LIKE
; A MOUSE - first byte 'M', at most MOU_IDMAX bytes, quiet for MOU_IDQUIET -
; and takes mouse_init from 1,200 ms to 596. A machine with a working mouse
; that still spends the whole MOU_IDWIN is therefore failing one of those three
; tests, and which one it is decides the fix: nothing arrived at all (the raise
; is not provoking the mouse), something arrived that was not 'M', too much
; arrived, or it never went quiet.
;
; Every byte of that verdict is already banked per port by mou_idbyte and is
; dead for the life of the machine afterwards (SPEC.md 9.4.1), so this reads
; state rather than adding any: the ONLY thing it stamps is the window's own
; open tick and the ticks it ended up spending, because [ticks] at the close is
; the one fact nothing keeps.
;
; **It is a KNOB and not a registry entry** (SPEC.md 57.2's rule) because it
; DRAWS: the machines this question is about are 86Box and the field 5150,
; which have no debug socket and no way to run a reader - so the delivery
; mechanism has to be the glass, exactly as SPEC.md 15.5's phase table is.
; tests/sysbench already reads the same block through SPEC.md 57's registry on
; a machine that can run a package, and shows port 0 alone.
;
; It is `.text` rather than `.cold` for the reason the phase table is not: it
; is knob-only, KERN_BUDGET is skipped for a knob kernel, and `.text` is the
; segment whose strings and data DS reaches without an override.
;
; Drawn with font_run and NOT font_str (SPEC.md 6.6): the lines are padded to
; MDG_W cells, so the padding IS the erase and there is no fill underneath -
; which is also why this needs no entry in tests/textsites.txt.
; =============================================================================
%ifdef MOU_DIAG
section .text

%ifdef BOOT_PROFILE
; ...BESIDE SPEC.md 15.5's phase table, whose fourteen rows from BP_Y own the
; left margin. Not BELOW it, which the first draft tried: that table is 140px
; tall and a CGA screen is 200, so six more rows landed on the DOCK - and
; os88marty's settle() gate reads the dock strip to decide the desktop is up,
; so instdeep and hdboot both timed out on a machine that had booted perfectly.
; A diagnostic that hides the thing the harness measures is worse than none.
; 252 + 40*8 = 572, inside a 640 CGA and a 720 Hercules alike.
MDG_X       equ BP_VALX + 8*8 + 8
MDG_Y       equ BP_Y
%else
MDG_X       equ 8               ; left margin, byte-aligned so font_run takes
MDG_Y       equ 28              ; its fast path; below the menu bar (MBAR_H 20)
%endif
%ifdef MOU_ROUND
MDG_ROW     equ 9               ; 8px cell + 1, for the ROUND build alone.
                                ; SPEC.md 9.4.6.5 makes it fifteen rows, and
                                ; fifteen at a pitch of 10 puts the last glyph
                                ; row's bottom at exactly 176 - which IS the
                                ; dock's black rule on a 200-line CGA, and
                                ; hiding the strip os88marty's settle() reads
                                ; is the failure 9.4.6.2 already recorded once
%else
MDG_ROW     equ 10              ; 8px cell + 2 - unchanged, so a plain MOUDIAG
                                ; still looks like 9.4.6's own photographs
%endif
MDG_RATE    equ 9               ; ticks between redraws - ~0.5s at 18.2 Hz
MDG_W       equ 40              ; cells a line, padded - 320px, inside 640 and
                                ; inside a Hercules 720. On CGA's 200 rows the
                                ; five lines end at y 78, clear of the dock

; --- SPEC.md 9.4.6.5: the WIRE half, and the round that walks the causes ------
; The rows above are all about the identify WINDOW, which is over 1.2 s into a
; boot and never runs again. A mouse that is not found at boot and works when
; it is hot-plugged onto a settled port (docs/FIELD-NOTES.md 44) is a fact
; about the rest of the session, so these count for the life of the machine.
MDB_PHT     equ 273             ; ticks a phase lasts - ~15 s at 18.2 Hz, long
                                ; enough for the poller to run four of its own
                                ; MOU_REPOLL cycles inside phase A
MDB_LONG    equ 20              ; ...and the LOW hold phase C substitutes for
                                ; MOU_RSTLOW: ~1.1 s unpowered against ~165 ms
MDB_NPH     equ 4               ; A normal, B hold, C long low, D hold+need 1 -
                                ; and D is terminal, being the most permissive
                                ; state and so the one to leave the user in

; -----------------------------------------------------------------------------
; mdg_show - the table, over the finished desktop
; in:  nothing.  out: nothing (every register preserved)
;
; LAST in kmain, after cursor_show and drv_notice, so nothing paints over it -
; and it goes away on the first repaint, which is what makes it a diagnostic
; rather than a window somebody has to close (SPEC.md 15.5's argument).
; -----------------------------------------------------------------------------
mdg_show:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call gfx_lock

    mov bp, MDG_Y

    mov si, mdg_t1              ; --- the window itself
    call mdg_tpl
    mov ax, [mdg_open]
    mov di, mdg_buf + 9         ; the template's own dot columns: 'win open '
    call mdg_h16                ; is nine characters and 'used ' ends at 19
    mov ax, [mdg_used]
    mov di, mdg_buf + 20
    call mdg_h16
    call mdg_line

    mov si, mdg_t2              ; --- the column heads
    call mdg_tpl
    call mdg_line

    xor bx, bx                  ; --- one line a PORT, rows 0 and 2
.port:
    mov si, mdg_t3
    call mdg_tpl
    mov al, bl                  ; the row index, as it is everywhere else
    add al, '0'
    mov [mdg_buf + 2], al
    mov ax, [bx+mou_bases]
    mov di, mdg_buf + 5
    call mdg_h16
    mov al, [bx+mou_idn]
    mov di, mdg_buf + 11
    call mdg_h8
    mov al, [bx+mou_idb0]
    mov di, mdg_buf + 15
    call mdg_h8
    mov ax, [bx+mou_idlast]     ; RELATIVE to the open, which is the only form
    sub ax, [mdg_open]          ; of it that means anything to a reader
    cmp byte [bx+mou_idn], 0
    jne .last
    mov ax, 0xFFFF              ; nothing ever arrived: FFFF, not a stale tick
.last:
    mov di, mdg_buf + 19
    call mdg_h16
    mov al, [bx+mou_ident]
    mov di, mdg_buf + 26
    call mdg_h1
    mov al, [bx+mou_need]
    mov di, mdg_buf + 31
    call mdg_h1
    mov al, [bx+mou_run]
    mov di, mdg_buf + 35
    call mdg_h1
    call mdg_line
    add bx, 2
    cmp bx, MOU_PEND
    jb .port

    mov si, mdg_t4              ; --- and the verdicts the rest of the kernel
    call mdg_tpl                ; hangs off
    mov al, [mou_idany]
    mov di, mdg_buf + 6
    call mdg_h1
    mov al, [mou_port]
    mov di, mdg_buf + 14
    call mdg_h1
    mov al, [mou_seen]
    mov di, mdg_buf + 22
    call mdg_h1
    mov al, [mou_p2st]
    mov di, mdg_buf + 30
    call mdg_h8
    mov al, [mou_hpst]
    mov di, mdg_buf + 39
    call mdg_h1
    call mdg_line

    mov si, mdg_t7              ; --- THE OTHER SOCKET (SPEC.md 9.9.6), which
    call mdg_tpl                ; the p2st byte on the row above can only half
    mov al, [cpu_tier]          ; answer. Reading order is the handshake's:
    mov di, mdg_buf + 5         ; tier 0 means the module never ran at all, and
    call mdg_h1                 ; every column after it is meaningless
    mov al, [mou_p2aux]         ; ...did 0xA8 take? E0 yes, E1 no, EE the
    mov di, mdg_buf + 11        ; command byte never came back
    call mdg_h8
    mov al, [mou_p2cmd0]        ; ...the 8042 command byte the BIOS left
    mov di, mdg_buf + 19
    call mdg_h8
    mov al, [mou_p2id]
    mov di, mdg_buf + 26
    call mdg_h8
    mov ax, [mou_p2irq]         ; ...and the two that say whether the LINE
    mov di, mdg_buf + 34        ; works, which no other row here can
    call mdg_h16
    call mdg_line

    mov si, mdg_t8
    call mdg_tpl
    mov al, [mou_p2]
    mov di, mdg_buf + 3
    call mdg_h1
    mov al, [mou_p2lb0]         ; the last byte 0: bit 3 set is a synced
    mov di, mdg_buf + 9         ; stream, and anything else is a desync
    call mdg_h8
    mov al, [mou_ptr]           ; ...and the byte the KEYBOARD MOUSE reads, so
    mov di, mdg_buf + 17        ; "why is the keyboard mouse up" is answered on
    call mdg_h1                 ; the same screen (SPEC.md 9.6.6)
    mov ax, [mou_p2pkt]
    mov di, mdg_buf + 24        ; ...and complete packets decoded
    call mdg_h16
    mov ax, [mouse_x]
    mov di, mdg_buf + 32
    call mdg_h16
    call mdg_line

    mov si, mdg_t9              ; --- ...and WHERE inside step 2 it stopped,
    call mdg_tpl                ; which p2st cannot say: four of its calls fail
%ifdef KERN_BIG                 ; the PS/2 probe is kern_big's (SPEC.md 9.9):
    mov al, [mou_p2sub]         ; into the same .fail (SPEC.md 9.9.6)
    mov di, mdg_buf + 4
    call mdg_h1
    mov al, [mou_p2pre]         ; the 8042 status before 0xA8...
    mov di, mdg_buf + 11
    call mdg_h8
    mov al, [mou_p2m1]          ; ...the MASTER 8259's mask as the probe found
    mov di, mdg_buf + 19        ; it: bit 1 is IRQ1, whose handler is what
                                ; takes a reply out from under the probe
    call mdg_h8
    mov al, [mou_p2sta]         ; ...and at the moment it gave up
    mov di, mdg_buf + 26
    call mdg_h8
%endif                          ; kern_small has none of those four bytes
    mov al, [mou_p2cm1]
    mov di, mdg_buf + 34
    call mdg_h8
    call mdg_line

    mov si, mdg_t5              ; --- and WHAT THE BAR ACTUALLY SAT THROUGH
    call mdg_tpl                ; (SPEC.md 9.4.6.1): n1-n0 is mouse_init and
    mov ax, [mdg_n0]            ; n2-n1 is desk_init's floppy probe, so the
    sub ax, [mdg_nlive]         ; table says whether a stall is the phase the
    mov di, mdg_buf + 7         ; reader thinks it is instead of leaving it to
    call mdg_h16                ; a stopwatch and an assumption - and CHROME is
                                ; what the hard-disk path draws inside the frame
                                ; the bar first shows (9.4.6.2), which a floppy
                                ; boot has already spent before kmain and reads
                                ; here as whatever [ticks] was at boot
    mov ax, [mdg_n1]
    sub ax, [mdg_n0]
    mov di, mdg_buf + 19
    call mdg_h16
    mov ax, [mdg_n2]
    sub ax, [mdg_n1]            ; (named for the PHASE, not the percentage: a
    mov di, mdg_buf + 29        ; floppy boot spends both at 92%, the loader
    call mdg_h16                ; having ticked the bar all the way there)
    call mdg_line

    mov si, mdg_t6              ; --- ...and WHY the probe went the way it did
    call mdg_tpl                ; (SPEC.md 9.4.6.4). 18.97 publishes all of it
    mov al, [fdd_dbg_u+FDU_ST3] ; per unit already (57.5) and the table is
    mov di, mdg_buf + 9         ; indexed from UNIT 1, so +0 IS unit 1 - the
    call mdg_h8                 ; only one desk_init contests
    mov al, [fdd_dbg_u+FDU_ST3B]
    mov di, mdg_buf + 12
    call mdg_h8
    mov al, [fdd_dbg_u+FDU_ST0]
    mov di, mdg_buf + 19
    call mdg_h8
    mov al, [fdd_dbg_u+FDU_STEP]
    mov di, mdg_buf + 27
    call mdg_h1
    mov al, [fdd_dbg_u+FDU_VRD]
    mov di, mdg_buf + 33
    call mdg_h1
    mov al, [fdd_dbg_eqp]
    mov di, mdg_buf + 39
    call mdg_h1
    call mdg_line

    ; --- SPEC.md 9.4.6.5: WHAT THE WIRE HAS DONE SINCE, which every row above
    ; is blind to. rx is the fork the whole question turns on - zero on both
    ; ports after a full round says the mouse has never put a byte on the
    ; wire, and anything else says it has and the kernel did not believe it
    mov si, mdb_t1
    call mdg_tpl
    call mdg_line

    xor bx, bx
.wire:
    mov si, mdb_t2
    call mdg_tpl
    mov al, bl
    add al, '0'
    mov [mdg_buf + 2], al
    mov ax, [bx+mdb_rx]
    mov di, mdg_buf + 5
    call mdg_h16
    mov al, [bx+mdb_err]        ; the LSR bits the ISR has ever seen set: 02
    mov di, mdg_buf + 12        ; overrun, 04 parity, 08 FRAMING, 10 break.
    call mdg_h8                 ; framing is a mouse talking at the wrong rate
    mov al, 0                   ; ...and MSR/MCR are read LIVE off the port,
    mov dx, [bx+mou_bases]      ; because "is the line even up" is a question
    or  dx, dx                  ; about now and not about boot
    jz .nomsr
    add dx, 6                   ; MSR: CTS/DSR/RI/DCD - a mouse drawing power
    in  al, dx                  ; usually pulls one of them
.nomsr:
    mov di, mdg_buf + 16
    call mdg_h8
    mov al, 0
    mov dx, [bx+mou_bases]
    or  dx, dx
    jz .nomcr
    add dx, 4                   ; MCR: 0B is DTR|RTS|OUT2 - the mouse powered
    in  al, dx                  ; - and 08 is OUT2 alone, i.e. mid reset hold
.nomcr:
    mov di, mdg_buf + 20
    call mdg_h8
    mov al, [bx+mdb_l0]         ; the last four bytes, newest first: 4D is 'M'
    mov di, mdg_buf + 24
    call mdg_h8
    mov al, [bx+mdb_l1]
    mov di, mdg_buf + 27
    call mdg_h8
    mov al, [bx+mdb_l2]
    mov di, mdg_buf + 30
    call mdg_h8
    mov al, [bx+mdb_l3]
    mov di, mdg_buf + 33
    call mdg_h8
    mov ax, [bx+mdb_dt]         ; ticks from OUR rising edge to this port's
    mov di, mdg_buf + 36        ; first byte after it - FFFF is "never spoke".
    call mdg_h16                ; A mouse slower than MOU_REPOLL can never
    call mdg_line               ; show anything but FFFF in phase A
    add bx, 2
    cmp bx, MOU_PEND
    jb .wire

%ifdef MOU_ROUND
    mov si, mdb_t3              ; --- ...and where the round has got to
    call mdg_tpl
    mov al, [mdb_ph]
    add al, 'A'
    mov [mdg_buf + 6], al
    mov al, [mdb_ph]            ; the phase's NAME, four cells, so the reader
    mov ah, 0                   ; needs no key to the letter
    shl ax, 1
    shl ax, 1
    mov si, ax
    add si, mdb_pnam
    mov di, mdg_buf + 9
    mov cx, 4
.pnam:                          ; a hand loop and NOT `rep movsb`: movsb's
    mov al, [si]                ; destination is ES-relative, and ES here is
    mov [di], al                ; whatever the desktop paint left. That is
    inc si                      ; SPEC.md 9.4.6.3's own ruled-out defect in
    inc di                      ; this very routine, arriving a second time
    loop .pnam
    mov ax, [ticks]             ; ticks this phase has been running
    sub ax, [mdb_pht]
    mov di, mdg_buf + 17
    call mdg_h16
    mov ax, [mdb_cyc]           ; ...and reset edges the POLLER has offered
    mov di, mdg_buf + 27
    call mdg_h16
    mov al, [mdb_hold]
    mov di, mdg_buf + 38
    call mdg_h1
    call mdg_line

    mov si, mdb_t4              ; --- the legend, so a photograph of the screen
    call mdg_tpl                ; explains itself with nothing else in the frame
    call mdg_line
%endif

    call gfx_unlock
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mdg_tick - redraw the table every MDG_RATE ticks, from ui_task (SPEC.md 9.9.6)
;
; mdg_show is called ONCE, at the end of kmain, and that is right for the six
; rows it was written for: they are all settled before the desktop exists and
; a diagnostic that has to be closed is a window rather than a diagnostic.
;
; SPEC.md 9.9.6's two rows are not like that. irq, pkt and b0 count what the
; POINTING DEVICE does, so at the end of kmain they read zero on a working
; machine and zero on a broken one - which is the one thing a diagnostic may
; not do. So on this knob the table is kept live, and "move the mouse and read
; the counters" becomes a question the glass can answer on a machine with no
; debugger, which is the whole reason SPEC.md 57.2 makes this a knob.
;
; It is deliberately NOT every pass: ui_task blocks on a tick (SPEC.md 8.1.2)
; and a table redrawn 18 times a second is 8 font_run rows of work against a
; desktop that has nothing else to do. MDG_RATE ticks is ~0.5 s, which is
; faster than a reader can look from one column to the next.
;
; in:  nothing; UI task.  out: nothing (every register preserved)
; -----------------------------------------------------------------------------
mdg_tick:
    push ax
%ifdef MOU_ROUND
    call mdb_step               ; SPEC.md 9.4.6.5: EVERY pass, not every
                                ; redraw - a phase boundary read at MDG_RATE
                                ; would land up to half a second late and the
                                ; t column on the glass would not match it
%endif
    mov ax, [ticks]
    sub ax, [mdg_last]          ; wrap-safe, like every other elapsed test in
    cmp ax, MDG_RATE            ; this kernel
    jb .out
    mov ax, [ticks]
    mov [mdg_last], ax
    call mdg_show
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; mdg_tpl  - DS:SI template -> mdg_buf, space-padded to MDG_W and NUL-ended
; mdg_line - draw mdg_buf at MDG_X, BP and step BP a row
; mdg_h16 / mdg_h8 / mdg_h1 - AX / AL / AL's low nibble as hex at [di]
; All clobber only what they must; mdg_line spends none of the caller's.
; -----------------------------------------------------------------------------
mdg_tpl:
    push ax
    push cx
    push si
    push di
    mov di, mdg_buf
    mov cx, MDG_W
.copy:
    mov al, [si]
    or al, al
    jz .pad
    inc si
    mov [di], al
    inc di
    loop .copy
    jmp short .end
.pad:
    mov al, ' '                 ; ...and NOT `rep stosb`, which was the first
.pz:                            ; draft and is an ES ASSUMPTION this routine has
    mov [di], al                ; no right to make: every other store here is
    inc di                      ; DS-relative, mdg_show runs at the END of kmain
    loop .pz                    ; where ES is whatever the desktop paint left,
.end:                           ; and the two halves of one buffer must not be
    mov byte [di], 0            ; written through two different segments
    pop di
    pop si
    pop cx
    pop ax
    ret

mdg_line:
    push ax
    push cx
    push dx
    push si
    mov si, mdg_buf
    mov cx, MDG_X
    mov dx, bp
    mov ax, (CBLACK << 8) | CWHITE
    call font_run
    add bp, MDG_ROW
    pop si
    pop dx
    pop cx
    pop ax
    ret

mdg_h16:
    push ax
    mov al, ah
    call mdg_h8
    pop ax
    add di, 2
    call mdg_h8
    sub di, 2
    ret

mdg_h8:
    push ax
    push cx
    mov cl, 4
    shr al, cl
    call mdg_h1
    pop cx
    pop ax
    push ax
    inc di
    call mdg_h1
    dec di
    pop ax
    ret

mdg_h1:
    push ax
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .store
    add al, 7                   ; 'A'..'F'
.store:
    mov [di], al
    pop ax
    ret

; =============================================================================
; SPEC.md 9.4.6.5 - THE ROUND, and the three counters it is read against
;
; The rows above answer "what did the identify window see", which is a fact
; about 1.2 s of a boot. docs/FIELD-NOTES.md 44 is a question about the rest
; of the session: a combo PS/2-or-serial mouse that is NOT found at boot and
; works perfectly when it is hot-plugged onto a port the kernel has already
; settled. Everything that differs between those two cases is something this
; kernel does to the port, so the round DOES each of them in turn and the
; counters say which one the mouse answered.
;
; It runs ONLY while [mou_seen] is 0. On a machine whose mouse works this is
; one compare per UI pass and the panel is byte for byte what it was before,
; which is what keeps MOUDIAG usable for the question it was written for.
; =============================================================================

; -----------------------------------------------------------------------------
; mdb_rxb - one received byte, from port BX (SPEC.md 9.4.6.5)
;
; Called from mou_byte's entry, so it sees EVERY byte: the ones the drain
; discards, the ones that resync, and the ones that never reach a packet. That
; is the point - rx is the one number that separates "the mouse has never said
; anything" from "the mouse talks and the kernel does not believe it", and
; every other counter in this kernel is downstream of a decision.
; ins:  AL = the byte, BX = the port row. ISR context, IF = 0
; outs: none (all registers preserved)
; -----------------------------------------------------------------------------
mdb_rxb:
    push ax
    push dx
    mov dl, [bx+mdb_l2]         ; the last four, newest first
    mov [bx+mdb_l3], dl
    mov dl, [bx+mdb_l1]
    mov [bx+mdb_l2], dl
    mov dl, [bx+mdb_l0]
    mov [bx+mdb_l1], dl
    mov [bx+mdb_l0], al
    mov dx, [bx+mdb_rx]         ; saturating, for mou_idn's reason: a stream
    cmp dx, 0xFFFF              ; must not wrap back down through a small
    je .dt                      ; plausible number
    inc dx
    mov [bx+mdb_rx], dx
.dt:
    cmp word [bx+mdb_dt], 0xFFFF ; the FIRST byte after our rising edge, and
    jne .out                     ; only that one - every raise puts it back to
    mov dx, [ticks]              ; FFFF, so it always measures the LAST edge.
    sub dx, [mdb_rt]             ; How long this mouse takes to wake up is the
                                 ; whole of why a 3 s poll interval could be
                                 ; starving it
    mov [bx+mdb_dt], dx
.out:
    pop dx
    pop ax
    ret

%ifdef MOU_ROUND
; -----------------------------------------------------------------------------
; mdb_pin - raise DTR/RTS and leave them there (SPEC.md 9.4.6.5)
;
; mou_hotplug's own raise, plus the two stamps the round needs. It goes
; through state 3 and arms the drain exactly as .low does, so a phase change
; can never strand DTR low - which is SPEC.md 9.4's trap, and the one thing
; here that would leave the user worse off than the bug.
; ins:  none.  outs: none (all registers preserved)
; -----------------------------------------------------------------------------
mdb_pin:
    push ax
    push bx
    mov ax, 0x040B
    call mou_pall
    mov bx, [ticks]
    mov [mou_hpt], bx
    mov [mdb_rt], bx
    mov [mou_dstamp], bx
    mov byte [mou_drain], 1
    mov byte [mou_hpst], 3
    mov word [mdb_dt], 0xFFFF
    mov word [mdb_dt+2], 0xFFFF
    inc word [mdb_cyc]
    call mou_newround
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mdb_step - advance the round (SPEC.md 9.4.6.5), from ui_task every pass
; ins:  none.  outs: none (all registers preserved)
; -----------------------------------------------------------------------------
mdb_step:
    push ax
    cmp byte [mdb_run], 0       ; the clock starts at the DESKTOP and not at
    jne .running                ; sched_init, for SPEC.md 9.4.8's reason: the
    mov byte [mdb_run], 1       ; boot's own tail is minutes of ticks on a slow
    mov ax, [ticks]             ; machine and would spend two phases before
    mov [mdb_pht], ax           ; the user could see one
    mov [mdb_rt], ax
    jmp short .out
.running:
    cmp byte [mou_seen], 0      ; FOUND. Freeze the round where it stands - the
    jne .out                    ; phase on the glass is then the answer, and
                                ; nothing power-cycles a working mouse behind
                                ; the user's hand
    mov ax, [ticks]
    sub ax, [mdb_pht]
    cmp ax, MDB_PHT
    jb .out
    mov al, [mdb_ph]
    inc al
    cmp al, MDB_NPH
    jae .out                    ; D is terminal: the most permissive state
    mov [mdb_ph], al            ; there is, so it is the one to be left in
    mov ax, [ticks]
    mov [mdb_pht], ax
    call mdb_enter
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; mdb_enter - do what phase [mdb_ph] means (SPEC.md 9.4.6.5)
;
; Each phase removes ONE of the things this kernel does to a port that a
; settled port does not have done to it, cumulatively:
;   A  nothing - the shipped kernel, as a baseline
;   B  the power cycle: DTR/RTS pinned high for good
;   C  ...and instead a LONG unpowered hold, for a part that needs more than
;      MOU_RSTLOW's 165 ms to see a power-on at all
;   D  pinned high AND the packet contest cut to one, which is what a settled
;      port has (SPEC.md 9.5) and the only difference B still leaves
; ins:  none.  outs: none (all registers preserved)
; -----------------------------------------------------------------------------
mdb_enter:
    push ax
    cmp byte [mdb_ph], 1
    jne .c
    mov byte [mdb_hold], 1
    call mdb_pin
    jmp short .out
.c:
    cmp byte [mdb_ph], 2
    jne .d
    mov byte [mdb_hold], 0      ; the poller runs again, but its low hold is
    mov word [mdb_low], MDB_LONG ; ~1.1 s instead of ~165 ms
    mov ax, 0x0408              ; ...and drop NOW rather than waiting out a
    call mou_pall               ; whole MOU_REPOLL to see the first one
    mov ax, [ticks]
    mov [mou_hpt], ax
    mov byte [mou_hpst], 1
    jmp short .out
.d:
    mov byte [mou_need], 1
    mov byte [mou_need+2], 1
    mov byte [mdb_hold], 1
    mov word [mdb_low], MOU_RSTLOW
    call mdb_pin
.out:
    pop ax
    ret
%endif

mdg_t1: db 'win open ....  used ....  of 0013 ticks', 0
mdg_t2: db 'row  base  idn  b0  last  idt  nd  run', 0
mdg_t3: db '  .  ....  ..  ..  ....   .    .   .', 0
mdg_t4: db 'idany .  port .  seen .  p2st ..  hpst .', 0
mdg_t5: db 'chrome ....  mouse ....  fdd ....  ticks', 0
mdg_t6: db 'fdd1 st3 .. .. st0 .. step . vrd . eqp .', 0
; SPEC.md 9.9.6's two rows. The dot columns ARE the offsets mdg_show writes at,
; so moving a label without moving its write puts a hex digit in the middle of
; a word - the two are edited together or not at all.
mdg_t7: db 'tier . aux ..  cmd ..  id ..  irq ....', 0
mdg_t8: db 'p2 .  b0 ..  ptr .  pkt ....  x ....', 0
mdg_t9: db 'sub .  pre ..  pic ..  st ..  cm1 ..', 0
mdb_t1: db 'row    rx  err msr mcr  b0 b1 b2 b3   dt', 0
mdb_t2: db '  .  ....   ..  ..  ..  .. .. .. .. ....', 0
%ifdef MOU_ROUND
mdb_t3: db 'phase .  ....  t ....  cyc ....  hold .', 0
mdb_t4: db 'A norm B hold C long D need1 - 15s each', 0
%endif
mdg_buf: times MDG_W + 1 db 0
mdg_last: dw 0                  ; [ticks] at the last redraw (mdg_tick)

; --- SPEC.md 9.4.6.5's state. Word arrays are indexed by the port ROW exactly
; as mou_idlast is, BX being 0 and 2, and the byte arrays carry four entries
; for the same reason mou_idn does
mdb_rx      dw 0, 0             ; bytes EVER received, per port, saturating
mdb_dt      dw 0xFFFF, 0xFFFF   ; ticks from our rising edge to the first byte
mdb_err     db 0,0,0,0          ; every LSR error bit ever set: 02 overrun,
                                ; 04 parity, 08 framing, 10 break
mdb_l0      db 0,0,0,0          ; the last four bytes, newest first
mdb_l1      db 0,0,0,0
mdb_l2      db 0,0,0,0
mdb_l3      db 0,0,0,0
mdb_rt      dw 0                ; [ticks] at the last DTR/RTS raise
mdb_cyc     dw 0                ; reset edges offered since boot
%ifdef MOU_ROUND
mdb_ph      db 0                ; the round's phase, 0..MDB_NPH-1
mdb_pht     dw 0                ; [ticks] it started
mdb_hold    db 0                ; DTR/RTS pinned high: mou_hotplug stands down
mdb_run     db 0                ; the round's clock has been started
mdb_low     dw MOU_RSTLOW       ; ticks mou_hotplug holds DTR/RTS LOW
mdb_pnam    db 'norm', 'hold', 'long', 'nd1 '
%endif

%endif
