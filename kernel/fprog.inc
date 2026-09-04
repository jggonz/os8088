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

; --- three facts the code below depends on and nothing used to state. They
;     cost NOTHING (%error emits no bytes) and each one fires on the edit that
;     would break it. Two of them were true by accident until this pass spent
;     them.
%if FPG_GAP != 0
  %error "fpg_boxr returns fpg_span's right edge unchanged, which is only the same number while the box's right edge IS the widget's (SPEC.md 12.8) - a non-zero gap needs the `add cx` back"
%endif
%if FPG_BAR_PX > 255
  %error "[fpg_px] is cleared one BYTE at a time (with [fpg_pat], one word store) on the strength of its high byte being a permanent zero - which is fpg_step's `div` quotient being bounded by the trough's width"
%endif
%if FPG_W % 8 != 0
  %error "FPG_CELLS truncates, so a widget that is not a whole number of glyph cells reserves fewer cells than it covers and menu_bar_text letters through it (SPEC.md 12.8.3)"
%endif

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
    CURBAR_ON                   ; SPEC.md 7.4.3: everything this module draws
                                ; is inside the menu bar's rows, so an arrow
                                ; below them is out of reach and cur_unlazy
                                ; keeps gfx_lock's promise instead of spending
                                ; it. menu_draw_bar, below, is the unclipped
                                ; composition this is really for
    push ax
    push bx
    push cx
    push dx
    push si
    xor ax, ax                  ; no scale until one is computed below, so a
    mov [fpg_total], ax         ; refusal cannot leave the PREVIOUS phase's
                                ; divisor counting this one's sectors. AX has
                                ; just been pushed and holds nothing; `mov
                                ; [mem],ax` is A3+disp16 where the immediate
                                ; store is 6, and fpg_arm clobbers AX anyway
    push cx                     ; BANKED across the arm: fpg_arm clobbers CX
    push dx                     ; and DX (its header says so, and on the path
    call fpg_arm                ; that puts the chrome up they come back as
    pop dx                      ; fpg_paint's and fpg_span's coordinates), and
    pop cx                      ; this pair IS the scale. `pop` leaves the
    jc .out                     ; flags alone, so CF=1 - the widget cannot be
                                ; shown here - still reads at the jump

    xchg ax, cx                 ; --- DX:CX bytes -> a 16-bit unit count. One
                                ; byte where `mov ax,cx` is two: AX is the
                                ; caller's and is restored at exit, and CX
                                ; receives it and is overwritten by `mov cx,
                                ; FPG_USHR` three instructions down - which
                                ; still runs CX to zero in `loop .shr`, so the
                                ; free zero `mov [fpg_done], cx` depends on is
                                ; untouched
    add ax, FPG_UNIT - 1        ; round UP: a 1-byte job is one unit, not none
    adc dx, 0
    mov cx, FPG_USHR
