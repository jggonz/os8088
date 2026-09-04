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
MDG_ROW     equ 10              ; 8px cell + 2
MDG_RATE    equ 9               ; ticks between redraws - ~0.5s at 18.2 Hz
MDG_W       equ 40              ; cells a line, padded - 320px, inside 640 and
                                ; inside a Hercules 720. On CGA's 200 rows the
                                ; five lines end at y 78, clear of the dock

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
mdg_buf: times MDG_W + 1 db 0
mdg_last: dw 0                  ; [ticks] at the last redraw (mdg_tick)

%endif
