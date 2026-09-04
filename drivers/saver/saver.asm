; =============================================================================
; os8088 - SAVER.DRV: the animated screen saver (SPEC.md 79)
;
; Four modes - a spinning cube that bounces, a starfield, geometric shapes
; that draw and erase themselves, and sea life - plus the settings window
; behind the Control Panel's Screen Saver button.
;
; THIS IS AN OVERLAY, NOT A DRIVER (SPEC.md 79.2, 41.12, 52.11). It has a
; driver's header, a driver's org 0, a driver's three-byte dispatcher and a
; driver's one-claim load - and class DRVC_OVL, which the kernel deliberately
; does not know: no row in drv_tab, no publication slot, no Drivers-page tick,
; no SYSTEM.CFG bit and no Control Panel page of its own. XMEM.DRV is the
; precedent and, like it, OUR OWNER IS THE KERNEL: blank.inc reads this file
; when the idle period runs out and frees it when the session ends.
;
; WHY AN OVERLAY AND NOT AN ON-DEMAND MODULE (SPEC.md 2.8). A module is the
; kernel's own code cut out of the kernel binary, and its DATA has to stay in
; `.text` - which is the kernel's 64KB segment and, at the time this was
; written, a rung with 20 bytes left in it (docs/KERNEL-MEMORY.md). A screen
; saver is almost entirely data: a 256-byte sine table, four mode state
; blocks, eleven strings and two vertex lists. An overlay owns a segment, so
; every byte of that is on the floppy and none of it is in the kernel. What
; the kernel keeps is 113 bytes of `.text`, and 26 of those are the row and
; the file name.
;
; WHAT A MODE OWES (SPEC.md 79.5), and all four of these are the difference
; between a screen saver and a demo:
;
;   1. **It must not favour a part of the screen.** That is the whole feature:
;      a phosphor is burned by a bright thing that does not move. So the cube
;      bounces, the starfield's origin drifts, a shape is placed at a fresh
;      random centre every time, and a fish that swims off one edge comes back
;      at a new height on the other.
;   2. **It is priced in CALLS, not pixels** (PERFORMANCE.md): ~756us of fixed
;      cost per `gfx_*` call on the 4.77MHz 8088 this targets, so a frame is
;      budgeted at 20-50 calls and each mode says what its own frame costs.
;   3. **It erases what it drew and nothing else.** The ground is black, and
;      the only thing that ever repaints the desktop is the kernel on the way
;      out (ss_stop_x), so a mode that leaves litter leaves it for minutes.
;   4. **It reads on ONE BIT PER PIXEL** (SPEC.md 39.4). Everything below
;      picks its pens out of sv_inks, which has a colour column and a mono
;      column, because indices 1..6 all round to BLACK on a Hercules and a
;      blue starfield there is a blank screen with a bug report behind it.
;
; REGISTERS: sv_entry preserves everything but AX and the flags (SPEC.md
; 79.3), so the kernel's own glue does not have to.
; =============================================================================

%include "os88drv.inc"

SV_ABI_VER  equ 1               ; the private interface blank.inc was built
                                ; against; the kernel does not check it today,
                                ; the header carries it, and the day the verbs
                                ; change is the day it starts being read

    OS88_OVERLAY 'SAVER', SV_ABI_VER, sv_entry

; --- the verbs, MIRRORED IN kernel/blank.inc ---------------------------------
; AL on the far call into sv_entry, drv_call's own contract. There is no
; linker here, so the two spellings are two constants: tests/unit/t_mirror.py
; scans both files and is the only thing that can notice them drifting.
SSV_START   equ 0
SSV_FRAME   equ 1
SSV_STOP    equ 2
SSV_CFG     equ 3
SSV_BUSY    equ 4

; ...and the settings block the kernel hands us in SI, read through ES
; (SPEC.md 79.7). MIRRORED the same way and for the same reason.
SS_P_MODES  equ 0               ; db: bit per mode, and 0 means "just blank it"
SS_P_SECS   equ 1               ; db: seconds one mode runs
SS_P_MINS   equ 2               ; db: minutes of idle before any of it starts
SS_P_POST   equ 3               ; db: we changed the three above; the UI task
                                ; owes them a re-derive and a SYSTEM.CFG mark
SS_P_IDLE   equ 4               ; dw: the DERIVED idle period, in ticks. The
                                ; Test button writes 1 here (SPEC.md 79.7) and
                                ; the post at the end of the test is what puts
                                ; the real one back