.shr:
    shr dx, 1                   ; DX:AX >>= 9 (dskw_size32's idiom - the 8086
    rcr ax, 1                   ; has no 32-bit shift)
    loop .shr
    neg dx                      ; CF = (DX != 0): more units than a word holds
    sbb dx, dx                  ; ...so DX is 0FFFFh when it does and 0 when
    or ax, dx                   ; not - SATURATE past 32MB of units rather than
    jz .out                     ; wrap, branchlessly, and the `or` sets ZF for
                                ; the zero-job refusal in the same instruction:
                                ; a 0-byte job has no progress to report and
                                ; would be fpg_step's divisor, so refuse it
                                ; whole. DX is dead here either way - it was
                                ; the byte count's high word, fully consumed by
                                ; the shift above, and fpg_begin restores the
                                ; caller's from its own entry `push dx`
    mov [fpg_total], ax
    mov [fpg_done], cx          ; CX IS THE EXHAUSTED LOOP COUNTER, so it is a
                                ; free zero: `.fits` is reached only by falling
                                ; out of `loop .shr` (which runs CX to 0) or
                                ; through the saturate above it, which writes
                                ; AX and not CX. Two bytes, and the dependency
                                ; is invisible without this sentence.
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
    jmp fpg_busy.out            ; THE SAME SEVEN BYTES, byte for byte: both
                                ; routines push pushf/ax/bx/cx/dx/si and both
                                ; end popf/retf, so fpg_busy's epilogue serves
                                ; this one. That makes the two push lists ONE
                                ; contract - add a push here without adding it
                                ; there and this far entry returns to nowhere.
                                ; Written `jmp`, so if fpg_busy ever moves out
                                ; of short reach this grows a byte rather than
                                ; failing to assemble.

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
    CURBAR_ON                   ; SPEC.md 7.4.3: everything this module draws
                                ; is inside the menu bar's rows, so an arrow
                                ; below them is out of reach and cur_unlazy
                                ; keeps gfx_lock's promise instead of spending
                                ; it. menu_draw_bar, below, is the unclipped
                                ; composition this is really for
    push ax
    push bx
    push cx
    push dx
    push si
    cmp byte [fpg_on], 0
    jne .armed
    cmp cx, FPG_WARM            ; --- not yet: has the machine been at the disk
    jae .now                    ; long enough to be worth saying so? A CALL
                                ; ALREADY CARRYING FPG_WARM SECTORS HAS MET THE
                                ; THRESHOLD BY ITSELF, and that subsumes the
                                ; old CH guard too: CX >= 256 is >= FPG_WARM.
                                ; Only a call BELOW the threshold needs the
                                ; counter at all, and below it CH is provably 0
    add [fpg_warm], cl          ; ...so CL is the whole of it.
                                ;
                                ; TWO BOUNDED CONSEQUENCES, both in SPEC.md
                                ; 12.8.3 now. A call of >= FPG_WARM sectors is
                                ; no longer RECORDED, so after fpg_arm refuses
                                ; one a following run of 1-sector calls has to
                                ; accumulate three of its own before the arm is
                                ; retried - ~72 ms inside a freeze that is
                                ; already seconds. And the saturate is gone
                                ; with the guard that needed it: only
                                ; sub-threshold calls feed the byte, so it
                                ; takes ~85 consecutive REFUSED ones to wrap
                                ; it, at a cost of one further sector, once.
                                ; drv_fs_call passes CX = FPG_WARM exactly
                                ; ("the threshold, already met"), so the
                                ; redirector arms on EVERY verb - which is what
                                ; its comment asks for and better than before
    cmp byte [fpg_warm], FPG_WARM
    jb .out
.now:
    call fpg_arm
    jc .out
.armed:
    cmp word [fpg_total], 0
    jne .out                    ; a phase with a scale owns the trough
    cmp byte [fpg_pat], 0
    jne .out                    ; ...and the hatch is already on it
    call fpg_hatch
.out:                           ; ...and fpg_begin's exit, too - see there
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    CURBAR_OFF
    popf
    retf

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
    cmp byte [fsx_cur], 0xFF    ; **A FOREIGN MODE IS ON THE GLASS** (SPEC.md
    jne .no                     ; 53.7): there is no menu bar to put a widget
                                ; at the end of, and everything below composes
                                ; one in the DESKTOP's geometry - so the icon,
                                ; the box and the bar all land inside whatever
                                ; the exclusive app is drawing, in its raster,
                                ; in its colours. Reported off a 5150: a game
                                ; saving a high score inside its bracket drew
                                ; the widget over its own screen, and nothing
                                ; took it away again - the app's own erase is
                                ; its dirty spans and it never knew those
                                ; pixels were dirty.
                                ;
                                ; The refusal belongs HERE and not at the call
                                ; sites because a package's file write is a
                                ; perfectly ordinary thing to do inside a
                                ; bracket (SPEC.md 53.7 forbids DRAWING slots,
                                ; not the file API), and the disk path has no
                                ; business knowing which mode is set.
    cmp byte [gfx_lock_flag], 0 ; **ANOTHER TASK OWNS THE SCREEN** - and this
    je .free                    ; routine COMPOSES THE BAR (menu_draw_bar
    mov al, [sch_cur]           ; below), which is drawing, so arming here
    cmp al, [gfx_lock_own]      ; would put two painters on one strip of glass
    jne .no                     ; (SPEC.md 59.1) without the mutex that exists
                                ; to prevent exactly that. AX IS NOT LIVE HERE
                                ; - this routine's own header says it clobbers
                                ; AX, nothing above has put anything in it, and
                                ; both callers treat it as clobbered (fpg_begin
                                ; banks CX and DX around the call precisely
                                ; because AX is not worth banking; fpg_busy has
                                ; already spent it) - so the push/pop pair that
                                ; used to bracket these two instructions
                                ; protected nothing. wm_lk has the identical
                                ; seven instructions and the identical dead
                                ; pair; they are NOT one routine, because the
                                ; two jump targets partition the three lock
                                ; states differently and no single flag can
                                ; carry both
