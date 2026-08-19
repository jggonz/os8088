; =============================================================================
; os8088 - fprog.inc (SPEC.md 12.8)
;
; The file-operation progress widget: a document icon and a progress bar in a
; line box, at the right end of the menu bar immediately left of the clock
; cell (SPEC.md 12.1).
;
; A file operation freezes the machine, and that is by design rather than by
; accident: the disk is the UI task's (SPEC.md 18), dsk_xfer raises
; [sch_lock] across every int 13h, and the caller is a window callback that
; has held the gfx lock since before it started (SPEC.md 22). So the cursor
; stops, no window paints, and nothing moves for as long as the disk takes -
; ~24 ms per 512 bytes on the field machine (PERFORMANCE.md Part 2, Set 24 -
; 65 at Set 17, 238 before SPEC.md 18.91's AL fix), which for a
; 116KB module is several seconds of a screen that looks hung. What was
; missing was never concurrency; it was FEEDBACK. This is stepped from inside
; the transfer loop, the one piece of code still running while the rest of
; the machine is stopped.
;
; Three entry points, and the split between them is the whole performance
; argument. A gfx_* call costs ~756us of ARRIVING whatever it draws, and the
; icon is a two-pass masked blit - so fpg_begin pays for the icon, the bed
; and the box frame ONCE, and fpg_step draws at most one gfx_fill: the strip
; between the pixel column the bar had reached and the one it has reached
; now. A bar that only grows is completely described by its current extent,
; which is spl_bar's argument (SPEC.md 15.3) reused.
;
; Prefix fpg_. Every entry point is called with the gfx lock ALREADY HELD by
; an outer frame and must never take it (gfx_lock is non-reentrant and would
; deadlock a task that cannot be switched away from anyway). All state lives
; in .text with real initialisers - NOT .bss, which -f bin zeroes not at all:
; drv_boot reads the disk before any init routine of ours could run, and a
; garbage [fpg_on] would put this widget's drawing inside the splash.
;
; **THE CHROME BELONGS TO THE FREEZE AND THE TROUGH BELONGS TO THE PHASE**
; (SPEC.md 12.8.3). Everything above describes ONE data transfer, and a user
; does not do one: a document double-click is an association lookup, then a
; package load, then the application reading its file, with a mount and a
; directory walk between each pair - so the widget used to appear, vanish and
; appear again while the machine never unfroze once. The freeze is exactly one
; gfx-lock hold, so that is the widget's extent: fpg_busy arms it from inside
; the disk path, fpg_end ends only the PHASE, and gfx_unlock's fpg_finish is
; what takes it away. Nothing brackets anything, and no caller can leave it up.
; =============================================================================

section .text

FPG_ICO_W  equ 12               ; the document glyph's drawn width
FPG_ICO_H  equ 14               ; rows FPG_ICO_Y..FPG_ICO_Y+13
FPG_ICO_Y  equ 3
FPG_ISP    equ 4                ; icon -> box gap
FPG_BOX_W  equ 64               ; the line box, frame included
FPG_BOX_H  equ 9                ; rows FPG_BOX_Y..FPG_BOX_Y+8
FPG_BOX_Y  equ 5
FPG_GAP    equ 0                ; box -> clock cell gap. It was 8, and the
                                ; whole cell it cost is worth more to the
                                ; TOAST than to the eye (SPEC.md 59.9.2): the
                                ; widget's reservation is FPG_W / 8 cells off
                                ; the gap a message has to fit in, and the
                                ; clock's own field is right-aligned with
                                ; seven or so blank cells at its left end, so
                                ; the separation the eye sees is unchanged
                                ; while the strip gets a cell back
FPG_W      equ FPG_ICO_W + FPG_ISP + FPG_BOX_W + FPG_GAP    ; 80
FPG_BAR_PX equ FPG_BOX_W - 2    ; 62: the trough, in pixels
FPG_MINX   equ MENU_SYS_XR + 8  ; an origin left of this would collide with
                                ; the System menu: refuse the whole feature
FPG_CELLS  equ FPG_W / 8        ; ...and what that is in menu-bar glyph cells,
                                ; which is what menu_bar_text has to keep off
                                ; (SPEC.md 12.8.3): the widget is drawn
                                ; straight onto the segment's last cells and
                                ; is composed into no buffer at all
FPG_WARM   equ 3                ; sectors of disk work inside ONE lock hold
                                ; before the widget appears at all (12.8.3).
                                ; Its unit is the only honest one available -
                                ; a sector is ~24 ms on the field machine, so
                                ; three of them is ~72 ms - and its job is to
                                ; keep a one-sector directory probe, which is
                                ; over inside a single displayed frame, from
                                ; putting an icon up and taking it away again.
                                ; That is PERFORMANCE.md's double-draw flash
                                ; with the widget itself as the flash
FPG_UNIT   equ 512              ; the trough's internal unit, in bytes: a
FPG_USHR   equ 9                ; SECTOR, because fpg_step's caller is the
                                ; per-sector loop in dsk_xfer and that is the
                                ; one report this widget gets for free. It is
                                ; NOT part of the interface - fpg_begin and
                                ; fpg_stepb both speak bytes (SPEC.md 12.8.1)

; -----------------------------------------------------------------------------
; fpg_begin - arm the widget and draw everything expensive about it
; in:  DX:CX = BYTES this operation expects (0 refuses: nothing to report);
;      DS = KERNEL_SEG, gfx lock held by the caller
; out: nothing - all registers and the flags preserved
;
; **The scale is BYTES, and the trough's units are the widget's own business**
; (SPEC.md 12.8.1). It took a SECTOR count until the file redirector arrived
; and had none to give: a DRVC_FILE volume has no sectors at all (SPEC.md
; 62.9), FSV_STAT answers a size in DX:CX, and a cable moves bytes. Every
; producer already holds a byte count - dskw_size32 divides one to get
; [dskw_rem] - so asking for the thing they have rather than the thing this
; module used to want costs nothing and makes the widget say something on a
; volume that has no geometry. DX:CX and not DX:AX because that is the file
; API's own 32-bit count convention (SPEC.md 18.4.1) and FSV_STAT's answer
; register pair, so the two arming sites that matter most pass it unmoved.
;
; Internally it is still 512-byte units, rounded UP, saturating at 65,535 -
; a 32MB job (the volume cap, SPEC.md 18.7) lands one unit short of exact and
; the trough is 62 pixels wide, so nothing about that is visible.
;
; **It no longer refuses a nested call, and that is SPEC.md 12.8.3.** It used
; to, on the reasoning that the outer operation is the honest scale - but
; there IS no outer operation with a scale: what surrounds a package load is
; a mount and a directory walk, neither of which has a byte count to give. So
; a second begin inside one freeze is not a nesting, it is the NEXT PHASE, and
; what it re-scales is the trough alone. The chrome is already up and is not
; redrawn, which is what stops the icon flickering at every phase boundary.
;
; Refuses, silently and completely, in three cases, all of them decided when
; the CHROME goes up and none of them re-asked afterwards: the splash still
; owning the screen and the video mode (SPEC.md 15.3), a visible fullscreen
; window over the bar (SPEC.md 11.2), and an origin that would reach the
; System menu on a narrow screen.
; -----------------------------------------------------------------------------
fpg_begin:
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    mov word [fpg_total], 0     ; no scale until one is computed below, so a
                                ; refusal cannot leave the PREVIOUS phase's
                                ; divisor counting this one's sectors
    push cx                     ; BANKED across the arm: fpg_arm clobbers CX
    push dx                     ; and DX (its header says so, and on the path
    call fpg_arm                ; that puts the chrome up they come back as
    pop dx                      ; fpg_paint's and fpg_span's coordinates), and
    pop cx                      ; this pair IS the scale. `pop` leaves the
    jc .out                     ; flags alone, so CF=1 - the widget cannot be
                                ; shown here - still reads at the jump

    mov ax, cx                  ; --- DX:CX bytes -> a 16-bit unit count
    add ax, FPG_UNIT - 1        ; round UP: a 1-byte job is one unit, not none
    adc dx, 0
    mov cx, FPG_USHR
.shr:
    shr dx, 1                   ; DX:AX >>= 9 (dskw_size32's idiom - the 8086
    rcr ax, 1                   ; has no 32-bit shift)
    loop .shr
    or dx, dx
    jz .fits
    mov ax, 0xFFFF              ; past 32MB of units: saturate rather than wrap
.fits:
    or ax, ax                   ; a 0-byte job has no progress to report and
    jz .out                     ; would be fpg_step's divisor: refuse it whole
    mov [fpg_total], ax
    mov word [fpg_done], 0
    mov word [fpg_frac], FPG_UNIT - 1
                                ; SEEDED, not zeroed, and that one word is what
                                ; makes fpg_stepb round the way the total above
                                ; just did: a running `(FPG_UNIT-1 + bytes) /
                                ; FPG_UNIT` IS ceil(bytes / FPG_UNIT) at every
                                ; point, so a 167-byte file reports one unit
                                ; out of the one it was armed for instead of
                                ; zero out of one. Zeroed, every file under
                                ; 512 bytes left the bar at 0% for the whole
                                ; transfer and no arithmetic anywhere was
                                ; wrong (SPEC.md 12.8.1). fpg_step never reads
                                ; it, so the sector path is untouched
    call fpg_clear              ; the trough is this phase's now: empty it,
                                ; whether it held the previous phase's bar or
                                ; the busy hatch (SPEC.md 12.8.3)
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; fpg_busy - a file operation with no scale is still a frozen machine
;            (SPEC.md 12.8.3)
; in:  CX = sectors this call is about to move (0 = none, just re-assert);
;      DS = KERNEL_SEG, gfx lock held by the caller
; out: nothing - every register and the flags preserved
;
; THE CHOKE POINT, and it has exactly two call sites because there are exactly
; two ways a sector's worth of waiting can happen: dsk_xfer, which is every
; int 13h and every cache hit in the machine, and drv_fs_call, which is every
; verb of a redirected volume (SPEC.md 62.9). Between them they cover the
; three things SPEC.md 12.8 could not: a MOUNT, a DIRECTORY WALK, and a file
; crossing a cable one FSV_ verb at a time. No operation has to remember to
; ask, and a new one cannot forget - which is dskw_sync's argument (22.8)
; applied to the other half of the same problem.
;
; The trough shows the 50% HATCH while there is no scale, and that is the
; honest picture rather than a compromise: a bar that MOVES has to be a
; fraction of something, and neither a mount nor a walk knows its own length
; until it has finished. One gfx_fill_gray covers the whole phase however long
; it lasts, where an animated indicator would pay ~2 ms of PERFORMANCE.md
; Part 2's fixed cost per step to say the same thing. It is the desktop's own
; dither, so it survives the 1bpp reduction as a checkerboard on both mono
; adapters (SPEC.md 39.4) and reads as neither empty nor full on any of them.
;
; Unarmed it is a compare, an add and a compare, once per int 13h call
; against that call's ~24 ms. Armed with a scale it is two compares.
; -----------------------------------------------------------------------------
fpg_busy:
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    cmp byte [fpg_on], 0
    jne .armed
    mov al, [fpg_warm]          ; --- not yet: has the machine been at the disk
    or ch, ch                   ; long enough to be worth saying so?
    jnz .sat                    ; a run over 255 sectors cannot happen, but a
    add al, cl                  ; corrupt one must not wrap the counter back
    jnc .warm                   ; under the threshold
.sat:
    mov al, 0xFF
.warm:
    mov [fpg_warm], al
    cmp al, FPG_WARM
    jb .out
    call fpg_arm
    jc .out
.armed:
    cmp word [fpg_total], 0
    jne .out                    ; a phase with a scale owns the trough
    cmp byte [fpg_pat], 0
    jne .out                    ; ...and the hatch is already on it
    call fpg_hatch
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; fpg_arm - put the chrome up if it is not up already (internal)
; out: CF=0 the widget is on screen; CF=1 it cannot be, and every entry point
;      above must do nothing at all
; clobbers: AX, BX, CX, DX, SI, flags
;
; The three refusals live HERE and are asked once per freeze rather than once
; per phase, which is what makes a re-scale free. A fullscreen window that
; goes up while the widget is already armed is therefore not re-tested - the
; same exposure fpg_step has always had, and for the same reason: the test is
; a walk and the window it protects against needs a callback to take the
; screen in between two of its own file operations.
; -----------------------------------------------------------------------------
fpg_arm:
    cmp byte [fpg_on], 0
    jne .yes
    cmp byte [spl_live], 0      ; boot: the splash owns screen and mode
    jne .no
    call wm_fs_vis              ; the bar is under a fullscreen window
    jc .no

    mov bx, [vid_clk_hx]        ; the LIVE cell edge (SPEC.md 39.2), never
    sub bx, FPG_W               ; MENU_CLK_HX - that one is the VGA reference
    cmp bx, FPG_MINX            ; SIGNED, so the one test also catches a screen
    jl .no                      ; narrower than the widget wrapping the sub
    mov [fpg_x0], bx

    mov al, [menu_bovr]         ; sampled BEFORE the composition below, because
                                ; a bar that owes an overdraw repair gets it
                                ; from inside menu_draw_bar - and menu_furniture
                                ; puts the widget back ITSELF (SPEC.md 12.9),
                                ; so the paint at the bottom of this routine
                                ; would be a second one over the same pixels.
                                ; menu_draw_bar clobbers nothing, so AL still
                                ; carries the answer on the far side of it

    mov byte [fpg_on], 1        ; **ARMED BEFORE THE FIRST PIXEL, so that the
    mov word [fpg_px], 0        ; TOAST CAN GET OUT OF THE WAY** (SPEC.md
    mov byte [fpg_pat], 0       ; 59.9.2). A strip is placed where it fitted
    mov byte [menu_bdirty], 1   ; when it went up, and SPEC.md 59.9's first
    call menu_draw_bar          ; choice - the gap the menus leave - is the
                                ; same stretch of bar this widget is about to
                                ; be drawn on; measured on a 5150/CGA, Paint's
                                ; own `Saving...` lands on cells 43..53 and
                                ; the widget takes 43..53. Without this the
                                ; bed fill below simply destroys the message
                                ; and the operation runs with `Sa` and half a
                                ; document icon on the bar.
                                ;
                                ; The flag being set FIRST is also what makes
                                ; that composition keep off our cells at all:
                                ; menu_bar_text shortens the menus segment by
                                ; FPG_CELLS while it is up (SPEC.md 12.8.3),
                                ; and this is the one composition that has to
                                ; see the reservation appear.
                                ;
                                ; Re-placing it costs one composition, which
                                ; is a compare per cell plus at most one
                                ; font_run, once per FREEZE - against a disk
                                ; this is not measurable. It is done HERE and
                                ; not by asking toast_kill to retire the
                                ; message, which is what this site used to do:
                                ; a refusal loses the message for good and
                                ; silently, and the messages worth saying are
                                ; exactly the ones about the operation that is
                                ; starting.

    or al, al                   ; the bed, the icon and the box - unless the
    jnz .span                   ; repair above has just drawn them, in which
    call fpg_paint              ; case doing it again is PERFORMANCE.md Part 1's
.span:                          ; double-draw flash with the widget as the flash
    call fpg_span               ; ...and the bar has lost those cells: tell it
    call menu_inval             ; which, so fpg_finish gives back exactly them
                                ; and not the whole bar (SPEC.md 12.9)
.yes:
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; fpg_paint - the widget's own pixels, from its own state (internal)
; clobbers: AX, BX, CX, DX, SI, flags
;
; Split out of the arm because menu_furniture - the ONE path in the machine
; that erases the menu bar (SPEC.md 12.9) - has to be able to put the widget
; back, and it runs from inside menu_draw_bar. So this draws pixels and
; nothing else: no menu_draw_bar (which would recurse straight back into it),
; no menu_inval. It repaints the TROUGH from [fpg_pat]/[fpg_px] rather than
; leaving it empty, or an overdraw mid-transfer would drop the bar back to
; zero and the next fpg_step would only light the strip past where it already
; was.
; -----------------------------------------------------------------------------
fpg_paint:
    call cur_unlazy             ; spend gfx_lock's promised hide before we
                                ; draw anywhere near the arrow (SPEC.md 7.1.4)
    call fpg_span               ; --- the bed: white over whatever the bar had
    xor bx, bx                  ;     there, since the space is BORROWED and
                                ;     menu_relayout's limit is untouched
    mov dx, MBAR_H - 2          ; the black rule on the last row is not ours
    mov byte [gfx_color], CWHITE
    call gfx_fill

    mov cx, [fpg_x0]            ; --- the document icon
    mov dx, FPG_ICO_Y
    mov si, fpg_ico
    call icon_draw

    mov byte [gfx_color], CBLACK    ; --- the line box round the trough
    call fpg_boxr               ; AX = box left, CX = box right
    mov bx, FPG_BOX_Y
    mov dx, FPG_BOX_Y + FPG_BOX_H - 1
    call gfx_frame

    cmp byte [fpg_pat], 0       ; --- and whatever the trough was showing
    jne fpg_hatch
    cmp word [fpg_px], 0
    je .out
    call fpg_tin
    mov cx, ax
    add cx, [fpg_px]
    dec cx
    mov byte [gfx_color], CBLACK
    jmp gfx_fill
.out:
    ret

; -----------------------------------------------------------------------------
; fpg_hatch / fpg_clear - the trough, without a scale and with one (internal)
; clobbers: AX, BX, CX, DX, flags
; -----------------------------------------------------------------------------
fpg_hatch:
    mov byte [fpg_pat], 1
    call fpg_tin
    jmp gfx_fill_gray

fpg_clear:
    mov word [fpg_px], 0
    mov byte [fpg_pat], 0
    call fpg_tin
    mov byte [gfx_color], CWHITE
    jmp gfx_fill

; -----------------------------------------------------------------------------
; fpg_tin - the trough's interior, inside the box frame (internal)
; out: AX = x1, BX = y1, CX = x2, DX = y2, all inclusive - gfx_fill's argument
; clobbers: AX, BX, CX, DX
; -----------------------------------------------------------------------------
fpg_tin:
    call fpg_boxr
    inc ax
    dec cx
    mov bx, FPG_BOX_Y + 1
    mov dx, FPG_BOX_Y + FPG_BOX_H - 2
    ret

; -----------------------------------------------------------------------------
; fpg_step - one 512-byte unit done
; in:  nothing;  DS = KERNEL_SEG (every caller is kernel code)
; out: nothing - EVERY register and the flags preserved, because the hot
;      caller is the middle of dsk_xfer's per-sector loop, where CX is a loop
;      counter, AX/BX/SI/DI are live transfer state and CF is the routine's
;      return value in flight. spl_step's contract exactly, and for the same
;      reason.
;
; Unarmed this is a compare and a return, and it stays on the disk path for
; the life of the machine at that price (~13 us against a sector's ~24 ms).
; Armed, it draws only when the bar's pixel extent actually CHANGED - the
; trough is 62 pixels and an operation is usually more sectors than that, so
; most steps draw nothing at all.
;
; **The gate is [fpg_total] and not [fpg_on]** (SPEC.md 12.8.3): the chrome
; now outlives the phase, so "the widget is on screen" and "these sectors are
; part of what the bar is measuring" stopped being one question. A mount's
; sectors reach here with the icon up and no scale, and counting them against
; the previous phase's divisor is exactly the wrong answer. It is also the
; divisor below, so the one test still guards the div.
; -----------------------------------------------------------------------------
fpg_step:
    pushf
    cmp word [fpg_total], 0
    je .out
    push ax                     ; AX/BX/CX/DX/SI and no more: everything this
    push bx                     ; calls below - gfx_fill and fpg_boxr
    push cx                     ; - is documented "clobbers: flags", and DI/BP
    push dx                     ; /ES are dsk_xfer's live transfer state that
    push si                     ; nothing on this path touches

    mov ax, [fpg_done]
    inc ax
    cmp ax, [fpg_total]         ; the FAT flush and the directory commit are
    jbe .set                    ; outside the job's sector count, so a long
    mov ax, [fpg_total]         ; operation crowds up against 100% rather than
.set:                           ; wrapping (SPEC.md 12.8)
    mov [fpg_done], ax

    mov cx, FPG_BAR_PX
    mul cx                      ; DX:AX = done * 62, so no 16-bit overflow
    div word [fpg_total]        ; AX = filled pixels, 0..62 (total >= 1)
    cmp ax, [fpg_px]
    jbe .done                   ; nothing new to light

    mov si, ax                  ; SI = the new extent
    call fpg_boxr               ; AX = box left, CX = box right (unused here)
    mov bx, ax
    inc bx                      ; BX = the trough's first pixel column
    mov ax, bx
    add ax, [fpg_px]            ; ...the strip starts where the bar had got to
    mov cx, bx
    add cx, si
    dec cx                      ; ...and ends at the new extent
    mov [fpg_px], si
    mov bx, FPG_BOX_Y + 1
    mov dx, FPG_BOX_Y + FPG_BOX_H - 2
    mov byte [gfx_color], CBLACK
    call gfx_fill

.done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    popf
    ret

; -----------------------------------------------------------------------------
; fpg_stepb - N BYTES done (SPEC.md 12.8.1)
; in:  AX = bytes moved since the last report;  DS = KERNEL_SEG, gfx lock held
; out: nothing - every register and the flags preserved, fpg_step's contract
;
; The entry point for a producer that moves bytes rather than sectors, and it
; IS slot 0x03C8 (OSAPI_FS_PROG) - an ordinary OSAPI_SLOT cell, so a
; DRVC_FILE driver inside FSV_READ/FSV_WRITE calls exactly this routine. That
; is the half fpg_begin cannot cover: the kernel arms the widget from
; FSV_STAT's size and then has NOTHING to say until the verb returns, because
; a redirected read is one far call and not a loop the kernel is standing in.
; Only the driver knows how far the file has got.
;
; A short count is the normal case and not an error - a wire delivers what it
; delivers - so this accumulates, and the remainder below one unit is carried
; in [fpg_frac] rather than rounded away. Report the SAME bytes twice and the
; bar advances twice, which is why the contract is "since the last report"
; and not "so far".
;
; Unarmed it is a compare and a return, so a driver may call it
; unconditionally, and one that never calls it at all is merely a driver whose
; bar sits at 0 - which is what every redirected read did before this existed.
; It draws, so it demands what every drawing path here demands: the gfx lock
; ALREADY held, true by construction inside a file operation (SPEC.md 18).
;
; It steps through fpg_step rather than repeating its arithmetic - the same
; discipline dskw_rdata/wdata's shared flush follows (SPEC.md 18.4.2), and
; for the same reason: two copies of "has the extent moved" is two answers
; that can drift. The loop runs at most 128 times per 64KB report and each
; turn is a compare that usually finds nothing to draw, which against the
; wire that made this necessary (3,741 B/s, PERFORMANCE.md Set 39) is not a
; cost anybody can measure.
; -----------------------------------------------------------------------------
fpg_stepb:
    pushf
    cmp word [fpg_total], 0     ; fpg_step's gate, for fpg_step's reason
    je .out
    push ax
    push cx
    push dx
    xor dx, dx                  ; DX:AX = the carried remainder + this report,
    add ax, [fpg_frac]          ; exact in 32 bits, so nothing is lost at a
    adc dx, 0                   ; chunk boundary
    mov cx, FPG_UNIT
    div cx                      ; AX = whole units (<= 256, so the quotient
    mov [fpg_frac], dx          ; cannot overflow), DX = the new remainder
    mov cx, ax
    jcxz .done
.each:
    call fpg_step
    loop .each
.done:
    pop dx
    pop cx
    pop ax
.out:
    popf
    ret

; -----------------------------------------------------------------------------
; fpg_end - this PHASE is over; the widget is not (SPEC.md 12.8.3)
; in:  nothing;  DS = KERNEL_SEG
; out: nothing - all registers and the flags preserved (its call sites sit in
;      the dskw_*_x epilogues, between a body's CF/AX answer and the caller's)
;
; It used to be the teardown, and moving that to fpg_finish is the whole of
; how the widget stopped flickering: what follows a package load is another
; file operation two or three routines later, with the machine frozen solidly
; in between, so a teardown here put the icon away and the next fpg_begin
; brought it straight back.
;
; It does NOT draw the hatch, deliberately. The common case by far is one
; transfer and then an unlock, and drawing the busy pattern there would be a
; fill nobody ever sees - so the trough is simply left holding a full bar
; until fpg_busy's next sector says the machine is still at the disk. Doing
; nothing is also what keeps this a `mov` and a `ret` in the dskw_*_x
; epilogues, where the flags are a body's answer in flight.
; -----------------------------------------------------------------------------
fpg_end:
    mov word [fpg_total], 0     ; no scale: fpg_step and fpg_stepb stop, and
    ret                         ; fpg_busy may hatch the trough again

; -----------------------------------------------------------------------------
; fpg_finish - disarm, and give the bar back
; in:  nothing;  DS = KERNEL_SEG, gfx lock held by the caller
; out: nothing - all registers and the flags preserved
;
; **gfx_unlock is its ONE caller, and that is the whole safety argument**
; (SPEC.md 12.8.3) - dsk_batch_end's, one section over (SPEC.md 18.9.3). The
; freeze this widget reports IS a gfx-lock hold: the disk is the UI task's,
; dsk_xfer raises [sch_lock] across every int 13h, and the caller has held the
; lock since before it started. So every way out of a locked run of kernel
; code - return, failure, a question, a swap prompt - comes through here, and
; the widget cannot be left on the bar by anything: not by forgetting a
; bracket, not by returning early, not by a file operation that failed.
; Nothing anywhere has to call this and nothing may.
;
; It is a compare and a return on every unlock that saw no file work, which is
; nearly all of them.
;
; **Only our own cells go back, not the whole bar** (SPEC.md 12.9). This used
; to be menu_force + menu_draw_bar, which white-filled the bar end to end and
; put back the logo, the app name, every cell title AND the clock - so every
; file operation in the system finished with the bar flashing to restore an
; 88px strip.
;
; The split is between the rows that carry menu text and the rows that do
; not. fpg_arm already told the bar which CELLS it took, so menu_bar_text's
; opaque run is about to rewrite the text band from what the widget left to
; what the menu says, in one pass and with no white in between - erasing that
; band here would put the flash straight back. What it cannot reach is the
; icon's top and bottom and the box frame, which live ABOVE and BELOW the
; glyph rows: those are ours to clean up, and they are two thin fills.
; -----------------------------------------------------------------------------
fpg_finish:
    pushf
    push ax
    push bx
    push cx
    push dx
    mov al, [fpg_on]            ; **ONE STORE CLEARS BOTH**, because [fpg_on]
    mov word [fpg_on], 0        ; and [fpg_warm] are declared adjacent and are
                                ; always cleared together: the next freeze
                                ; warms up from nothing whether this one armed
                                ; or not, which is what makes FPG_WARM mean
                                ; "in this hold" rather than "ever". The old
                                ; value is banked first because it is the test
                                ; below - and the declaration order is load
                                ; bearing for exactly this reason
    or al, al
    je .out
    mov word [fpg_total], 0     ; ...and no phase survives the widget either

    mov byte [gfx_color], CWHITE
    call fpg_span               ; --- above the text band: the icon's head
    xor bx, bx
    mov dx, MENU_TEXT_Y - 1
    call gfx_fill

    call fpg_span               ; --- and below it: its foot and the box
    mov bx, MENU_TEXT_Y + 8
    mov dx, MBAR_H - 2
    call gfx_fill

    mov byte [menu_bdirty], 1   ; **THE TEXT BAND IS OURS TO ASK FOR.** The
                                ; box's rows MENU_TEXT_Y..+7 are erased by
                                ; nothing here - the opaque run that rewrites
                                ; them is the erase (above) - and that run
                                ; only happens if the flag is set. fpg_begin's
                                ; menu_inval sets it, but anything composing
                                ; the bar in between SPENDS it, and then the
                                ; box frame stays on the glass for good. One
                                ; store makes the erase this routine's own
                                ; rather than a debt somebody else may settle
    call menu_draw_bar          ; the text band, as the ONE run that differs -
                                ; and the menus segment is FPG_CELLS longer
                                ; again the moment [fpg_on] went to 0 above,
                                ; so this is also the composition that gives
                                ; those cells back (SPEC.md 12.8.3)
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; fpg_span - the whole widget's horizontal extent, from the armed origin
; out: AX = left x, CX = right x (both inclusive)
; clobbers: AX, CX
;
; Four sites want exactly this - the bed, the menu_inval that borrows the
; cells, and fpg_finish's two thin fills - and one of them giving a different
; answer from another is a strip of bar nothing owns.
; -----------------------------------------------------------------------------
fpg_span:
    mov ax, [fpg_x0]
    mov cx, ax
    add cx, FPG_W - 1
    ret

; -----------------------------------------------------------------------------
; fpg_boxr - the line box's horizontal extent, from the armed origin
; in:  nothing ([fpg_x0] valid)
; out: AX = box left x, CX = box right x (both inclusive)
; clobbers: AX, CX
; -----------------------------------------------------------------------------
fpg_boxr:
    mov ax, [fpg_x0]
    add ax, FPG_ICO_W + FPG_ISP
    mov cx, ax
    add cx, FPG_BOX_W - 1
    ret

; --- state (SPEC.md 12.8: .text, because -f bin zeroes no .bss and the very
;     first int 13h of the machine calls fpg_step) ----------------------------
fpg_on      db 0                ; is the CHROME up? Set for the whole freeze
                                ; (SPEC.md 12.8.3), not for one transfer.
                                ; **KEEP [fpg_warm] NEXT TO IT** - fpg_finish
                                ; clears the pair with one word store
fpg_warm    db 0                ; sectors of disk work seen in this lock hold,
                                ; against FPG_WARM; zeroed by fpg_finish, so
                                ; by gfx_unlock, so once per freeze
fpg_pat     db 0                ; 1 = the trough is carrying the busy hatch,
                                ; so fpg_busy does not draw it twice and
                                ; fpg_paint knows to put it back
fpg_x0      dw 0                ; the widget's left edge, from [vid_clk_hx]
fpg_done    dw 0                ; units stepped so far
fpg_total   dw 0                ; ...out of this many. **0 = no scale**, and
                                ; that is fpg_step's and fpg_stepb's gate as
                                ; well as the divisor they guard (12.8.3)
fpg_px      dw 0                ; pixels of trough currently lit
fpg_frac    dw 0                ; fpg_stepb's carry: bytes reported that have
                                ; not yet made a whole unit (SPEC.md 12.8.1)

; --- the document icon (icons.inc's record: db wwords, height, then height
;     mask words and height data words, bit 15 = leftmost). A page with a
;     folded top-right corner and four text rules. Pure black on white with
;     no dither anywhere, so it survives the 1bpp reduction intact
;     (SPEC.md 39.4) - a dithered outline would lose half of itself. --------
fpg_ico:
    db 1, FPG_ICO_H
    dw 0xFF80, 0xFFC0, 0xFFE0, 0xFFF0   ; mask: the silhouette, and the fold
    dw 0xFFF0, 0xFFF0, 0xFFF0, 0xFFF0   ; corner cut away from the top right
    dw 0xFFF0, 0xFFF0, 0xFFF0, 0xFFF0
    dw 0xFFF0, 0xFFF0
    dw 0xFF80, 0x80C0, 0x80A0, 0x80F0   ; data: top edge, then the fold's left
    dw 0x8010, 0xBE10, 0x8010, 0xBFD0   ; edge and diagonal, then the page
    dw 0x8010, 0xBFD0, 0x8010, 0xBE10   ; sides with four rules of "text"
    dw 0x8010, 0xFFF0