SS_M_CUBE   equ 1
SS_M_STARS  equ 2
SS_M_SHAPES equ 4
SS_M_FISH   equ 8
SS_M_ALL    equ 15
SS_NMODE    equ 4

; --- the modes, as an index rather than a bit --------------------------------
SV_CUBE     equ 0
SV_STARS    equ 1
SV_SHAPES   equ 2
SV_FISH     equ 3
SV_NONE     equ 0FFh

; --- logical inks (rule 4 in the header) -------------------------------------
SV_GROUND   equ 0               ; what an erase writes, and it is black on
                                ; every adapter: a screen saver whose ground
                                ; is not the darkest thing available is a
                                ; screen saver that burns the phosphor evenly
SV_MAIN     equ 1
SV_DIM      equ 2
SV_HOT      equ 3

SV_FTICK    equ 1               ; ticks between frames, per mode (sv_ival)
SV_MINSEC   equ 2               ; seconds a mode runs, clamped: a zero from a
SV_MAXSEC   equ 250             ; corrupt settings file would re-pick a mode
                                ; on every frame and never draw one

SV_MINW     equ 320             ; the screen a mode assumes it has at least.
SV_MINH     equ 128             ; Every margin below is subtracted from the
                                ; live width and height, and an UNSIGNED bound
                                ; that went negative would put a shape a
                                ; thousand pixels off the glass

; =============================================================================
; sv_entry - the one door (SPEC.md 79.3)
; in:  AL = SSV_*, DS = CS, ES = KERNEL_SEG, SS = LOW_SEG
; out: CF as the verb documents; every register but AX preserved
;
; The pops are what make the ABI's promise true, and `pop` touches no flag, so
; the CF a verb answers in survives all seven of them. That is the same
; property SPEC.md 20.3's API cell is built on.
; =============================================================================
sv_entry:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call sv_disp
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

sv_disp:
    cmp al, SSV_FRAME           ; FIRST: it is the one that arrives eighteen
    je sv_do_frame              ; times a second and the other four twice a
    cmp al, SSV_START           ; session
    je sv_do_start
    cmp al, SSV_STOP
    je sv_do_stop
    cmp al, SSV_CFG
    je sv_do_cfg
    cmp al, SSV_BUSY
    je sv_do_busy
    stc                         ; a verb from a kernel we were not built with
    ret