.free:                          ; A package's file write does not hold the
                                ; mutex - it does not freeze the UI and has no
                                ; reason to - so FTPD writing a received file
                                ; armed the widget from task 0 while task 1 was
                                ; already inside menu_bar_text. That
                                ; composition LOWERED [menu_bn] by FPG_CELLS
                                ; (SPEC.md 12.8.3) under the other one's feet,
                                ; and the field caught the result: menu_bpadc
                                ; padding to a bound menu_bput had begun
                                ; dropping, spinning forever with the mutex
                                ; held. That loop now re-reads the bound and
                                ; cannot hang (SPEC.md 59.7); this is the same
                                ; defect at its cause rather than its symptom.
                                ;
                                ; Held by US is fine and is the normal case:
                                ; the freeze the widget reports IS a mutex
                                ; hold (SPEC.md 12.8.3), and fpg_finish runs
                                ; from inside gfx_unlock
    call wm_fs_vis              ; the bar is under a fullscreen window
    jc .no

    mov ax, [vid_clk_hx]        ; the LIVE cell edge (SPEC.md 39.2), never
    sub ax, FPG_W               ; MENU_CLK_HX - that one is the VGA reference.
    cmp ax, FPG_MINX            ; IN THE ACCUMULATOR, whose load and store are
    jl .no                      ; 3 bytes where BX's are 4; the `sub` and `cmp`
    mov [fpg_x0], ax            ; are 3 either way. SIGNED, so the one test
                                ; also catches a screen narrower than the
                                ; widget wrapping the sub

    xor ax, ax                  ; **ARMED BEFORE THE FIRST PIXEL, so that the
    mov [fpg_pat], ax           ; TOAST CAN GET OUT OF THE WAY** (SPEC.md
                                ; 59.9.2) - ONE WORD STORE empties the whole
                                ; trough state, [fpg_pat] and [fpg_px]'s low
                                ; byte, the high one being a permanent zero
                                ; (the %if at the top of the file holds it to
                                ; that). Building the value in AX and storing
                                ; it in the accumulator forms is shorter than
                                ; four immediates: A2/A3+disp16 are 3 where
                                ; `mov byte [mem],imm` is 5 and `mov word` 6
    inc ax
    mov [fpg_on], al            ; ...and the payload is written BEFORE the flag
    mov [menu_bdirty], al       ; that publishes it, which is the kernel's own
                                ; discipline and strictly safer than the order
                                ; this replaced. Nothing DRAWS in between, and
                                ; no cross-task reader of [fpg_on] (mou_apply,
                                ; menu_bar_text, toast_pass, fsx_mode) reads
                                ; [fpg_px] or [fpg_pat] at all
    mov al, [menu_bovr]         ; LAST OF THE FOUR, and it has to be: the block
                                ; above builds its constants in AX. Still
                                ; sampled BEFORE the composition below, which
                                ; is what matters - a bar that owes an
                                ; overdraw repair gets it from inside
                                ; menu_draw_bar, and menu_furniture puts the
                                ; widget back ITSELF (SPEC.md 12.9), so the
                                ; paint at the bottom of this routine would be
                                ; a second one over the same pixels.
                                ; menu_draw_bar clobbers nothing, so AL still
                                ; carries the answer on the far side of it
                                ;
                                ; A strip is placed where it fitted
                                ; when it went up, and SPEC.md 59.9's first

    ; --- ...AND THE ARROW COMES OFF THE GLASS (SPEC.md 12.8.4) --------------
    ; This module is the one painter in the machine that draws with the gfx
    ; lock FREE, and free is the one state the mouse ISR draws in (SPEC.md 7).
    ; The first half of the fix is one compare in mou_apply - while [fpg_on] is
    ; set the ISR defers exactly as if the lock were held - and this is the
    ; second: frozen is not gone, so an arrow already SITTING on the bar would
    ; still be drawn over, and cur_save would still go stale under it.
    ;
    ; ONE COMPARE, because everything this module draws is inside the bar's
    ; rows; conservative by a cell, because a shape's hot spot can be as low as
    ; CUR_GH (SPEC.md 7.2.2); and conservative by a display on a two-card
    ; machine, where the y is virtual (39.15) and hiding once too often costs a
    ; blink nobody is looking at.
    ;
    ; NO `cli`, and that is the ISR gate above paying for itself: [fpg_on] is
    ; already set, so no packet can move the arrow between this test and the
    ; hide - and an ISR that was mid-cur_move when we got here ran to
    ; completion before this instruction did.
    ;
    ; ABOVE menu_draw_bar, which composes the WHOLE bar and would otherwise be
    ; the first thing to draw over it.
    cmp byte [gfx_lock_flag], 0
    jne .lit                    ; somebody's hold already keeps the ISR off and
                                ; their gfx_unlock owes the show - which is
                                ; every locked file operation there is

                                ; ...and NO [cur_level] test, which is five
                                ; bytes and answers nothing: hiding an arrow
                                ; that is already hidden by refcount is one
                                ; decrement and no pixels, and fpg_finish's
                                ; show puts the count back. Every rung this
                                ; module has ever crossed was paid for in
                                ; exactly this coin (12.8.3's closing note)
    cmp word [cur_drawn_y], MBAR_H + CUR_GH
    jae .lit
    or byte [fpg_on], 2         ; **BIT 1 OF [fpg_on] IS THE DEBT**, not a byte
                                ; of its own: fpg_finish already banks this
                                ; byte to decide whether the widget was up at
                                ; all, so the debt rides home in a register it
                                ; is already carrying. Every other reader tests
                                ; it against 0 and is unaffected, and
                                ; fpg_finish's one word store still clears it
    call cursor_hide
.lit:
%ifndef NOCURDISK
    ; --- ...OR IT IS ALREADY DOWN, AND AN ARROW THAT IS DOWN CANNOT TRACK ---
    ; (SPEC.md 7.4.3.1) The case above is the pointer sitting ON the bar. This
    ; is the other one, and it is most of the machine: a handler that PAINTED
    ; before it read - the Control Panel, a file dialog, a copy repainting its
    ; list - spent gfx_lock's promised hide on the way past (SPEC.md 7.1.4),
    ; so by the time any of the disk work starts the arrow is off the glass and
    ; [cur_level] < 0 keeps the ISR off it for the rest of the hold.
    ;
    ; PUT IT BACK, AND PUT THE PROMISE BACK WITH IT. Re-arming [cur_lazy] is
    ; what makes this need no new state and no debt to settle: the arrow is up
    ; and owed a hide, which is exactly what that byte means, so every existing
    ; consumer takes over unchanged - a later unclipped painter's cur_unlazy
    ; hides it, a clipped one's cur_lazyck decides, and gfx_unlock's own tail
    ; ends it either way. Without that re-arm this would be a lit arrow with no
    ; promise outstanding, and the first painter after the transfer would draw
    ; straight through it (SPEC.md 7.1.4's permanent smear).
    ;
    ; Five conditions, and the first two are the ones that make the rest sound:
    cmp byte [fsx_task], 0xFF
    jne .curdone                ; ANY fsx bracket owns the pointer (SPEC.md
                                ; 53.6): .no above refuses a FOREIGN mode for
                                ; the widget, but a same-mode bracket (53.7 -
                                ; Paint fullscreen, Cyclone's high-score save)
                                ; keeps the desktop's geometry and the app
                                ; owns every pixel of it, so the system arrow
                                ; has no business on that raster, and an app
                                ; writing VRAM directly would never spend the
                                ; re-armed promise - the stale save-under is
                                ; restored at the bracket's end
    cmp byte [gfx_lock_flag], 0
    je .curdone                 ; HELD, and .no above already refused another
                                ; task's - so it is ours. A free lock needs
                                ; none of this: nothing spent the promise,
                                ; because there was no hold to make one
    cmp byte [cur_level], -1
    jne .curdone                ; not hidden, or hidden more than once - a
                                ; refcount we did not take is not ours to undo
    cmp word [wm_clip_n], 0
    jne .curdone                ; a region is armed, so the ISR will not move
                                ; it anyway (SPEC.md 7.4.2) and lit-but-frozen
                                ; is the thing 7.1.4.3 rejected
    cmp word [mouse_y], MBAR_H + CUR_GH
    jb .curdone                 ; [mouse_y] and NOT [cur_drawn_y]: the drawn
                                ; position is meaningless while the arrow is
                                ; hidden, and cursor_show draws at the live one
    mov byte [cur_lazy], 1
    call cursor_show
.curdone:
%endif
    ; ...AND THE BAR BELOW IS COMPOSED WITH THE MUTEX POSSIBLY UNHELD, which is
    ; not excluded by the [gfx_lock_own] test above: that test refuses a lock
    ; held by ANOTHER task at the instant of arming and says nothing about one
    ; taken an instruction later. So "two painters on one strip of glass" is
    ; structurally open here, and the argument that it is benign is worth
    ; writing down because it is not this routine's own test that makes it so:
    ;
    ;   - the painter that could genuinely race a lock-free draw is IRQ4, and
    ;     [fpg_on] - set ABOVE, before any of this - is exactly what shuts it
    ;     off (mouse.inc's `cmp byte [fpg_on], 0`, docs/FIELD-NOTES.md 34);
    ;   - the remaining case is a second TASK taking gfx_lock and painting, and
    ;     a lock-free arm is bounded by what can produce one: dsk_xfer from a
    ;     wm_wake_disp dispatch, on a task that by construction is not the UI
    ;     task. It would overlap the bar rather than corrupt shared primitive
    ;     state, and it self-heals at the next fpg_finish.
    ;
    ; That is two comments in two files that did not reference each other,
    ; under a paragraph that said the case was prevented by a test that does
    ; not prevent it. It is a bound, not an exclusion - a third painter, or a
    ; UI-task path that reaches a lock-free arm, would break it.
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
    call cur_unlazy              ; spend gfx_lock's promised hide before we
                                ; draw anywhere near the arrow (SPEC.md 7.1.4)
                                ; - which SPEC.md 7.4.3's [cur_barok], set by
                                ; every public entry of this module, turns into
                                ; "...unless the arrow is below the bar, where
                                ; nothing here can reach it"
    call fpg_span               ; --- the bed: white over whatever the bar had
    xor bx, bx                  ;     there, since the space is BORROWED and
                                ;     menu_relayout's limit is untouched
    mov dx, MBAR_H - 2          ; the black rule on the last row is not ours
    call thm_bg                
    call gfx_fill

    mov cx, ax                  ; --- the document icon. FOUR REGISTER
                                ; CONTRACTS CARRY THE NEXT NINE LINES and each
                                ; one was READ rather than assumed: gfx_fill
                                ; and gfx_frame are "clobbers: flags",
                                ; icon_draw is "all registers preserved", and
                                ; thm_bg/thm_ink are "clobbers: nothing (flags
                                ; preserved)". So AX is still fpg_span's own
                                ; left edge here, BH is still 0 from the `xor
                                ; bx,bx` above, and DH is still 0 from `mov
                                ; dx, MBAR_H - 2`. If any of those ever gains
                                ; a clobber this breaks silently
    mov dl, FPG_ICO_Y
    mov si, fpg_ico
    call icon_draw

    call thm_ink                    ; --- the line box round the trough
    call fpg_boxr               ; AX = box left, CX = box right (and it
                                ; clobbers AX and CX only, which is what lets
                                ; BH and DH survive it)
    mov bl, FPG_BOX_Y
    mov dl, FPG_BOX_Y + FPG_BOX_H - 1
    call gfx_frame

    cmp byte [fpg_pat], 0       ; --- and whatever the trough was showing
    jne fpg_hatch
    mov si, [fpg_px]            ; SI = the extent to restore...
    xor ax, ax                  ; ...and the bar is "at 0" for the duration, so
    mov [fpg_pat], ax           ; the strip fpg_lit lights below is the whole
                                ; of it. One word store again ([fpg_px]'s high
                                ; byte is a permanent zero), and fpg_lit's own
                                ; `mov [fpg_px], si` puts the extent back
    or si, si
    jnz fpg_lit
    ret

; -----------------------------------------------------------------------------
; fpg_lit - light the trough from the OLD extent ([fpg_px]) to SI, and adopt
;           SI (internal)
; in:  SI = the new extent in pixels;  [fpg_px] = the old one
; clobbers: AX, BX, CX, DX, flags
;
; ONE copy of "light the trough from one extent to another", which fpg_paint's
; restore above and fpg_step's advance below both are - they differed only in
; where the strip STARTS, and fpg_paint's start is the constant 0, which it
; now states by zeroing [fpg_px] first. fpg_paint FALLS INTO this rather than
; calling it.
;
; It is a NEW GLOBAL LABEL between fpg_paint and fpg_hatch, so it re-parents
; any NASM local that follows it - fpg_paint's `.out` was the only one and the
; bare `ret` above replaces it, so nothing is re-parented today. A `.local`
; added to fpg_paint later must go ABOVE this label.
; -----------------------------------------------------------------------------
fpg_lit:
    call fpg_tin
    mov cx, ax
    add cx, si
    dec cx
    add ax, [fpg_px]
    mov [fpg_px], si
    call thm_ink                
    jmp gfx_fill

; -----------------------------------------------------------------------------
; fpg_hatch / fpg_clear - the trough, without a scale and with one (internal)
; clobbers: AX, BX, CX, DX, flags
; -----------------------------------------------------------------------------
fpg_hatch:
    mov byte [fpg_pat], 1
    call fpg_tin
    jmp gfx_fill_gray

fpg_clear:
    xor ax, ax                  ; the pair in one word store - see fpg_arm.
    mov [fpg_pat], ax           ; AX is free: this routine's header says it
                                ; clobbers AX, its ONE caller is fpg_begin, and
                                ; `call fpg_tin` on the next line overwrites it
    call fpg_tin
    call thm_bg                
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
    CURBAR_ON                   ; SPEC.md 7.4.3: everything this module draws
                                ; is inside the menu bar's rows, so an arrow
                                ; below them is out of reach and cur_unlazy
                                ; keeps gfx_lock's promise instead of spending
                                ; it. menu_draw_bar, below, is the unclipped
                                ; composition this is really for
    cmp word [fpg_total], 0
    je .out
    push ax                     ; AX/BX/CX/DX/SI and no more: everything this
    push bx                     ; calls below - gfx_fill and fpg_boxr
    push cx                     ; - is documented "clobbers: flags", and DI/BP
    push dx                     ; /ES are dsk_xfer's live transfer state that
    push si                     ; nothing on this path touches

    mov ax, [fpg_done]          ; the FAT flush and the directory commit are
    cmp ax, [fpg_total]         ; outside the job's sector count, so a long
    jae .set                    ; operation crowds up against 100% rather than
    inc ax                      ; wrapping (SPEC.md 12.8) - ASKED BEFORE THE
    mov [fpg_done], ax          ; INCREMENT, which is the same three cases and
.set:                           ; one store fewer once the bar has clamped.
                                ; [fpg_done] <= [fpg_total] is now LOAD-BEARING
                                ; where it was merely true, and it is enforced
                                ; by this store and by fpg_begin's `mov
                                ; [fpg_done], cx` with CX = 0 - the only two
                                ; writers there are

    mov cx, FPG_BAR_PX
    mul cx                      ; DX:AX = done * 62, so no 16-bit overflow
    div word [fpg_total]        ; AX = filled pixels, 0..62 (total >= 1)
    cmp ax, [fpg_px]
    jbe .done                   ; nothing new to light

    mov si, ax                  ; SI = the new extent, and the strip from the
    call fpg_lit                ; old one to it is fpg_paint's own restore -
                                ; ONE routine, above. It is BELOW the `jbe
                                ; .done` gate, so it runs only when the bar
                                ; advances a pixel column: at most FPG_BAR_PX
                                ; times in a whole operation, always
                                ; immediately before a gfx_fill that costs
                                ; ~756 us of arriving, so the added call and
                                ; ret are ~7 us against that
.done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    CURBAR_OFF
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
    CURBAR_ON                   ; SPEC.md 7.4.3: everything this module draws
                                ; is inside the menu bar's rows, so an arrow
                                ; below them is out of reach and cur_unlazy
                                ; keeps gfx_lock's promise instead of spending
                                ; it. menu_draw_bar, below, is the unclipped
                                ; composition this is really for
    cmp word [fpg_total], 0     ; fpg_step's gate, for fpg_step's reason
    je .out
    push ax
    push cx
    push dx
    add ax, [fpg_frac]          ; DX:AX = the carried remainder + this report,
    sbb dx, dx                  ; exact in 32 bits, so nothing is lost at a
    neg dx                      ; chunk boundary: DX = -CF negated is the carry
                                ; out. `neg` writes CF, which nothing between
                                ; here and the `div` reads
    mov cx, FPG_UNIT
    div cx                      ; AX = whole units (<= 256, so the quotient
    mov [fpg_frac], dx          ; cannot overflow), DX = the new remainder
    xchg ax, cx                 ; AX is dead (popped at .done) and CX's
    jcxz .done                  ; FPG_UNIT is read by nothing below
.each:
    call fpg_step
    loop .each
.done:
    pop dx
    pop cx
    pop ax
.out:
    CURBAR_OFF
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
    retf                        ; fpg_busy may hatch the trough again

; -----------------------------------------------------------------------------
; fpg_finish - disarm, and give the bar back
; in:  nothing;  DS = KERNEL_SEG, gfx lock held by the caller
; out: nothing - all registers and the flags preserved
;
; **gfx_unlock is its ONE ROUTINE caller, and that is the whole safety
; argument** (SPEC.md 12.8.3) - dsk_batch_end's, one section over (SPEC.md
; 18.9.3). The
; freeze this widget reports IS a gfx-lock hold: the disk is the UI task's,
; dsk_xfer raises [sch_lock] across every int 13h, and the caller has held the
; lock since before it started. So every way out of a locked run of kernel
; code - return, failure, a question, a swap prompt - comes through here, and
; the widget cannot be left on the bar by anything: not by forgetting a
; bracket, not by returning early, not by a file operation that failed.
; Nothing anywhere HAS to call this.
;
; ONE OTHER SITE MAY, and it is an exception with a reason rather than a
; loophole: fsx_mode calls it before setting a foreign mode (SPEC.md 12.8.5),
; because the desktop this routine repairs is about to stop existing. It calls
; the TEARDOWN and not a state reset for the sake of bit 1 of [fpg_on] -
; clearing that byte by hand would drop the cursor debt and leave the arrow
; gone for the rest of the session.
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
    CURBAR_ON                   ; SPEC.md 7.4.3: everything this module draws
                                ; is inside the menu bar's rows, so an arrow
                                ; below them is out of reach and cur_unlazy
                                ; keeps gfx_lock's promise instead of spending
                                ; it. menu_draw_bar, below, is the unclipped
                                ; composition this is really for
    push ax
    push bx
    push cx
    push dx
    xor ax, ax                  ; **ONE INSTRUCTION BANKS AND CLEARS BOTH**,
    xchg ax, [fpg_on]           ; because [fpg_on] and [fpg_warm] are declared
                                ; adjacent and are always cleared together: the
                                ; next freeze warms up from nothing whether
                                ; this one armed or not, which is what makes
                                ; FPG_WARM mean "in this hold" rather than
                                ; "ever". The old value is what the test below
                                ; asks, and the declaration order is load
                                ; bearing for exactly this reason.
                                ;
                                ; `xchg r16,m16` is one read-modify-write, so
                                ; the window in which the mouse ISR could see
                                ; the old flag between a load and a store is
                                ; gone as well. It writes NO flag on an 8086,
                                ; so `or al,al` still decides; AH becomes the
                                ; old [fpg_warm] where it used to be the
                                ; caller's, and it is dead through `push ax`
                                ; .. `pop ax` .. `test al,2`, with the caller's
                                ; AX restored from this routine's entry push.
                                ; 26 clocks either way, one byte shorter
    or al, al
    je .out
    push ax                     ; **BANKED HERE AND NOT AT THE TEST BELOW**:
                                ; bit 1 is fpg_arm's cursor debt (SPEC.md
                                ; 12.8.4) and fpg_span answers in AX, so
                                ; everything between this and the pop destroys
                                ; it. The jump above is above the push, so the
                                ; stack balances on both exits
    xor ax, ax                  ; ...and no phase survives the widget either.
    mov [fpg_total], ax         ; BELOW the `je`, so the unarmed path - which
                                ; gfx_unlock runs on every drawing burst in the
                                ; machine - does exactly the work it did before

    call thm_bg                
    call fpg_span               ; --- above the text band: the icon's head
    xor bx, bx
    mov dx, MENU_TEXT_Y - 1
    call gfx_fill

                                ; --- and below it: its foot and the box.
                                ; AX and CX still carry the span: gfx_fill is
                                ; "clobbers: flags" and nothing between the
                                ; two fills writes either, so the second
                                ; `call fpg_span` was asking for what it was
                                ; already holding
    mov bl, MENU_TEXT_Y + 8     ; BH is still 0 from the `xor bx,bx` above and
    mov dl, MBAR_H - 2          ; DH from `mov dx, MENU_TEXT_Y - 1`, because
                                ; the gfx_fill between them is "clobbers:
                                ; flags" - the same contract the two `fpg_span`
                                ; calls that used to be here were collapsed on
    call gfx_fill

    call menu_inval             ; **THE TEXT BAND IS OURS TO ASK FOR.** The
                                ; box's rows MENU_TEXT_Y..+7 are erased by
                                ; nothing here - the opaque run that rewrites
                                ; them is the erase (above) - and that run
                                ; only happens if [menu_bdirty] is set.
                                ; fpg_begin's menu_inval sets it, but anything
                                ; composing the bar in between SPENDS it, and
                                ; then the box frame stays on the glass for
                                ; good. This makes the erase this routine's own
                                ; rather than a debt somebody else may settle.
                                ;
                                ; menu_inval rather than the store it opens
                                ; with: it is a strict SUPERSET (it also
                                ; forgets those cells in menu_bcell, which is
                                ; what the paragraph above is asking for), it
                                ; is 3 bytes where the store is 5, and AX/CX
                                ; still hold the widget's span from fpg_span -
                                ; the identical span fpg_arm already passes it
                                ; at the foot of that routine, so the range is
                                ; legal by an existing site rather than by
                                ; argument. It preserves every register
                                ; (jmp kret_dx), so the `pop ax` below still
                                ; gets fpg_arm's banked cursor debt. It runs
                                ; AFTER [fpg_on] went to 0, so the menus
                                ; segment is back to full length and nothing
                                ; is clamped away
    call menu_draw_bar          ; the text band, as the ONE run that differs -
    pop ax                      ; and the menus segment is FPG_CELLS longer
                                ; again the moment [fpg_on] went to 0 above,
                                ; so this is also the composition that gives
                                ; those cells back (SPEC.md 12.8.3)

    test al, 2                  ; ...and fpg_arm's arrow comes back (SPEC.md
    jz .out                     ; 12.8.4), LAST: the save-under has to be taken
    CURLOG 'F'
    call cursor_show            ; from finished pixels and the bar is not
                                ; finished until the line above. We are inside
                                ; gfx_unlock, which still owns the glass, and
                                ; its own cursor tail is BELOW this - so the
                                ; refcount lands where it started either way
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    CURBAR_OFF
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
    call fpg_span               ; the same RIGHT EDGE: FPG_W - 1 is 79 and so
    add ax, FPG_ICO_W + FPG_ISP ; is FPG_ICO_W + FPG_ISP + FPG_BOX_W - 1,
    ret                         ; because the box's right edge IS the widget's
                                ; while FPG_GAP is 0 - which the %if at the top
                                ; of the file now holds it to. Clobbers AX and
                                ; CX, exactly as it did

; --- state (SPEC.md 12.8: .text, because -f bin zeroes no .bss and the very
;     first int 13h of the machine calls fpg_step) ----------------------------
fpg_on      db 0                ; is the CHROME up? Set for the whole freeze
                                ; (SPEC.md 12.8.3), not for one transfer.
                                ; **BIT 1 IS fpg_arm's CURSOR DEBT** (SPEC.md
                                ; 12.8.4): the arrow was taken off the glass
                                ; because it was sitting in the bar's rows, and
                                ; fpg_finish owes a cursor_show. Every other
                                ; reader asks this byte "non-zero?", so the bit
                                ; is invisible to all of them.
                                ; **KEEP [fpg_warm] NEXT TO IT** - fpg_finish
                                ; clears the pair with one word store
fpg_warm    db 0                ; sectors of disk work seen in this lock hold,
                                ; against FPG_WARM; zeroed by fpg_finish, so
                                ; by gfx_unlock, so once per freeze
fpg_pat     db 0                ; 1 = the trough is carrying the busy hatch,
                                ; so fpg_busy does not draw it twice and
                                ; fpg_paint knows to put it back.
                                ; **KEEP [fpg_px] NEXT TO IT** - the same
                                ; adjacency contract one field along: three
                                ; sites empty the trough's whole state with
                                ; `mov [fpg_pat], ax`, AX = 0, in three bytes
fpg_px      dw 0                ; pixels of trough currently lit. Its HIGH BYTE
                                ; IS A PERMANENT ZERO and the store above
                                ; depends on it: the only non-zero writer is
                                ; fpg_step's `mov [fpg_px], si` with SI the
                                ; quotient of `done * FPG_BAR_PX / total` and
                                ; done <= total, so SI <= FPG_BAR_PX = 62. The
                                ; %if at the top of the file is what holds that
fpg_x0      dw 0                ; the widget's left edge, from [vid_clk_hx]
fpg_done    dw 0                ; units stepped so far
fpg_total   dw 0                ; ...out of this many. **0 = no scale**, and
                                ; that is fpg_step's and fpg_stepb's gate as
                                ; well as the divisor they guard (12.8.3)
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