; -----------------------------------------------------------------------------
; sv_do_busy - may our image be freed? (SPEC.md 79.8)
; out: CF = 1 = no
;
; The settings window's W_PAINT is a near pointer INTO THIS SEGMENT, so an
; image freed under it is a paint into whatever claims that memory next. The
; kernel asks; we never volunteer, because the call in which we said "I have
; finished" would be answered by freeing the code it has to return into
; (SPEC.md 52.11.7's rule, one overlay over).
; -----------------------------------------------------------------------------
sv_do_busy:
    cmp word [sv_win], 0
    je .free
    stc
    ret
.free:
    clc
    ret

; -----------------------------------------------------------------------------
; sv_do_start - a session begins (SPEC.md 79.1)
; in:  SI = the settings block's offset in KERNEL_SEG; THE GFX LOCK IS HELD
; out: CF = 0 we have the screen; CF = 1 the kernel should blank the signal
;
; **THE KERNEL HOLDS THE LOCK ACROSS THIS ONE**, which is SSV_CFG's rule and
; not SSV_FRAME's, and the reason is on the kernel's side: ss_start_x has just
; READ THIS IMAGE OFF A FLOPPY, and a file read arms the menu bar's progress
; widget (SPEC.md 12.8) with only gfx_unlock able to take it down again
; (SPEC.md 12.8.3, "nothing anywhere has to call fpg_finish and nothing may").
; So the read had to be made inside a hold.
;
; **AND THAT IS WHY THE FIRST THING HERE IS AN UNLOCK.** fpg_finish does not
; merely disarm: it PAINTS THE WIDGET'S 80-PIXEL SPAN BACK in the bar's own
; ground and re-letters the text band, and it does that at the END of the
; hold. Clear the screen inside the same hold and the restore lands on a black
; screen a moment later - measured on a 5150-class machine, a WHITE BLOCK 80
; pixels wide sat in the menu bar for the whole session, drawn over and around
; by every mode. So the hold the READ was made in is ended here, over the
; desktop that is still intact, and a fresh one is taken for the clear.
;
; The kernel released nothing and expects the lock back, which is why this is
; an unlock/lock PAIR and not a plain unlock. What it can cost is one
; scheduling window: SPEC.md 7.3 makes a task that unlocks and re-locks inside
; one slice yield to a waiter, which on the UI task is ordinary and free.
;
; It can still refuse - a machine whose adapter reports a screen we cannot
; draw on - and a refusal here is the blanker, silently.
; -----------------------------------------------------------------------------
sv_do_start:
    mov [sv_pub], si
    call OSAPI_GFX_UNLOCK       ; the READ's hold ends HERE, over a desktop
    call OSAPI_GFX_LOCK         ; that is still on the glass - see the header
    call OSAPI_VIDEO            ; AX = w, BX = h, DH = bits per pixel
    mov [sv_w], ax
    mov [sv_h], bx
    mov byte [sv_monoff], 0
    cmp dh, 1                   ; SPEC.md 39.4: one bit per pixel, so every
    jne .col                    ; ink comes out of the second column
    mov byte [sv_monoff], 4
.col:
    cmp ax, SV_MINW             ; a screen nothing can be drawn on. No adapter
    jb .no                      ; SPEC.md 39 knows is anywhere near it - the
    cmp bx, SV_MINH             ; smallest is CGA's 640x200 - and it is two
    jb .no                      ; compares that stop every `sub cx, margin`
                                ; below from going NEGATIVE and arriving at
                                ; sv_rndn as a bound of 65,000

    call OSAPI_GET_TICKS        ; the RNG is seeded from the CLOCK, so two
    call OSAPI_SRAND            ; sessions on one machine do not run the same
                                ; shapes past the same fish
    mov byte [sv_mode], SV_NONE
    call sv_pick                ; ...which clears the screen and inits a mode,
    clc                         ; under the lock the kernel is already holding
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; sv_do_stop - the session is over (SPEC.md 79.6)
;
; NOTHING IS ERASED AND NOTHING IS PUT BACK. ss_stop_x repaints the whole
; desktop the moment this returns, so an erase here would be pixels written
; twice - PERFORMANCE.md rule 2, in the one place where the second writer is
; in another module and cannot be seen from this one.
;
; What it DOES do is forget the frame on the glass, so a session that starts
; again does not try to erase a cube the desktop is now standing on.
; -----------------------------------------------------------------------------
sv_do_stop:
    mov byte [sv_mode], SV_NONE
    mov byte [sv_shown], 0
    cmp byte [sc_testing], 0    ; A TEST ENDED, so the kernel is owed the post
    je .out                     ; the Test button deliberately did not raise
    mov byte [sc_testing], 0    ; (SPEC.md 79.7): it re-derives [ss_idle] from
    mov bx, [sv_pub]            ; [ss_mins] - putting the real period back,
    or bx, bx                   ; including the 0 that means off - and marks
    jz .out                     ; SYSTEM.CFG owed for the settings Test applied
    mov byte [es:bx + SS_P_POST], 1
.out:
    clc
    ret

; -----------------------------------------------------------------------------
; sv_do_frame - one frame, IF one is due (SPEC.md 79.5)
;
; THE CLOCK IS OURS, which is why this verb exists rather than the kernel
; carrying a deadline: blk_pass calls it on every UI pass - hundreds of times
; a second - and all but one in every dozen return from the compare below.
; A mode that wants to run at half rate says so in [sv_ival] and nothing in
; the kernel has to know.
;
; The deadline is ADVANCED BY THE INTERVAL and not re-anchored to now, so a
; mode keeps its rate rather than drifting slower every frame - and it is
; re-anchored when the machine falls more than SV_LAG behind, which is
; arkanoid's rule (SPEC.md 44) and stops a long stall being repaid as a burst
; of frames nobody saw.
;
; **AND IT ANSWERS CF = 1 WHEN THE NEXT ONE IS ALREADY DUE** (SPEC.md 79.5.7),
; which is the whole of what this verb owes a kernel whose UI task BLOCKS
; (SPEC.md 8.1.2). `task_sleep(1)` parks until [ticks] reaches the value it
; holds AT THE CALL, so a frame that has already run into the next tick is
; followed by a sleep through the whole of the one after it - and a mode
; polled once a pass therefore gets 18.2/(floor(work/54.9ms) + 1) frames a
; second, 18.2 while it fits a tick and 9.1 the moment it does not, with
; nothing in between.
;
; Sea life is the one mode that sits on the boundary: a pass with one of its
; frames in it costs 50-106ms against a 54.9ms tick, depending on how many
; swimmers were born at scale 2 and how many are at a screen edge - so it
; lands either side and the mode measured 12.0 fps SWINGING between 9.2 and
; 17.8, with 37.2% of the machine halted, asleep with a frame already overdue.
; The other three sat on 18.2 throughout, which is why one mode was reported
; and not four.
;
; Nothing is done about the COST here: an expensive sea still draws at what it
; costs. What goes away is the step down to the divisor below it.
;
; The answer reaches the kernel because nothing on the way writes a flag: the
; seven pops in sv_entry (SPEC.md 79.3) and ssf_frame's retf both leave CF
; alone, which is the property SPEC.md 20.3's API cell is built on.
; -----------------------------------------------------------------------------
SV_LAG      equ 4

sv_do_frame:
    call OSAPI_GET_TICKS        ; AX = [ticks]
    mov bx, ax
    sub ax, [sv_due]            ; modular: negative until the deadline passes
    js .out
    cmp ax, SV_LAG
    ja .anchor
    mov ax, [sv_due]
    add al, [sv_ival]
    adc ah, 0
    mov [sv_due], ax
    jmp short .go
.anchor:
    mov ax, bx
    add al, [sv_ival]
    adc ah, 0
    mov [sv_due], ax
.go:
    mov ax, bx                  ; ...and has this mode had its turn?
    sub ax, [sv_mt]
    cmp ax, [sv_mdue]
    jb .draw
    call sv_lone                ; ONE mode ticked: leave it alone (79.5.3)
    jc .draw
    call OSAPI_GFX_LOCK
    call sv_pick
    call OSAPI_GFX_UNLOCK
    clc
    ret
.draw:
    call OSAPI_GFX_LOCK
    call sv_step
    call OSAPI_GFX_UNLOCK
    call OSAPI_GET_TICKS        ; ...and RE-READ the clock: the frame above is
    sub ax, [sv_due]            ; the thing that may have spent a whole tick,
    js .out                     ; so the answer is worthless taken before it
    stc                         ; the next frame is already late: SAY SO, and
    ret                         ; the UI task goes round instead of sleeping
.out:
    clc
    ret

; -----------------------------------------------------------------------------
; sv_lone - is at most ONE mode ticked? (module internal)
; in:  ES = KERNEL_SEG; out: CF = 1 = yes, so nothing may be picked
; out: all registers preserved
;
; `Change every` is how often to change to a DIFFERENT mode, and with one
; ticked there is no different one: the pick would land on the same mode and
; re-init it, which clears the screen and starts the picture again. That is a
; visible reset every thirty seconds in return for nothing, and a user who
; ticked one box asked for one thing rather than for that thing restarted.
;
; `x & (x - 1)` is zero for zero bits and for one, which is the whole test.
; Zero ticked cannot start a session at all (`ss_start_x` refuses it), so the
; two answers this folds together never both arise.
;
; POP DOES NOT WRITE FLAGS on an 8086, which is what lets the result of the
; AND survive to the caller's `jc` through two of them.
; -----------------------------------------------------------------------------
sv_lone:
    push ax
    push bx
    mov bx, [sv_pub]
    mov al, [es:bx + SS_P_MODES]
    mov ah, al
    dec ah
    and al, ah
    pop bx
    pop ax
    jnz .many
    stc
    ret
.many:
    clc
    ret

; -----------------------------------------------------------------------------
; sv_pick - choose a mode and start it (SPEC.md 79.5)
; in:  the gfx lock is HELD (or we are inside SSV_START, which takes it)
; out: nothing
;
; **IT NEVER PICKS THE ONE ALREADY RUNNING WHEN THERE IS ANOTHER**, which is
; not a nicety: with two modes ticked a fair coin repeats about half the time,
; and a "screen saver that changes every thirty seconds" that visibly does
; nothing every other change reads as broken. With ONE mode ticked it still
; answers that mode - the walk finds it - which is what the FIRST pick of a
; session needs; every pick after that is refused by sv_lone above, so a
; single-mode session is not restarted every n seconds (SPEC.md 79.5.3).
;
; The walk is a rotation and not a retry loop: start one past the current mode
; and take the first ticked one after n random steps. That terminates, which a
; "pick at random until it is different" cannot promise on a machine whose RNG
; has just been seeded with the same tick twice.
; -----------------------------------------------------------------------------
sv_pick:
    push ax
    push bx
    push cx
    push dx

    call OSAPI_RAND
    and ah, 3                   ; AH and NOT AL: the low bits of the kernel's
    mov cl, ah                  ; LCG are a counter (sv_rndn's header), and a
                                ; walk distance that cycles 4-long is a mode
                                ; ORDER that repeats every four changes
                                ; CL = how far to walk past the current mode
    inc cl                      ; ...at least one, so it moves
    mov al, [sv_mode]
    cmp al, SV_NONE
    jne .have
    xor al, al                  ; a fresh session starts the walk at mode 0
.have:
    mov ch, SS_NMODE * 4        ; THE BOUND IS cl TURNS OF THE WHEEL AND NOT
                                ; TWO, which is the difference between working
                                ; and mostly working: with ONE mode ticked the
                                ; walk passes it once per turn, so a cl of 3 or
                                ; 4 ran out of tries and fell through to the
                                ; `xor al, al` below - mode 0, the cube. Every
                                ; picture in a starfield-only session was a
                                ; cube, half the time, and with all four
                                ; ticked (the default) nothing was wrong at
                                ; all. cl is at most SS_NMODE, so SS_NMODE
                                ; turns is the exact bound
.walk:
    inc al
    cmp al, SS_NMODE
    jb .test
    xor al, al
.test:
    call sv_enabled
    jc .next
    dec cl
    jz .got
.next:
    dec ch
    jnz .walk
    xor al, al                  ; NOTHING TICKED AT ALL, which the kernel
                                ; refuses to start a session for (ss_start_x)
                                ; - so with the bound above this is genuinely
                                ; unreachable, and it answers mode 0 rather
                                ; than running off the end of the table
.got:
    mov [sv_mode], al
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov ax, [bx + sv_initp]     ; the mode's init proc...
    mov byte [sv_ival], SV_FTICK
    mov byte [sv_shown], 0
    call sv_clear
    call ax                     ; ...which may raise [sv_ival]

    call OSAPI_GET_TICKS        ; the mode's own clock starts HERE and not at
    mov [sv_mt], ax             ; the session's, or the first mode of every
    mov [sv_due], ax            ; session would be cut short by however long
                                ; the load took
    call sv_secs                ; DX = its turn, in ticks
    mov [sv_mdue], dx

    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sv_step - one frame of the running mode (module internal)
; in:  the gfx lock is HELD
; -----------------------------------------------------------------------------
sv_step:
    push ax
    push bx
    mov al, [sv_mode]
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov ax, [bx + sv_stepp]
    call ax
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sv_enabled - is mode AL ticked? (module internal)
; in:  AL = SV_*; ES = KERNEL_SEG
; out: CF = 0 yes, CF = 1 no; AL kept
; -----------------------------------------------------------------------------
sv_enabled:
    push bx
    push cx
    mov bx, [sv_pub]
    mov ch, [es:bx + SS_P_MODES]
    mov cl, al
    shr ch, cl                  ; the mode INDEX is the bit position, which is
    test ch, 1                  ; what SS_M_CUBE..SS_M_FISH being 1,2,4,8 in
    jz .no                      ; SV_CUBE..SV_FISH order buys
    pop cx
    pop bx
    clc
    ret
.no:
    pop cx
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; sv_secs - how long the current mode runs, in TICKS (module internal)
; out: DX = ticks; every other register preserved
;
; 18 ticks a second and not 18.2065, which loses about a second per minute and
; is a screen saver: the setting is "roughly this often" and the alternative
; is a multiply by 1092 and a divide by 60 to be wrong by less than nobody can
; measure. The clamp is what stops a zero - from a settings file this kernel
; did not write, or a future one - re-picking a mode on every frame.
; -----------------------------------------------------------------------------
sv_secs:
    push ax
    push bx
    mov bx, [sv_pub]
    mov al, [es:bx + SS_P_SECS]
    cmp al, SV_MINSEC
    jae .lo
    mov al, SV_MINSEC
.lo:
    cmp al, SV_MAXSEC
    jbe .hi
    mov al, SV_MAXSEC
.hi:
    mov ah, 18
    mul ah                      ; AX = ticks, at most 4,500
    mov dx, ax
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sv_clear - the whole screen to the ground ink (module internal)
; in:  the gfx lock is HELD
;
; ONE gfx_fill for the lot, which on VGA is four plane writes of a rectangle
; and on the 1bpp adapters one `rep stosb` a row - about 12ms on the field
; machine either way, twice a minute. It is the ONE place this file paints
; more than it changed, and PERFORMANCE.md rule 1's exemption applies for the
; same reason ss_stop_x's repaint does: the whole screen really did change.
; -----------------------------------------------------------------------------
sv_clear:
    push ax
    push bx
    push cx
    push dx
    mov al, SV_GROUND
    call sv_pen
    xor ax, ax
    xor bx, bx
    mov cx, [sv_w]
    dec cx
    mov dx, [sv_h]
    dec dx
    call OSAPI_GFX_FILL
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sv_ink - logical ink AL of the current mode -> AL = the COLOUR (SPEC.md 39.4)
; sv_pen - ...and set the pen to it
; in:  AL = SV_GROUND..SV_HOT
; out: sv_ink AL = a colour index; sv_pen preserves everything
;
; ONE TABLE WITH TWO COLUMNS, and the column is chosen once at SSV_START. The
; alternative - every mode testing [sv_mono] at every colour change - is the
; shape that gets a case wrong, and the case it gets wrong is invisible on the
; adapter the author is looking at. gfx_inktab (SPEC.md 39.4) rounds 1..6 to
; BLACK, so a mode that reaches for CBLUE on a Hercules draws nothing at all.
;
; THE TWO ENTRIES EXIST BECAUSE A BLIT CARRIES ITS OWN PEN. Three of the four
; modes set a pen and draw; the sea builds a 1bpp band in RAM and hands it to
; OSAPI_GFX_BLIT1, which never reads [gfx_color] - the colours are the pen
; OSAPI_GFX_BLIT1_PEN sets for that one band (SPEC.md 5.4.2.2). So the table
; read and the SET_COLOR are separable, and a second copy of the table read is
; exactly the thing this file must not grow.
; -----------------------------------------------------------------------------
sv_ink:
    push bx
    push cx
    mov bl, [sv_mode]
    mov bh, 0
    mov cl, 3
    shl bx, cl                  ; eight bytes a mode: four inks, twice
    mov cl, [sv_monoff]
    mov ch, 0
    add bx, cx
    mov ah, 0
    add bx, ax
    mov al, [bx + sv_inks]
    pop cx
    pop bx
    ret

sv_pen:
    push ax
    call sv_ink
    call OSAPI_SET_COLOR
    pop ax
    ret

; -----------------------------------------------------------------------------
; sv_rndn - in CX = a bound; out AX = 0..CX-1. clobbers AX, DX, flags
;
; **NEVER THE LOW BITS.** `osapi_rand` is the classic LCG - seed*25173 + 13849
; - and both constants are ODD, so bit 0 of its output does not vary at all:
; it FLIPS on every call, forever. Bit 1 has period 4, bit 2 period 8. Every
; low bit of an LCG is a counter wearing a disguise.
;
; The first draft took the REMAINDER - `AX mod CX` - so a bound of 2 WAS bit
; 0, and the swimmers' heading read bit 0 of `OSAPI_RAND` directly. What that
; produces is not a wrong ratio, which is why it survived review: it is a
; SERIAL CORRELATION, and the marginal split stays 50/50. Measured, by forcing
; 1200 births and reading the arrays back out of the overlay:
;
;   heading  57 runs of 1200 births, against ~600 for a fair coin
;   kind     57       - and the SAME 57, in lockstep with the heading
;   size     57       - and the same again
;
; So for thirty-odd births in a row every swimmer was the same kind, at the
; same size, going the same way; then all three flipped together. From a chair
; that is "the fish only ever come from one side", which is how it was
; reported, and no amount of watching one screenful would show otherwise.
;
; So the bound is applied by MULTIPLYING and keeping the high word, which is
; driven by the top bits - the ones with the full period. The same 1200 births
; then give 570, 562 and 613 runs, independently. It is also shorter and
; faster than the divide it replaces: `mul` is at worst 133 clocks against
; `div`'s 162, and the bound no longer has to be range-checked to keep the
; quotient inside AX.
;
; A caller that wants a single random BIT should not come here for a bound of
; 2 either; it should test AH, or the sign, of `OSAPI_RAND` directly. Three
; do, and each says so.
; -----------------------------------------------------------------------------
sv_rndn:
    call OSAPI_RAND
    mul cx                      ; DX:AX = the word * the bound...
    mov ax, dx                  ; ...so the HIGH word is 0..CX-1
    ret

; -----------------------------------------------------------------------------
; sv_sin / sv_cos - AL = an angle, 256 to the turn -> AL = the value, +-127
; clobbers: AL, flags (BX is banked)
; -----------------------------------------------------------------------------
sv_sin:
    push bx
    mov bl, al
    mov bh, 0
    mov al, [bx + sv_sintab]
    pop bx
    ret

sv_cos:
    add al, 64
    jmp short sv_sin

; -----------------------------------------------------------------------------
; sv_mulsh - AL = (AL * BL) >> 7, signed (module internal)
; clobbers: AX, flags
;
; The projection's inner step, wire.asm's wr_sh7 (SPEC.md 78.2): `sar ax,7` is
; 8 + 4*7 = 36 clocks on an 8088 and one doubling plus a byte move is five,
; because the wanted bits land in AH.
; -----------------------------------------------------------------------------
sv_mulsh:
    imul bl                     ; AX = the signed product, |AX| <= 16,129
    add ax, ax
    mov al, ah
    ret

; -----------------------------------------------------------------------------
; sv_rotset - stage the four sines a 3D mode rotates through
; sv_rot3   - one model vertex through both rotations (SPEC.md 78.2)
; sv_sh7    - AL = DI >> 7, signed
;
; sv_rotset: AL = the angle about y, AH = the angle about x. Clobbers AX.
; sv_rot3:   SI -> three SIGNED BYTES (x, y, z) -> [sv_rx], [sv_ry].
;            Clobbers AX, BL, DI, flags.
;
; **|x|, |y|, |z| MUST BE 64 OR LESS**, and that is arithmetic rather than
; taste. `sv_sh7` shifts a sum of two products of a coordinate with a +-127
; sine, so it needs |DI| < 16384. The second rotation's inputs are y and the
; FIRST rotation's z', and a rotation preserves length: |z'| <= sqrt(x^2+z^2),
; which for a figure of radius r is r. So the worst sum is 127r + 127r = 254r,
; and 254 * 64 = 16,256. A radius of 65 overflows, silently, into a figure
; that turns inside out at one attitude in four.
;
; z is computed and then thrown away: an orthographic projection does not want
; it, and a depth sort is not what a wireframe is. It WAS an output for one
; round - the cube took a perspective divide off it, to give the eye something
; to tell near from far with - and it came back out when the composite (SPEC.md
; 79.5.6) fixed what that depth cue had actually been aimed at (79.5.5).
;
; Two modes rotate - the cube (SPEC.md 78.2) and the geometry (79.5) - so this
; is here rather than in either of them, and neither owns the four sines.
; -----------------------------------------------------------------------------
sv_rotset:
    push ax
    mov al, ah                  ; the angle about x first, so AH is spent
    push ax
    call sv_sin
    mov [sv_rsb], al
    pop ax
    call sv_cos
    mov [sv_rcb], al
    pop ax
    push ax
    call sv_sin
    mov [sv_rsa], al
    pop ax
    call sv_cos
    mov [sv_rca], al
    ret

sv_rot3:
    mov al, [si]                ; x' = (x*cosA - z*sinA) >> 7
    imul byte [sv_rca]
    mov di, ax
    mov al, [si+2]
    imul byte [sv_rsa]
    sub di, ax
    call sv_sh7
    mov [sv_rx], al

    mov al, [si]                ; z' = (x*sinA + z*cosA) >> 7
    imul byte [sv_rsa]
    mov di, ax
    mov al, [si+2]
    imul byte [sv_rca]
    add di, ax
    call sv_sh7
    mov bl, al

    mov al, [si+1]              ; y' = (y*cosB - z'*sinB) >> 7
    imul byte [sv_rcb]
    mov di, ax
    mov al, bl
    imul byte [sv_rsb]
    sub di, ax
    call sv_sh7
    mov [sv_ry], al
    ret

; `sar di,7` is 36 clocks on an 8088 and this is five (SPEC.md 78.2).
sv_sh7:
    mov ax, di
    add ax, ax
    mov al, ah
    ret

sv_rsa:     db 0                ; the four sines, staged once a figure or once
sv_rca:     db 0                ; a frame by whichever mode is running
sv_rsb:     db 0
sv_rcb:     db 0
sv_rx:      db 0                ; sv_rot3's two outputs
sv_ry:      db 0

%include "svcube.inc"           ; the four modes (SPEC.md 79.5)
%include "svstars.inc"
%include "svshape.inc"
%include "svfish.inc"
; The two shared SOURCE includes (SPEC.md 20.5.1). They come BEFORE svcfg.inc
; and not at the end of the file, which is what that file's own header asks
; for: the rule there is about a PACKAGE, whose header and OS88_ICON16 block
; sit at fixed offsets that emitted code would push apart (SPEC.md 20.2), and
; an overlay has neither. What decides it here is that svcfg.inc RESERVES
; `times OS88LINE_SZ` of state, and a `times` count has to be known when the
; line is assembled rather than in a later pass.
%include "os88ui.inc"           ; the standard button, glyph and press latch
%include "os88line.inc"         ; ...and the one-line field the two numbers use

%include "svcfg.inc"            ; ...and the settings window (SPEC.md 79.7)

; =============================================================================
; Tables and state. AN OVERLAY HAS NO BSS (drivers/os88drv.inc): zeroed data
; is written as `db 0` and ships on the floppy, which is what buys a load path
; with exactly one heap claim in it, made at the size the directory entry
; already reported.
; =============================================================================

sv_initp:   dw sv_cube_init, sv_star_init, sv_shape_init, sv_fish_init
sv_stepp:   dw sv_cube_step, sv_star_step, sv_shape_step, sv_fish_step

; --- the pens, four logical inks a mode, colour column then mono column ------
; The mono column may use only 0, 7..11, 13 (the dither) and 12, 14, 15
; (white): gfx_inktab reduces everything else to black (SPEC.md 39.4), so a
; row that reads well on VGA can be a blank screen on a 5151.
sv_inks:
    db CBLACK, CLCYAN,   CBLUE,     CWHITE      ; cube: a lit edge and a dim one
    db CBLACK, CWHITE,   CLGRAY,    CWHITE
    db CBLACK, CWHITE,   CLGRAY,    CYELLOW     ; stars: far ones grey, near white
    db CBLACK, CWHITE,   CWHITE,    CWHITE      ; ...but NOT grey on 1bpp: see
                                                ; svstars.inc's header, a
                                                ; dithered one-pixel star is
                                                ; invisible half the time
    db CBLACK, CLMAGENTA,CLGREEN,   CYELLOW     ; shapes: three inks in rotation
    db CBLACK, CWHITE,   CWHITE,    CWHITE      ; ...but NOT grey on 1bpp, for
                                                ; the starfield's reason: a
                                                ; dithered line is a dashed
                                                ; one, and a figure with an
                                                ; interior is where that stops
                                                ; reading as depth and starts
                                                ; reading as noise
    db CBLACK, CYELLOW,  CLCYAN,    CWHITE      ; sea: fish, jellyfish, bubbles
    db CBLACK, CWHITE,   CLGRAY,    CWHITE

; --- sin(2*pi*i/256) * 127, SHARED WITH WIREFRAME ----------------------------
; apps/wire/wiresin.inc is a generated constant table (SPEC.md 78.2) and this
; is the second reader of it. Included rather than copied: it is 256 bytes of
; numbers nobody can proof-read, and a second copy is a second thing that can
; be regenerated at a different amplitude.
sv_sintab:
%include "wiresin.inc"

sv_pub:     dw 0                ; the settings block, in KERNEL_SEG
sv_w:       dw 0                ; the screen this machine actually has
sv_h:       dw 0
sv_monoff:  db 0                ; 0 or 4: sv_inks' column (SPEC.md 39.4)
sv_mode:    db SV_NONE
sv_shown:   db 0                ; is there a previous frame to erase?
sv_ival:    db SV_FTICK         ; ticks between frames, the mode's to raise
sv_due:     dw 0                ; ...and the next one's deadline
sv_mt:      dw 0                ; when this mode started
sv_mdue:    dw 0                ; ...and how long its turn is, in ticks

    OS88_DRV_END
