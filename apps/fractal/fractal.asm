; =============================================================================
; os8088 - apps/fractal/fractal.asm
;
; Fractal, the sixth shipped package: five escape-time fractals rendered in
; Q4.12 fixed point by a BACKGROUND WORKER TASK (SPEC.md 20.6), while the
; desktop stays fully live. It is the first client of OSAPI_TASK_SPAWN /
; OSAPI_TASK_ALIVE and exists to demonstrate them: a frame is tens of
; millions of cycles - a minute or two on a real 8088 - and the whole point
; is that the clock keeps ticking, windows drag and Minesweeper plays while
; it lands band by band.
;
; Numerics are pinned by the design's reference model (Q4.12, 1.0 = 4096):
;
;   * ONE iteration core, frac_iter, with three flag bits: FF_ABS (Burning
;     Ship), FF_NEG (Tricorn), FF_JUL (Julia seeding). Five fractals, one
;     loop body.
;   * The escape test ORDER is load-bearing. |zx| and |zy| are guarded
;     against 8191 BEFORE either square is formed: |z| >= 2 means z has
;     genuinely escaped, so the picture is identical to a wide-arithmetic
;     radius-2 test, and the guard is what keeps x2, y2 and x2+y2 (max
;     32758) inside a signed word. At a guard of 8192 the sum reaches 32768,
;     the sign bit flips and a runaway point reads as interior - a black
;     speck in the outer field. Do not change 8191.
;   * qmul truncates TOWARD ZERO (the +4095 bias on a negative product),
;     not toward -infinity. That makes qmul(-a,b) == -qmul(a,b) exact, which
;     is what keeps the conjugate symmetry exact; a plain arithmetic shift
;     breaks it by 1 ulp per iteration.
;   * frac_clamp bounds |cx|,|cy| <= 14336 (3.5) over the WHOLE view, not
;     just the centre. It is the only thing between the user and a signed
;     word wrapping inside the core, so it runs unconditionally after every
;     recentre, zoom and type change.
;   * Zoom is a SHIFT COUNT (step = step0 >> z), so there is no division
;     anywhere per frame except the single step0 = span / cw. ZMAX is 4,
;     measured: at z=5 the step hits the format's 1-ulp floor and z=6 is
;     indistinguishable from z=5 - a menu item that lies.
;
; The worker (fr_worker) is spawned from a W_PAINT, never from the entry
; proc: the loader publishes the instance only after the entry returns, so a
; spawn there is refused by contract. Every restart (fr_kick -> fr_hire)
; retries until one is granted, because a full task table is a normal,
; TRANSIENT outcome - close a Timer and the next repaint gets the worker.
; Until then the canvas carries a notice and nothing renders: with no frame
; buffer, a fallback that renders under the caller's lock would either be
; erased by the next repaint before it finished a second band, or hold the
; lock for a whole frame. It computes one scanline
; into fr_line with NO lock held (tens of millions of cycles), then takes
; the lock for a short run-coalesced emit and releases it - rule 3 of
; SPEC.md 20.6, and the reason the GUI stays responsive.
;
; THERE IS NO FRAME BUFFER, AND THERE IS A RESTORE CACHE. The cache holds
; EVERY emitted row - all three passes - as (colour, last column) words in
; emission order, in a HEAP CLAIM (SPEC.md 50.3) rather than in bss. It held
; pass 0 alone until this branch, for a reason that expired: image + bss had
; to fit a 7,168-byte slice of the 19,968-byte package pool, and that pool is
; retired - a package's region is an ordinary claim now (SPEC.md 20.1). Pass 0
; alone meant the cache STOPPED GROWING at 25%, so every later repaint threw
; away three quarters of a frame that had already been computed and the
; percentage fell back to 25 and climbed again. Measured with a C model of
; this core: a whole frame is 15,366 bytes worst case over the five types,
; five zooms and four palettes at their default centres (pass 0 alone was
; 3,856), so the 16KB tier holds any of them whole.
;
; So a W_PAINT - window moved, uncovered, repainted - restarts nothing and
; RECOMPUTES nothing. fr_redraw replays the pass-0 prefix inline, because
; that is the coarse image and it is owed within the paint, and then hands
; the worker the rest to replay a row at a time. That split is the whole
; performance argument: a row is ~40 runs and a drawing call costs ~756us on
; a 4.77MHz 8088 (PERFORMANCE.md Part 2), so replaying a finished 170-row
; frame in one lock hold would be seconds of frozen desktop, while replaying
; it the way it was rendered - lock, one row, unlock, yield - costs the same
; drawing and freezes nothing. It is still ~12x faster than recomputing those
; rows, which is half a second EACH.
;
; fr_prog therefore counts rows COMPUTED, never rows replayed, and a repaint
; does not touch it: that is what the percentage is a percentage of, and it
; is why a redraw no longer moves it. A view change still restarts, because
; fr_kick is the single invalidation point and every type/palette/centre/zoom
; change funnels through it. Overflow degrades exactly as it always did: the
; cache stops growing, the rows it holds still replay, the rest is recomputed
; - never corrupted - and fr_prog drops to what the cache can restore, which
; is the honest number because the remainder is about to be computed again.
; A refused claim degrades one rung further, to the pre-cache behaviour of a
; repaint being a restart, and is retried on every kick.
;
; On a 1bpp adapter (SPEC.md 39.4) the entry proc defaults to the Contour
; palette, whose 48 entries use only the white and dither classes in runs of
; four: twelve legible contour bands, and the interior is then the ONLY
; black region on the screen.
;
; Window procs run with the gfx lock held and preserve all registers.
; BP is used as a plain value register only - never dereferenced (SS != DS).
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'FRACTAL', fr_entry, 1, OS88_STACK_192
                                ; THE WORKER'S STACK, declared
                                ; rather than defaulted (SPEC.md 8.7):
                                ; measured +60, static 48;
                                ; the larger of the two wins
                                ; over the 64-byte interrupt floor
                                ; that is 124, and 192 gives 1.55x

; --- embedded 16x16 icon (SPEC.md 20.2, flags bit 0) ---------------------------
; The Mandelbrot silhouette: the cardioid body on the right, the period-2
; bulb bumping out on the left at rows 6..9, and the antenna spiking to the
; left edge on the centre rows. The mask is the silhouette dilated 1px
; (8-connected) so the glyph sits on a clean white underlay.
;
;   data                          mask
;   ................              .......#######..
;   ........#####...              ......########..
;   .......#######..              .....##########.
;   ......#########.              .....##########.
;   ......#########.              .....##########.
;   ......#########.              .###############
;   ..#############.              ################
;   ################              ################
;   ################              ################
;   ..#############.              ################
;   ......#########.              .###############
;   ......#########.              .....##########.
;   ......#########.              .....##########.
;   .......#######..              .....##########.
;   ........#####...              ......########..
;   ................              .......#######..
    OS88_ICON16
    dw 0x01FC                       ; 16 mask rows (white underlay)
    dw 0x03FE
    dw 0x07FF
    dw 0x07FF
    dw 0x07FF
    dw 0x7FFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0x7FFF
    dw 0x07FF
    dw 0x07FF
    dw 0x07FF
    dw 0x03FE
    dw 0x01FC
    dw 0x0000                       ; 16 data rows (black pixels)
    dw 0x00F8
    dw 0x01FC
    dw 0x03FE
    dw 0x03FE
    dw 0x03FE
    dw 0x3FFE
    dw 0xFFFF
    dw 0xFFFF
    dw 0x3FFE
    dw 0x03FE
    dw 0x03FE
    dw 0x03FE
    dw 0x01FC
    dw 0x00F8
    dw 0x0000
    OS88_ICON16_END

; --- the fixed-point format and the iteration core -----------------------------
FR_ONE      equ 4096                ; Q4.12: 1.0
FR_GUARD    equ 8191                ; |zx|,|zy| magnitude guard (see the header)
FR_FOUR     equ 16384               ; escape threshold for x2 + y2 (4.0)
FR_CLAMP    equ 14336               ; |cx|,|cy| ceiling over the whole view (3.5)
FR_CAP      equ 48                  ; iteration cap; palette index is then the
                                    ; escape count directly (0..47)
FR_ZMAX     equ 4                   ; deepest useful zoom in Q4.12 (measured)
FR_CYCK     equ 8                   ; the cycle check's reference is replaced
                                    ; every FR_CYCK iterations (SPEC.md 40.7).
                                    ; A POWER OF TWO, because the test is
                                    ; `test di, FR_CYCK-1` and one AND is what
                                    ; keeps the whole scheme affordable. Eight
                                    ; is measured: 4 wins more on the Rabbit
                                    ; and the elephant and loses more
                                    ; everywhere else, 16 and 32 give up more
                                    ; than they save

; fr_inset's two shapes (SPEC.md 40.5). Every bound here is a MEASURED extent
; of the algebraic test rather than a convenient round number: the main
; cardioid lies inside Re -0.75..0.375 and |Im| <= 3*sqrt(3)/8 = 0.6495, the
; period-2 bulb inside Re -1.25..-0.75 and |Im| <= 0.25, and tools/frref.py
; sweeps the plane to say so. They are what makes the test nearly free on a
; view that holds neither shape - |cy| alone rejects both in two instructions.
FR_QTR      equ 1024                ; 0.25, the cardioid's cusp
FR_SYMAX    equ 2661                ; ceil(0.6495 * 4096): above BOTH shapes
FR_CDXHI    equ 512                 ; dx = cx - 0.25 at the cardioid's Re max
FR_CDXLO    equ -4096               ; ...and at its Re min
FR_BULBR    equ 1024                ; the bulb's box: |cx + 1| and |cy| < 0.25
FR_BULBR2   equ 256                 ; ...and its radius squared, 1/16
FR_INMARG   equ 16                  ; ulps of Q4.12 held back before claiming a
                                    ; point. NOT load-bearing - the exhaustive
                                    ; sweep passes at zero - but it is what
                                    ; keeps that true if qmul's rounding is
                                    ; ever touched, and it costs 7% of the
                                    ; claimed area, all of it boundary annulus

FF_ABS      equ 01h                 ; Burning Ship: t = |t|
FF_NEG      equ 02h                 ; Tricorn: t2 = -t2  (conj(z))
FF_JUL      equ 04h                 ; Julia: z0 = pixel, c = the constant

; --- the fractal parameter table (stride 16: index -> ptr is one shl) ----------
FT_NAME     equ  0                  ; word: NUL name (menu item AND status)
FT_FLAG     equ  2                  ; word: FF_ABS | FF_NEG | FF_JUL
FT_JCX      equ  4                  ; word: Julia constant, Q4.12
FT_JCY      equ  6                  ; word
FT_CENX     equ  8                  ; word: default centre, Q4.12
FT_CENY     equ 10                  ; word
FT_SPAN     equ 12                  ; word: default horizontal span, Q4.12
FT_SYM      equ 14                  ; word: 0 none / 1 x-axis / 2 origin -
                                    ; declared, not yet exploited (the mirror
                                    ; is the design's phase 2)
FT_SIZE     equ 16

; --- window / content geometry -------------------------------------------------
; Frame 322x199 -> content 320x180 (SPEC.md 11: content is x+1..x+w-2,
; y+TITLE_H..y+h-2). Not WF_SIZABLE: a resize during a 60-second render is a
; needless failure mode. The paint proc still reads W_W/W_H every frame,
; because wm_fit clamps the frame on a short screen (SPEC.md 39.7) - on CGA
; the desktop is 156 rows and the content becomes 320x137.
FR_STRIP_H  equ 10                  ; status strip: content rows 0..9
FR_TXT_Y    equ 1                   ; text baseline inside the strip
; Status strip column layout, EVERY COLUMN A MULTIPLE OF 8 (SPEC.md 11.94.3).
; They were 2, 130, 170, 200, 250 - four of the five at 2 mod 8, which the
; SNAPAUDIT histogram saw as 2,323 of 2,801 sampled glyphs in bucket 2, the
; worst entry in the survey. The content origin is a multiple of 8 (11.94.1),
; so a pen's offset mod 8 IS its screen phase, and on the two mono adapters an
; aligned pen is what earns OSAPI_FONT_RUN's single-store cell path (6.1).
; They all moved LEFT except the name, because FR_X_PAL + 'Spectrum' is 312 of
; a 320px content and there is nowhere to the right to go.
FR_X_NAME   equ 8                   ; ...and the name moved right, off the
                                    ; border: 0 would abut the window frame
FR_X_ZOOM   equ 128
FR_X_ZNUM   equ 168
FR_X_PCT    equ 200                 ; the one that was already aligned
FR_X_PAL    equ 248
FR_PCT_CELLS equ 5                  ; the percentage field's WIDTH in cells:
                                    ; '100%' is four and the run is padded to
                                    ; five, so it erases whatever the last one
                                    ; wrote whether that was longer or shorter.
                                    ; Asserted against fr_numbuf at the foot of
                                    ; this file, because these bss offsets are
                                    ; hand-computed and the gap to fr_line is
                                    ; exactly six bytes

; --- the restore cache (see the file header) -----------------------------------
; ONE WORD per run: colour in bits 15..12, the run's last column in 11..0.
; A colour index is 0..15 and a column 0..319, so they do not collide and the
; pack is an OR. Runs start at column 0 and each one begins where the last
; ended, so the start column is implied and a row ends with the run whose last
; column is cw-1. The ROW is implied too - not by pass 0's 0, 4, 8 ... any
; more, but by replaying the (pass, row) state machine from (0, [fr_mrc]):
; rows are appended in the order fr_advance produces them, so stepping
; fr_stepv once per stored row names every one of them. That is why there is
; exactly one copy of that arithmetic and why fr_stepv exists - and why
; [fr_mrc] is a cache validity key beside the canvas size (SPEC.md 40.6),
; since the phase decides which row each stored one IS.
;
; It STARTS at 4KB and REGROWS, doubling whenever a row will not fit, and
; that is the shape rather than a fixed size because how much a view needs is
; a property of the PICTURE and spans a factor of two hundred. Measured with
; a C model of this core: a whole frame is 340 bytes for a deep zoom that is
; all interior, 15,366 worst case over the five types, five zooms and four
; palettes at their default centres, and 69,690 for a recentred deep zoom on
; the Burning Ship - so any fixed claim is either too small for the view that
; wanted it or held against a machine that never asked. 4KB is what the old
; bss cache held, so the first claim is never worse than what this replaced;
; doubling keeps the number of grows to three even at the ceiling, and only a
; grow that has to MOVE copies anything (SPEC.md 50.3 path 2 extends in
; place, which is what a claim with free heap above it normally gets).
;
; A grow is only taken when the heap's largest free run is at least twice the
; increment: a fractal must not be the reason the next package cannot load,
; and this is an optimisation, not the app. The ceiling is 32KB because
; fr_cn, fr_cpos and fr_cmax are WORDS - past 64KB they could not name the
; cache at all, and 32KB keeps every compare on them clear of the sign bit.
FR_CRUN        equ 2                ; bytes per cached run
FR_CACHE_KB0   equ 4                ; the first claim
FR_CACHE_MAXKB equ 32               ; ...and where doubling stops

FR_SENT      equ 0FFh               ; ends a row of fr_line for the run scans:
                                    ; no colour index is ever this
FR_BSS_TOTAL equ 464                ; see the bss layout after OS88_IMAGE_END

; --- register discipline inside this package ----------------------------------
; The CALLBACKS keep the kernel's contract (fr_paint, fr_onclick, fr_about and
; fr_reloc preserve every register, fr_oncmd clobbers AX-DI and nothing else,
; and none of them touches ES or BP without putting it back). Everything they
; call does NOT: an internal routine says what it clobbers, which is usually
; everything, and the one callback that wants a register back across one of
; them saves it there. That is the first apps size pass's main cut - it was
; ~140 bytes of push/pop pairs around routines whose every caller reloads.

; -----------------------------------------------------------------------------
; fr_entry - package entry point (SPEC.md 20.2)
; in:  DS=ES=KERNEL_SEG, IF=1, gfx lock NOT held
; out: BX = window ptr, CF clear (CF set = abort, propagated from wm_create)
;
; The loader zeroed our bss, which is type 0 / palette 0 / zoom 0 - but the
; Mandelbrot's default centre is (-0.5, 0), not the origin, so fr_defaults
; has to run. On a 1bpp adapter the palette starts at Contour instead
; (SPEC.md 39.4): it is the only one of the four whose ramp never produces a
; black-class colour, so the set reads as a solid silhouette.
;
; The worker is NOT spawned here - it cannot be. The loader publishes
; I_STATE=1 and binds wm_owner only after this returns (SPEC.md 21 step 9),
; so inst_pkg_spawn would find no instance and refuse. fr_paint does it.
;
; OSAPI_MENU_SET preserves the flags as well as the registers, so the CF = 0
; our contract owes the loader is the one wm_create's branch established.
; -----------------------------------------------------------------------------
fr_entry:
    push si
    call OSAPI_VIDEO                ; DH = bits per pixel, 4 or 1
    cmp dh, 1
    jne .colour
    mov byte [fr_pal], 3            ; Contour: the 1bpp-safe ramp (the high
.colour:                            ; byte is the loader's zero)
    call fr_defaults                ; centre + zoom for the starting type
    mov si, fr_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr, CF on table full
    jc .out                         ; no window: nothing to attach menus to
    mov [fr_win], bx
    ; OUR REGION MAY MOVE (SPEC.md 66.6.1). Here, where the window
    ; exists, and not beside any worker's declaration: a package with
    ; NO worker is the case that moves most easily, and putting it at
    ; the spawn left exactly those runs declaring nothing - measured,
    ; by the row that reads MC_RLOC back out of the kernel's own table.
    ; The proc is a bare `ret` already in this file: no segment word we
    ; hold points inside the region (the cache's is fr_reloc's business).
    OS88_REGION_MOVABLE fr_noreloc
    mov si, fr_menus
    call OSAPI_MENU_SET             ; BX = the window, SI = our set
    mov si, fr_about                ; ...and 'About Fractal' above the Close
    call OSAPI_ABOUT_SET            ; the kernel already puts in our pull-down
                                    ; (SPEC.md 12.2). Flags preserved, like
                                    ; menu_win_set above
.out:
    pop si                          ; POP leaves the flags alone
    ret

; -----------------------------------------------------------------------------
; fr_paint - W_PAINT: put the picture back, and spawn the worker on the
;            first call
; in:  SI = window ptr; caller holds the gfx lock; content arrives white
; out: nothing; preserves all registers
;
; A paint means the content was erased - dragged, uncovered, or repainted by
; wm_paint_all. The view state survives it; what used to die with it was the
; work. fr_redraw replays the pass-0 cache instead and resumes refining.
;
; The worker is claimed inside fr_kick / fr_redraw (fr_hire), not here, so
; that a paint, a click and every menu command all retry it.
; -----------------------------------------------------------------------------
fr_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [fr_win], si
    call fr_redraw                  ; ...which finds the content origin itself
    cmp byte [fr_abon], 0           ; ...and the About card LAST, over the
    je fr_pop7                      ; canvas it is opaque about (SPEC.md 20.5.1)
    mov bx, [fr_win]
    mov si, fr_ablines
    call os88ui_about_d             ; _d: this paint's region is already armed
fr_pop7:                            ; the shared epilogue: fr_onclick jumps here
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fr_onclick - W_ONCLICK: re-centre on the clicked point, zoom one level in,
;              and restart
; in:  CX = x, DX = y (absolute screen), SI = window ptr; gfx lock held
; out: nothing; preserves all registers
;
; cen += (pixel - half) * step, then frac_clamp. |px - cw/2| <= 160 and
; step <= 64, so the product is at most 10240 and the addition cannot
; overflow before the clamp brings it back. Both axes are one loop, BX = 0
; then 2, because fr_cw/fr_ch and fr_cenx/fr_ceny are word pairs.
;
; The ZOOM goes after the recentre and not before it, and the order is the
; whole of the arithmetic: the two products above convert a PIXEL offset into
; a complex one using [fr_step], which is the step of the view the user
; actually clicked in. Zooming first would deepen the step and then measure
; the click against a view that was never on screen, putting the centre a
; factor of two away from the thing pointed at. fr_kick's fr_setup is what
; recomputes the step from the new zoom, after both.
;
; At FR_ZMAX the click still recentres and simply does not deepen - the point
; clicked is worth moving to whether or not there is a level left, and the
; strip goes on reporting the zoom it really has.
; -----------------------------------------------------------------------------
fr_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call fr_abdismiss               ; the credits are up: this click is spent
    jc fr_pop7                      ; taking them down
    push dx
    call fr_org                     ; AX = content left, DX = top; CX kept
    sub cx, ax                      ; -> content-relative
    pop ax
    sub ax, dx
    sub ax, FR_STRIP_H              ; -> canvas-relative
    js fr_pop7
    cmp ax, [fr_ch]
    jge fr_pop7
    or cx, cx
    jl fr_pop7
    cmp cx, [fr_cw]
    jge fr_pop7
    xchg di, ax                     ; DI = y, for the second turn; CX = x
    xor bx, bx
.axis:
    mov ax, [fr_cw+bx]
    shr ax, 1
    sub cx, ax                      ; CX = p - dim/2
    xchg ax, cx
    imul word [fr_step]             ; |product| <= 10240: AX is the answer
    add [fr_cenx+bx], ax
    mov cx, di
    inc bx
    inc bx
    cmp bl, 4
    jb .axis
    call fr_zoom_in                 ; ...and only now, with both products taken
    call fr_kick                    ; fr_setup re-clamps both axes
    jmp fr_pop7

; -----------------------------------------------------------------------------
; fr_oncmd - AM_ONCMD: the three menus (SPEC.md 12.2)
; in:  AL = item index, AH = menu index, SI = the owning window, BX = the set;
;      gfx lock held, UI task
; out: nothing; clobbers AX, BX, CX, DX, SI, DI
;
; This is the demonstration: every handler is a couple of stores plus
; fr_kick. It does NOT draw the picture - it white-fills the canvas, resets
; the state machine and returns, and the worker fills it in. The kernel does
; not repaint after a handler returns, which fr_kick's clear + status
; satisfies. The kernel clamps both indices to the counts we declared, so no
; range check is needed - only an order matching the declaration. Type and
; palette are stored as BYTES into words whose high byte is the loader's zero
; and is never written.
; -----------------------------------------------------------------------------
fr_oncmd:
    call fr_abdismiss               ; a menu pick takes the credits down first
    or ah, ah                       ; (keeping AX and SI), and then does what
    jnz .m1                         ; it says
    mov [fr_type], al               ; --- Fractal: the five types ---
    call fr_defaults                ; a new type brings its own view
    jmp short .kick
.m1:
    dec ah
    jnz .m2
    mov [fr_pal], al                ; --- Colour: the four palettes ---
    jmp short .kick
.m2:
    or al, al                       ; --- View: Zoom In / Out / Reset / Redraw
    jnz .v1
    call fr_zoom_in
    jmp short .kick
.v1:
    dec al
    jnz .v2
    cmp [fr_z], ah                  ; AH = 0 here: Zoom Out stops at level 0
    je .kick
    dec word [fr_z]
    jmp short .kick
.v2:
    dec al
    jnz .kick                       ; item 3 = Redraw: restart, keep the view
    call fr_defaults
.kick:
    jmp fr_kick

; -----------------------------------------------------------------------------
; fr_worker - THE background task (SPEC.md 20.6)
; in:  DX = our instance index, DS = ES = CS = KERNEL_SEG, IF = 1, gfx lock
;      free. NEVER returns and never exits on its own - the only way out is
;      OSAPI_TASK_ALIVE not coming back.
;
; Outer loop, in the order the contract demands:
;   1. OSAPI_TASK_ALIVE with the lock NOT held (rule 4: it takes the lock
;      itself, and gfx_lock is not reentrant).
;   2. Consume [fr_restart]. It is set only by the UI task, under the lock,
;      as the LAST store of a fr_kick (start over) or a fr_redraw (resume from
;      the cache), and both of those have already set up every word the worker
;      reads - the view (fr_setup) and the (pass, row) to continue from. So
;      what the flag means to the worker is only "the row in hand is stale":
;      clearing it here and dropping the row is the whole response. (It used
;      to re-run fr_setup and re-zero the pass on this side as well, which was
;      the same pure function of the same words a second time.) The clear is a
;      read-and-clear XCHG, atomic against the PIT switch.
;   3. Nothing to do -> sleep. Otherwise get ONE row lock-free - out of the
;      cache if a repaint left rows there to replay, else from its mirror
;      twin, else computed - re-check the restart flag (a view change mid-row
;      makes the row stale - drop it), emit under the lock, yield. The check
;      before the lock is only the cheap early-out: the binding one is under
;      the lock, which is the only place it can be atomic against fr_kick.
;
; fr_take is where a repaint stops costing anything. A replayed row is the
; same row through the same emit path - same band, same clip, same visibility
; test - so nothing downstream knows the difference, and the loop paces it
; exactly as it paces a computed one: one row per lock hold, yield between.
;
; Everything that means "this row was consumed" - the cache append, the
; progress count, the step of (pass, row) - happens under the lock, behind the
; restart check. Outside it they race fr_redraw, which publishes a (pass, row)
; to resume from, and a stale step out here would walk straight past it -
; leaving a canvas row no pass ever paints and an fr_crow the cache can never
; match again.
; -----------------------------------------------------------------------------
fr_worker:
.loop:
    mov bx, [fr_win]
    call OSAPI_TASK_ALIVE           ; may never return; preserves everything
    xor ax, ax
    xchg ax, [fr_restart]           ; read and clear in ONE instruction
    cmp word [fr_pass], 3
    jb .work
    mov ax, 4                       ; frame complete: sleep
    call OSAPI_TASK_SLEEP
    jmp .loop
.work:
    call fr_take                    ; CF=0: a repaint left this row cached
    jnc .ready

    ; --- fr_twin (inline since the first apps size pass; SPEC.md 40.6) ---
    ; Does fr_line ALREADY hold the row about to be drawn?
    ; The mirror is bit-exact in this core because qmul truncates toward zero,
    ; so qmul(-a,b) = -qmul(a,b) holds exactly and the conjugate orbit is the
    ; negated orbit: x2, x2+y2 and the magnitude guard are all untouched by the
    ; sign of zy. Swept at stride 3 over the whole clamped plane, 45,677,682
    ; pairs, zero disagreements - and the Burning Ship, which declares FT_SYM
    ; 0, disagrees on 6.5 million of them, which is what makes that a
    ; measurement rather than a sweep that was not looking.
    ;
    ; It compares against [fr_lrow] and NOTHING else, so the failure mode is a
    ; recompute rather than a wrong row: fr_kick and a HALF-DECODED fr_take both
    ; park 0FFFFh there, and a value that is merely stale cannot match a twin
    ; the render has not reached yet. The range test is what makes that
    ; sentinel safe: 2*rc - row is -1, which IS 0FFFFh, for row 2*rc+1 - an
    ; ordinary row of the walk wherever the axis sits high enough for one - so
    ; without it the sentinel would read as a match and paint that row from
    ; whatever fr_line last held.
    mov ax, [fr_mrc]
    add ax, ax                      ; 2*rc, and ZF: rc = 0 is "no axis"
    jz .calc
    sub ax, [fr_row]                ; AX = 2*rc - row, the twin
    cmp ax, [fr_row]
    je .calc                        ; the axis row is its own twin
    cmp ax, [fr_ch]
    jae .calc                       ; a twin off the canvas is no row at all -
                                    ; unsigned, so row -1 fails here
    cmp ax, [fr_lrow]
    je .ready                       ; fr_line holds it: nothing to compute

    ; --- fr_rowcalc (inline): canvas row [fr_row] into fr_line, NO LOCK ---
    ; Per-pixel arithmetic is exactly one ADD (cx += step) plus the core call -
    ; no multiply, no divide, per the mapping cx = x0 + p*step maintained
    ; incrementally. All the loop's own state lives in memory because frac_iter
    ; clobbers every register. The escape index (0..FR_CAP) indexes fr_paltab
    ; directly: entry FR_CAP is the interior's CBLACK, so an interior pixel and
    ; an escaped one take the same two instructions and there is no branch.
.calc:
    mov ax, [fr_row]                ; cy = y0 + row*step  (<= 191*51: fits AX)
    imul word [fr_step]
    add ax, [fr_y0]
    mov [fr_pcy], ax
    mov ax, [fr_x0]
    mov [fr_pcx], ax
    mov word [fr_px], fr_line       ; a POINTER into the row, not a column
.pixel:
    mov bx, [fr_pcx]
    mov si, [fr_pcy]
    test byte [fr_flg], FF_JUL
    jnz .go                         ; Julia: z0 = the pixel, c already set
    mov [fr_cx], bx                 ; Mandelbrot-type: z0 = 0, c = the pixel
    mov [fr_cy], si
    cmp byte [fr_flg], 0            ; SPEC.md 40.5: the two interior components
    jne .go0                        ; that answer without iterating at all.
    call fr_inset                   ; THE TYPE GATE IS HERE and not inside it,
    mov ax, FR_CAP                  ; so the Burning Ship and the Tricorn do
    jc .store                       ; not pay a near call a pixel to be told no
.go0:
    xor bx, bx
    xor si, si
.go:
    call frac_iter                  ; AX = escape index, or FR_CAP = interior
.store:
    xchg ax, bx
    mov al, [fr_paltab+bx]
    mov bx, [fr_px]
    mov [bx], al
    inc bx
    mov [fr_px], bx
    mov ax, [fr_step]
    add [fr_pcx], ax
    cmp bx, [fr_pend]
    jb .pixel
    mov byte [bx], FR_SENT          ; the run scans stop here
    mov ax, [fr_row]                ; fr_line holds this row now - the twin
    mov [fr_lrow], ax               ; test above compares against it

.ready:
    cmp word [fr_restart], 0
    je fr_emit                      ; the view changed under us: drop the row
    jmp .loop

; -----------------------------------------------------------------------------
; fr_emit - consume one row: cache it, paint it, step - under ONE lock hold
; in:  fr_line (+ its sentinel), [fr_pass], [fr_row], [fr_cfrom]; lock NOT
;      held. Part of fr_worker: it ends by yielding and going round again.
;
; THE LOCK IS THIS ROUTINE'S FIRST INSTRUCTION, and fr_hire's restart
; declaration counts on it (see there).
;
; Everything that means "this row was consumed" happens here, inside one lock
; hold, behind one restart check: the cache append, the progress count and
; the step. That is the whole point. Outside the lock they race fr_redraw,
; which publishes a (pass, row) to resume from and would find a stale step
; had already gone past it - leaving a canvas row no later pass paints and an
; fr_crow the cache can never match again.
;
; Visibility is re-checked UNDER the lock, every row, and the clip region is
; armed there too (SPEC.md 11.3) - windows move and get buried while the row
; was being computed, and a fractal with one corner covered goes on
; rendering the rest instead of stopping dead, which is what the old
; wm_obscured veto did. The content origin is re-read for the same reason.
; A row that cannot be seen is still cached and still steps the state
; machine: only the painting is conditional, because the cache is exactly
; what makes uncovering the window cheap.
;
; So is the restart flag, and it has to be re-checked HERE rather than in
; the worker's loop: fr_kick runs under this same lock, so the interval a
; pre-lock test cannot cover is exactly the interval spent blocked in
; OSAPI_GFX_LOCK. A view change landing there would otherwise paint the old
; view's scanline as a band at the NEW state's row.
;
; BOTH RUN SCANS STOP AT A SENTINEL. Whoever fills fr_line (the compute loop
; above, fr_take) writes FR_SENT at fr_line + cw, and no colour index is
; FR_SENT, so the extend loop is `inc / cmp / je` with no bound test at all.
; Per pixel that is ~34 cycles on the 8088 where the column-indexed loop it
; replaced was ~105, and per run it carries four fewer memory operands (the
; run start, the band's y pair and the pen colour stay in registers across the
; two kernel calls, which preserve every register) - so the emit is cheaper
; per run AND per pixel, whatever the picture. Worst case is 320 one-pixel
; runs, typical rows are around 40, and the kernel's two calls per run
; dominate either way: that ratio is what keeps the desktop responsive.
; -----------------------------------------------------------------------------
fr_emit:
    call OSAPI_GFX_LOCK             ; what follows is fr_emit_body's old
                                    ; body, inline
    cmp word [fr_restart], 0        ; the view changed while we waited for
    jne .unlock                     ; the lock: this row is the old view's -
                                    ; do not cache it, paint it OR step past it
    cmp byte [fr_cfrom], 0
    jne .cached

    ; --- fr_cache_row (inline): a COMPUTED row joins the cache, seen or not
    ; EVERY pass is cached, and the (fr_cpass, fr_crow) frontier is what keeps
    ; the rows in step with the state machine that names them. It is stepped
    ; by fr_stepv - the same routine the render is stepped with - so the two
    ; cannot drift. [fr_c0n] tracks the byte the pass-0 prefix ends at,
    ; because that is the part fr_redraw replays inline, and [fr_c0row] the
    ; LAST pass-0 row that went into it, because one fr_stepv step off that
    ; row is where the worker resumes (SPEC.md 40.6).
    ;
    ; A row is committed whole or not at all: a partial row would desync every
    ; row after it, since the start column of each run is implied by the end
    ; of the last. [fr_cn] is not moved until the row is down, so it IS the
    ; rollback point. Out of room, the cache doubles if the heap can spare it
    ; (fr_cache_grow's old body, inlined) and the row is laid down again from
    ; [fr_cn]: re-encoding one row is a few hundred byte compares against the
    ; half-second that computed it, and it is a great deal simpler than
    ; resuming a run walk into a claim that may have moved. When the heap says
    ; no the cache simply stops growing: the frontier stays put, so every later
    ; row fails the test below and the cached prefix stays replayable.
    mov ax, [fr_cseg]
    or ax, ax
    jz .cdone                       ; the heap refused us: no cache at all
    mov es, ax
    mov ax, [fr_pass]
    cmp ax, [fr_cpass]
    jne .cdone
    mov ax, [fr_row]
    cmp ax, [fr_crow]
    jne .cdone                      ; not the row the cache is waiting for
    cld
.lay:
    mov di, [fr_cn]                 ; ES:DI = where this row goes
    mov si, fr_line
    mov dx, [fr_cmax]
    dec dx
    dec dx                          ; DX = the last offset a run may start at
    mov cl, 4
.crun:
    cmp di, dx
    ja .full
    mov ch, [si]                    ; CH = the run's colour
    mov bx, si
.cext:
    inc bx
    cmp ch, [bx]
    je .cext                        ; BX = the first byte past the run
    lea ax, [bx-fr_line-1]          ; AX = the run's last column (<= 319)
    shl ch, cl
    or ah, ch                       ; pack: colour << 12 | last column
    stosw
    mov si, bx
    cmp byte [si], FR_SENT
    jne .crun
    mov [fr_cn], di                 ; commit the whole row
    inc word [fr_cnrow]
    cmp word [fr_cpass], 0
    jne .cadv                       ; past pass 0: the inline prefix is fixed
    mov [fr_c0n], di
    mov ax, [fr_crow]               ; ...and the row it stops AT, which is what
    mov [fr_c0row], ax              ; fr_redraw steps off (SPEC.md 40.6)
.cadv:
    mov di, fr_cpass                ; step the frontier the ONE way rows step
    call fr_stepm
    jmp short .cdone
.full:
    ; fr_cache_grow (inline): double the cache, if the heap can spare it
    ; (SPEC.md 50.3). It never
    ; shrinks: the size this reached is the best guess at what the next view
    ; wants. A grow is only taken when the heap's largest free run is at least
    ; twice the increment - a fractal must not be the reason the next package
    ; cannot load. ALWAYS take the base from DX: a grow that had to move leaves
    ; the old segment pointing at memory that is no longer ours. A move copies
    ; inside mem_regrow's IF=0 window, which is affordable BECAUSE it doubles:
    ; the total copied is bounded by twice the final size.
    mov cx, [fr_ckb]
    cmp cx, FR_CACHE_MAXKB
    jae .cdone                      ; the ceiling: our offsets are words
    call OSAPI_MEM_AVAIL            ; AX = largest free run in KB (BX too)
    add cx, cx                      ; the increment is the size again, and we
    cmp ax, cx                      ; want that much left for everybody else
    jb .cdone
    xchg ax, cx                     ; AX = the new size in KB
    mov dx, [fr_cseg]
    call OSAPI_MEM_REGROW           ; out DX = the base NOW: it may have MOVED
    jc .cdone
    mov [fr_cseg], dx
    mov es, dx
    shl word [fr_ckb], 1
    shl word [fr_cmax], 1           ; 32,768 at the ceiling: still a word
    jmp .lay                        ; lay the row down again, from [fr_cn]
.cdone:
    mov ax, [fr_cn]                 ; the cursor FOLLOWS the frontier, or the
    mov [fr_cpos], ax               ; next fr_take replays the row just
    inc word [fr_prog]              ; appended onto the row after it
    jmp short .vis
.cached:
    mov ax, [fr_cnext]              ; replayed: step the cursor past it, here
    mov [fr_cpos], ax               ; and not in fr_take - fr_redraw
                                    ; republishes the cursor with the row it
                                    ; names, and a step taken outside the lock
                                    ; walks past it. fr_prog counts COMPUTED
                                    ; rows only, which is what makes a repaint
                                    ; free of it
.vis:
    cmp byte [fr_abon], 0           ; the credits are up: the UI task owns the
    jne .adv                        ; content until a click or a menu pick
                                    ; takes them down. The row was CACHED above
                                    ; whatever happens here, so fr_redraw puts
                                    ; back every band this skipped
    mov bx, [fr_win]
    call OSAPI_WM_GEOM              ; CF=1: not visible (SPEC.md 11)
    jc .adv
    call OSAPI_WM_CLIP_SET          ; how much of it shows? (SPEC.md 11.3)
    jc .adv
    call fr_org                     ; AX = content left
    xchg bp, ax
    sub bp, fr_line                 ; BP = screen x of the byte at [SI] - SI
    mov ax, [fr_pass]
    mov bx, [fr_row]
    call fr_band                    ; BX = band top, DX = bottom (screen)
    mov si, fr_line
.run:
    mov al, [si]
    mov di, si
.ext:
    inc di
    cmp al, [di]
    je .ext                         ; DI = the first byte past the run
    call OSAPI_SET_COLOR
    lea ax, [bp+si]                 ; x1
    lea cx, [bp+di-1]               ; x2
    call OSAPI_GFX_FILL             ; BX/DX: the band, unchanged across runs
    mov si, di
    cmp byte [si], FR_SENT
    jne .run
    ; fr_status_maybe (inline): the PERCENTAGE, and only when it moved - at
    ; most 100 of these per frame
    ; instead of one per row. Nothing but the percentage can change while a
    ; render runs: the type, the zoom and the palette all move through fr_kick,
    ; which redraws the whole strip itself. So this draws ONE field, as one
    ; OPAQUE run - SPEC.md 12.9's rule at the menu bar - and there is no fill
    ; and no wm_clip_test gate: font_run decides one cell at a time, so the
    ; field is never momentarily blank and a clipped caller may draw through it
    ; (SPEC.md 40.2.1).
    call fr_pctcalc
    cmp al, [fr_pct]
    je .adv
    mov [fr_pct], al
    call fr_status_pct
.adv:
    mov di, fr_pass                 ; fr_advance: the row is consumed - step
    call fr_stepm                   ; the state machine, under the same hold
    cmp ax, 3                       ; ...and if THAT was the last row, the
    jb .unlock                      ; frame is complete, so the picture stands
    call fr_promise                 ; still and can be banked (SPEC.md 40.4).
                                    ; Fires once a frame - the worker sleeps
.unlock:                            ; from here and emits nothing more
    call OSAPI_GFX_UNLOCK
    call OSAPI_TASK_YIELD
    jmp fr_worker

; -----------------------------------------------------------------------------
; fr_hire - make sure this instance owns its worker, and say so when it does
;           not (SPEC.md 20.6)
; in:  gfx lock HELD (every caller is a window callback or a menu handler),
;      [fr_win] valid, the canvas freshly cleared by fr_kick
; out: nothing; clobbers AX, BX, CX, DX, SI
;
; Called from fr_kick, so EVERY paint, click and menu command retries the
; spawn. Refusal is a normal outcome - the 12-slot task table fills with
; Timers, Bounces, tm_task and a transient SB refill task - and it is
; transient: the slot the user frees by closing a Timer has to be reachable
; without relaunching the package, so fr_spawned latches only on SUCCESS.
;
; There is no inline-render fallback, deliberately. With no frame buffer a
; W_PAINT is a restart at row 0, so a fallback that renders under the
; caller's lock either paints one band and is erased by the next repaint
; before it can paint the second, or holds the lock for a whole frame -
; minutes on a 4.77 MHz 8088 with the cursor frozen, which rule 3 of
; SPEC.md 20.6 exists to forbid. Saying so on the canvas is the honest
; degradation, and View > Redraw retries.
; -----------------------------------------------------------------------------
fr_hire:
    cmp byte [fr_spawned], 0
    jne fr_noreloc                  ; we own one already: a second
                                    ; OSAPI_TASK_SPAWN would only be refused
    mov al, 1                       ; PARK-SAFE (SPEC.md 66.5.4): nothing here
    call OSAPI_MEM_PARKSAFE         ; holds a cache-derived pointer across a
                                    ; call that can yield. The worker's ONLY
                                    ; lock site is fr_emit, which takes the
                                    ; lock first and re-reads [fr_cseg] after
                                    ; it rather than inheriting it, so the
                                    ; interval spent blocked in OSAPI_GFX_LOCK
                                    ; holds nothing at all. fr_take DOES hold
                                    ; [fr_cseg] in AX for its whole walk, and
                                    ; is lock-free, but a task pre-empted there
                                    ; is not parked: this marks the task only
                                    ; while it is descheduled INSIDE gfx_lock.
                                    ; It is a WIDENING here and not the thing
                                    ; that makes the cache movable (SPEC.md
                                    ; 66.5.7.2) - this worker sleeps between
                                    ; passes and reaches OSAPI_TASK_ALIVE
                                    ; inside INST_PARKW anyway, measured. What
                                    ; it buys is the pass where the worker
                                    ; happens to be waiting on a lock some
                                    ; long repaint is holding
    mov ax, fr_worker               ; a whole-word package address: os88pkg
    mov bx, [fr_win]                ; relocates it as class 0 (SPEC.md 20.2)
    call OSAPI_TASK_SPAWN           ; CF=1 refused, nothing was created
    jc fr_nowork                    ; the canvas would otherwise stay blank
                                    ; with no explanation at all
    inc byte [fr_spawned]
    ; ...AND THE REGION CANNOT MOVE WITHOUT THIS (SPEC.md 66.6.2): the
    ; kernel wrote our segment into this worker's frame, so a region
    ; declaration alone is INERT. It is PERMANENT and not a window
    ; around OSAPI_TASK_ALIVE - the SDK note's advice for a park-safe
    ; worker, which fails SILENTLY (REGION-SELF-COMPACT-PLAN 8.2):
    ; mem_frameless reads [inst_restart] at PLAN time, and this worker
    ; is outside its own ALIVE for essentially all of a tick, so a
    ; windowed declaration would make OSAPI_MEM_COMPACT's what-if answer no
    ; better for a heap the compactor could have emptied.
    ;
    ; SO BOTH PARK POINTS ARE ENUMERATED, Tracker's shape. We are
    ; OSAPI_MEM_PARKSAFE, so the second one is BLOCKED IN
    ; OSAPI_GFX_LOCK - never HOLDING it (66.5.4 marks the task only
    ; across the yield inside the wait path).
    ;   * ALIVE is the top of .loop, above the xchg that consumes
    ;     [fr_restart] - so a park there has not eaten the request.
    ;   * The only lock site is fr_emit's, and it is fr_emit's FIRST
    ;     instruction. Everything that means `this row was consumed` -
    ;     the cache append, the progress count, the step - is past that
    ;     lock. So a restart discards a computed fr_line and [fr_row]
    ;     has NOT stepped: the next pass recomputes the same row rather
    ;     than leaving a gap no later pass paints.
    ; A restart costs one row. The worker needs nothing re-established on
    ; the way back in, because it no longer derives any state of its own:
    ; the UI task's fr_setup is the only one, and it has always run already.
    OS88_WORKER_RESTARTABLE fr_worker
fr_noreloc:                         ; also OS88_REGION_MOVABLE's proc: a ret
    ret

; -----------------------------------------------------------------------------
; fr_nowork - "there is no worker" on the empty canvas
; in:  gfx lock held; [fr_ox]/[fr_oy]/[fr_cw]/[fr_ch] valid
; out: nothing; clobbers AX, BX, CX, DX, SI
;
; Two centred lines. Each is 32 glyphs at 8px, so 256px against the 320px
; canvas - it fits on every adapter, since the window is not WF_SIZABLE and
; only its height is ever clamped (SPEC.md 39.7). The second line is .line
; fallen into, so its fr_textxy returns to fr_nowork's caller.
; -----------------------------------------------------------------------------
fr_nowork:
    mov si, fr_s_now1               ; no pen: fr_textxy carries its own pair
    mov bx, -10
    call .line
    mov si, fr_s_now2
    mov bx, 2
.line:                              ; SI = string, BX = y offset from the
    call OSAPI_FONT_WIDTH           ; canvas centre
    mov cx, [fr_cw]
    sub cx, ax
    jg .half
    xor cx, cx                      ; wider than the canvas: flush left
.half:
    shr cx, 1
    mov dx, [fr_ch]
    shr dx, 1
    add dx, bx
    jns .y_ok
    xor dx, dx
.y_ok:
    add dx, FR_STRIP_H
    jmp fr_textxy

; -----------------------------------------------------------------------------
; fr_promise - "the picture has stopped moving" / "it is being drawn"
; in:  [fr_pass], [fr_win]; THE GFX LOCK HELD BY THE CALLER
; out: nothing; clobbers AX, BX
;
; SPEC.md 40.4, which is SPEC.md 11.96.1's promise answered per FRAME.
;
; This app is the disqualifier in its purest form while it renders - the
; worker emits a band a row and goes on doing it with the window buried,
; because fr_emit only makes the PAINTING conditional on visibility and
; caches and steps regardless (that is what makes uncovering cheap) - and it
; is the flattest window in the tree once the frame lands. Pass 3 is the
; whole distinction: at pass 3 the worker sleeps, arming no clip and touching
; no pixel until a menu command or a repaint moves it.
;
; So the promise is [fr_pass] >= 3, and the three sites below are every place
; that word changes under a lock: fr_kick resets it to 0, fr_redraw
; republishes a resume point, and fr_emit steps it - the last of which is the
; only one that can ever reach 3.
;
; NO DEPTH CLAIM (SPEC.md 11.96.17): the canvas is sixteen colours by
; construction - fr_emit sets the pen per run from the palette - so a
; two-colour claim would be a lie. It does not need one: a 322x199 window is
; 320x180 of content and 30,254 bytes at four planes, inside wm_su_kb's
; 64,512 ceiling with room over.
;
; THE LOCK IS A CONDITION, NOT POLITENESS (SPEC.md 20.6 rule 7): the clear
; frees the raise cache, so a worker calling it outside a hold could free a
; buffer the UI task is blitting out of. The one worker-side site is inside
; fr_emit's hold.
;
; The window pointer comes from [fr_win] and not from whatever BX holds -
; SPEC.md 11.96.11.4, where a screen HEIGHT reached the kernel as a window
; record and set a bit inside the API jump table. Every caller runs after
; wm_create succeeded, so [fr_win] is never 0 here.
; -----------------------------------------------------------------------------
fr_promise:
    mov bx, [fr_win]
    xor ax, ax                      ; still rendering: withdraw, and wm_saveu
    cmp word [fr_pass], 3           ; drops any cache made under the last
    jb .say                         ; promise with it
    mov al, OSAPI_SAVEU_ON
.say:
    call OSAPI_WM_SAVEU
    ret

; -----------------------------------------------------------------------------
; fr_org - [fr_ox]/[fr_oy] from the window record
; in:  [fr_win]; out: AX = content left, DX = content top, BX = [fr_win], and
;      both stored; preserves CX, SI, DI, BP
; -----------------------------------------------------------------------------
fr_org:
    mov bx, [fr_win]
    call OSAPI_WM_CONTENT
    mov [fr_ox], ax
    mov [fr_oy], dx
    ret

; -----------------------------------------------------------------------------
; fr_kick - UI-side restart: recompute the geometry, reset the state machine,
;           clear the canvas, redraw the status strip, ask the worker to
;           start over
; in:  [fr_win]; gfx lock held (every caller is a window callback or a menu
;      handler); DF clear (every callback is entered with it clear)
; out: nothing; clobbers AX, BX, CX, DX, SI, DI (ES and BP preserved)
;
; This is also the cache's SINGLE invalidation point. Every user-side view
; change - type, palette, centre, zoom - funnels through here, so emptying the
; cache here is the whole of the invalidation rule and there is nowhere else
; to get it wrong. fr_paint does NOT come through here: a repaint is not a
; view change. The run cache itself is claimed here on the first kick and
; retried on every later one - fr_hire's reasoning exactly: a heap that is
; full while Paint has a canvas open is a transient fact, and the user who
; closes Paint should get the cache back without relaunching. It asks for
; FR_CACHE_KB0 only, and fr_emit earns the rest. The claim is freed for us
; when the instance dies (SPEC.md 50.3), so there is no teardown hook.
;
; The reset is one block of bss written in order, because the words were laid
; out in the order it writes them: the zeros, then the three the cache is
; keyed on (copied from fr_cw/fr_ch/fr_mrc), then the render's and the
; frontier's (pass, row) - both (0, rc), pass 0 opening at d = 0 (SPEC.md
; 40.6) - then fr_lrow = 0FFFFh (fr_line is the OLD view's, so no twin may
; be taken from it) and LAST [fr_restart] = 1, so the worker never sees the
; request before the state it names.
;
; fr_hire is last, after the clear: it draws on the canvas when there is no
; worker to fill it. fr_kick_s is the entry for fr_redraw, which has already
; run fr_org and fr_setup and found nothing it can resume from.
; -----------------------------------------------------------------------------
fr_kick:
    call fr_org
    call fr_setup                   ; the clamp happens before we return: it is
fr_kick_s:                          ; what keeps the core's arithmetic in range
    cmp word [fr_cseg], 0
    jne .have
    mov ax, FR_CACHE_KB0
    call OSAPI_MEM_CLAIM            ; DX = base segment, CF=1 refused
    jc .have                        ; a refusal is transient: retry every kick
    mov [fr_cseg], dx
    mov word [fr_ckb], FR_CACHE_KB0
    mov word [fr_cmax], FR_CACHE_KB0*1024
    mov ax, fr_reloc                ; ...and it MOVES (SPEC.md 66.5.7). Safe
    call OSAPI_MEM_MOVABLE          ; from the instant it exists, unlike
                                    ; Tracker's module (66.5.2): nothing has
                                    ; been read into it yet, and every later
                                    ; reader re-aims ES from [fr_cseg]
.have:                              ; fr_cache_reset, inline
    push es
    push ds
    pop es
    mov di, fr_zblk
    mov cx, FR_ZWORDS
    xor ax, ax
    rep stosw                       ; fr_cn .. fr_pct
    mov si, fr_cw
    movsw                           ; fr_ccw  = fr_cw
    movsw                           ; fr_cch  = fr_ch
    movsw                           ; fr_cmrc = fr_mrc
    mov bx, [fr_mrc]
    stosw                           ; fr_pass  = 0
    xchg ax, bx
    stosw                           ; fr_row   = rc
    xchg ax, bx
    stosw                           ; fr_cpass = 0
    xchg ax, bx
    stosw                           ; fr_crow  = rc
    mov ax, 0FFFFh
    stosw                           ; fr_lrow  = none
    neg ax
    stosw                           ; fr_restart = 1, LAST
    pop es
    call fr_promise                 ; back to pass 0: the picture is moving again
    call fr_white                   ; fr_clear, inline: the canvas to white
    add bx, FR_STRIP_H
    mov dx, bx
    add dx, [fr_ch]
    dec dx
    call OSAPI_GFX_FILL
    call fr_status
    jmp fr_hire

; -----------------------------------------------------------------------------
; fr_redraw - W_PAINT-side repaint: replay the coarse image here, hand the
;             rest to the worker
; in:  [fr_win]; gfx lock held (the caller is W_PAINT or fr_abdismiss)
; out: nothing; clobbers AX, BX, CX, DX, SI, DI (ES and BP preserved)
;
; The content arrived white-filled and the view state is untouched, so
; nothing has to be COMPUTED again - the cache holds every row that was
; emitted for this view (fr_kick empties it on every view change). What has
; to be decided is how much of it to put back HERE, holding the gfx lock,
; and the answer is the pass-0 prefix and no more: that is the whole canvas
; at quarter vertical resolution, it is what "the picture is back" means,
; and it is a quarter of the drawing. The rest is left to the worker, which
; replays it a row per lock hold (fr_take) - the same rows, the same total
; drawing, none of it in one frozen hold.
;
; THE REPLAY is fr_emit's paint loop with the arithmetic removed: the same
; 4-row bands (fr_band), from the run words instead of fr_line, stopping at
; [fr_c0n] because everything past that is a refinement and refinements are
; the worker's to replay. The rows are named by stepping the one state
; machine (fr_stepv) from (0, rc), never by a second `add 4` - which was a
; second opinion about what the cache holds, and wrong under a phase (SPEC.md
; 40.1, 40.6). The walk is bounded by the byte count, not by the row loop, so
; a truncated cache stops cleanly.
;
; So the resume point is the row after the last cached PASS-0 one, which is
; fr_stepv from (0, c0row) - one step of the one state machine, and it
; handles both cases without a branch: pass 0 interrupted lands back in pass
; 0, pass 0 complete lands at the head of pass 1 (and skips a pass a
; degenerate canvas has no rows for).
;
; fr_prog becomes the cached row count, which is a no-op when the cache holds
; everything (it does, up to its ceiling) and a truthful reduction when it
; overflowed, because those rows are about to be computed a second time and
; will be counted a second time.
;
; The key compare is belt and braces: fr_setup re-reads W_W/W_H every time
; and wm_fit can clamp them, a cache built for a different canvas would replay
; runs at the wrong columns, and one built in a different PHASE would put
; every row at the wrong height (SPEC.md 40.6). The three words are compared
; as one string because they were laid out as one.
;
; With nothing cached this is exactly the old behaviour, spelled fr_kick.
; -----------------------------------------------------------------------------
fr_redraw:
    call fr_org
    call fr_setup
    cmp word [fr_cn], 0
    je fr_kick_s                    ; nothing cached: restart from row 0
    push es
    push ds
    pop es
    mov si, fr_cw
    mov di, fr_ccw
    mov cx, 3
    repe cmpsw                      ; (fr_cw, fr_ch, fr_mrc) = the cache's?
    pop es
    jne fr_kick_s

    push es                         ; --- fr_replay (inline): the pass-0 prefix
    push bp
    mov bp, [fr_ox]                 ; BP = screen x of column 0
    xor si, si                      ; SI = the next run word
    mov bx, [fr_mrc]                ; pass 0 opens at d = 0 (SPEC.md 40.6)
.row:
    push bx                         ; the row this band is
    xor ax, ax
    call fr_band                    ; pass 0's band: BX = top, DX = bottom
    xor di, di                      ; DI = this run's first column
.run:
    cmp si, [fr_c0n]
    jae .ran                        ; the cache ran out mid-frame
    mov es, [fr_cseg]               ; re-aimed per run: the API calls below
    es lodsw                        ; may hand ES back as anything
    mov cx, ax
    and ch, 0Fh                     ; CX = last column of the run (FR_CRUN)
    mov al, ah                      ; AH = colour<<4 | column bits 8..11, and
    shr al, 1                       ; a 320-wide canvas never sets more than
    shr al, 1                       ; one of those, so four shifts are enough
    shr al, 1                       ; to leave the colour alone in AL
    shr al, 1
    call OSAPI_SET_COLOR
    lea ax, [bp+di]                 ; x1
    mov di, cx
    inc di                          ; the next run's first column
    add cx, bp                      ; x2
    call OSAPI_GFX_FILL
    cmp di, [fr_cw]
    jb .run
    pop bx
    xor ax, ax                      ; the NEXT pass-0 row, off the one state
    call fr_stepv                   ; machine
    or ax, ax
    jz .row
    push bx                         ; (balance the pop below)
.ran:
    pop bx
    pop bp
    pop es

    mov ax, [fr_c0n]                ; the worker picks the cache up where that
    mov [fr_cpos], ax               ; stopped, and replays what is past it
    xor ax, ax
    mov bx, [fr_c0row]              ; the last cached pass-0 row...
    call fr_stepv                   ; ...and the row after it
    mov [fr_pass], ax
    mov [fr_row], bx
    mov ax, [fr_cnrow]
    mov [fr_prog], ax
    mov word [fr_restart], 1        ; LAST of the resume point: the row the
                                    ; worker holds, if any, is not this one
    call fr_promise                 ; ...and a resume is rendering, whatever
                                    ; the cache put back here (SPEC.md 40.4)
    call fr_pctcalc                 ; the WHOLE strip, with the RESUMED number
    mov [fr_pct], al                ; rather than 0% - wm_paint_all has just
    call fr_status                  ; white-filled it, so every field is owed
    jmp fr_hire

; -----------------------------------------------------------------------------
; fr_reloc - the compactor moved the run cache (SPEC.md 66.5.7)
; in:  BX = the base it WAS at, DX = the base it is at NOW, DS = CS = ours
; out: nothing; preserves every register
;
; ONE word, and that is a property of the cache's design rather than luck:
; every cursor into it - [fr_cpos], [fr_cn], [fr_cmax], [fr_cnext] - is a
; byte OFFSET, and an offset is exactly what a move does not change. The
; three walkers (the emit, fr_take and the replay) all re-aim ES from
; [fr_cseg] per row or per run, so there is no second copy of the base
; anywhere to fall out of step with this one.
; -----------------------------------------------------------------------------
fr_reloc:
    cmp bx, [fr_cseg]
    jne .out
    mov [fr_cseg], dx
.out:
    ret

; -----------------------------------------------------------------------------
; fr_take - fill fr_line from the cached row at [fr_cpos], if there is one
; in:  the cache; NO lock held - this is where the compute would be, and it is
;      a read of memory only the lock's holders ever write
; out: CF=0 fr_line holds the row (and its sentinel), [fr_cnext] the offset
;      past it, [fr_cfrom] = 1; CF=1 nothing to replay here - compute it
; clobbers: AX, BX, CX, SI, DI, ES
;
; The cursor is NOT advanced here. It belongs to "this row was consumed",
; which is fr_emit's business under the lock and behind its restart check:
; a repaint republishes the cursor together with the (pass, row) it names,
; and a step taken out here would walk past it.
;
; Every read is bounded by [fr_cn] - re-read per run, not banked - so a
; fr_kick emptying the cache under this walk ends it rather than running it
; off the claim. The row it half-decoded is then dropped by the restart
; check, like any other stale row.
;
; The two refusals are NOT the same exit. Only a walk that gave up mid-row has
; written into fr_line, and only that one may invalidate [fr_lrow]; the
; ordinary "the cursor is at the frontier, compute it" refusal leaves fr_line
; and [fr_lrow] exactly as they were, because fr_worker tries the mirror on
; the very next instruction and a wipe here would mean it never fires at all
; (SPEC.md 40.6).
; -----------------------------------------------------------------------------
fr_take:
    mov byte [fr_cfrom], 0
    mov ax, [fr_cseg]
    or ax, ax
    jz .quiet
    mov si, [fr_cpos]
    cmp si, [fr_cn]
    jae .quiet                      ; the cursor is at the frontier: compute
    mov di, fr_line                 ; DI = where this run starts
    mov bx, [fr_pend]               ; BX = fr_line + cw
    cld
.run:
    mov es, ax                      ; (AX holds fr_cseg for the whole walk)
    cmp si, [fr_cn]
    jae .none                       ; ran out mid-row: not a row, so not ours
    es lodsw
    mov cx, ax
    and ch, 0Fh
    add cx, fr_line                 ; CX -> the run's last byte
    cmp cx, di
    jb .none                        ; runs only ever move forward
    cmp cx, bx
    jae .none                       ; ...and never past the canvas
    sub cx, di
    inc cx                          ; CX = pixels in the run
    mov al, ah                      ; the colour: see fr_redraw's replay
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    push ds
    pop es                          ; stos writes through ES: aim it at us
    rep stosb
    mov ax, [fr_cseg]
    cmp di, bx
    jb .run                         ; CF=0 falls out of here: DI = BX
    mov byte [di], FR_SENT          ; the emit's run scans stop here
    mov [fr_cnext], si              ; the whole row, and only a whole row
    inc byte [fr_cfrom]
    mov ax, [fr_row]                ; fr_line holds THIS row now - a replayed
    mov [fr_lrow], ax               ; row is as good a mirror source as a
    ret                             ; computed one (40.6). CF is still the 0
                                    ; the loop's last compare left: mov and inc
                                    ; do not touch it
.none:
    mov word [fr_lrow], 0FFFFh      ; half a row may have been decoded into
                                    ; fr_line before this gave up
.quiet:                             ; ...where nothing was: fr_line still holds
    stc                             ; the row [fr_lrow] names, and the twin
    ret                             ; test is about to want it (SPEC.md 40.6)

; -----------------------------------------------------------------------------
; fr_setup - derive everything a frame needs from the view state
; in:  [fr_win], [fr_type], [fr_pal], [fr_z], [fr_cenx], [fr_ceny]; UI task,
;      gfx lock held, DF clear
; out: [fr_cw] [fr_ch] [fr_pend] [fr_flg] [fr_cx]/[fr_cy] (Julia only)
;      [fr_step] [fr_x0] [fr_y0] [fr_mrc] [fr_paltab]; the centre re-clamped
; clobbers: AX, BX, CX, DX, SI, DI (ES preserved)
;
; The content rectangle is re-read from the window record every time, never
; from the template: wm_fit clamps the frame onto the live screen and the
; record, not the template, is the truth (SPEC.md 39.7/39.10).
;
; It runs on the UI task only, from fr_kick and fr_redraw, before either
; publishes [fr_restart]. It used to run in the worker as well, on picking
; the flag up, and that was the same pure function of the same words a second
; time.
; -----------------------------------------------------------------------------
fr_setup:
    cld                             ; for the palette here, and for fr_kick's
                                    ; and fr_redraw's string walks after it
    mov bx, [fr_win]
    call OSAPI_WM_GEOM              ; CX/DX = CONTENT w/h (SPEC.md 11)
    mov ax, 1                       ; neither is ever 0: the width is a
    cmp cx, ax                      ; divisor below
    jge .cwok
    mov cx, ax
.cwok:
    mov [fr_cw], cx
    sub dx, FR_STRIP_H              ; ...less our own status strip
    cmp dx, ax
    jge .chok
    mov dx, ax
.chok:
    mov [fr_ch], dx
    add cx, fr_line
    mov [fr_pend], cx               ; where a row of fr_line ends

    call fr_trec                    ; SI = the type's 16-byte record
    mov ax, [si+FT_FLAG]
    mov [fr_flg], al
    test al, FF_JUL                 ; a Julia's c is a constant for the frame;
    jz .nojul                       ; a Mandelbrot-type's c is the pixel
    mov ax, [si+FT_JCX]
    mov [fr_cx], ax
    mov ax, [si+FT_JCY]
    mov [fr_cy], ax
.nojul:
    xor dx, dx                      ; step0 = span / cw: the ONE division
    mov ax, [si+FT_SPAN]
    div word [fr_cw]
    mov cl, [fr_z]                  ; step = step0 >> z, floored at 1 ulp -
    shr ax, cl                      ; which also floors step0 itself, since
    or ax, ax                       ; a zero step0 shifts to zero
    jnz .stok
    inc ax
.stok:
    mov [fr_step], ax

    ; --- fr_clamp, both axes: keep |cx|,|cy| <= 3.5 over the WHOLE view ---
    ; The overflow proof in the file header assumes the bound for every pixel,
    ; not just the centre, so the limit is FR_CLAMP minus the half-span. With
    ; step <= 51 and cw = 320 the half-span is at most 8160, so the limit
    ; never goes negative. The same half-span then places the view's corner:
    ; x0 = cen_x - (cw>>1)*step, and y0 likewise (screen y and the imaginary
    ; part both grow downward). BX = 0 then 2: fr_cw/fr_ch, fr_cenx/fr_ceny
    ; and fr_x0/fr_y0 are word pairs.
    xor bx, bx
.axis:
    mov ax, [fr_cw+bx]
    shr ax, 1
    imul word [fr_step]             ; AX = the half-span
    mov cx, FR_CLAMP
    sub cx, ax                      ; CX = +limit on the centre
    mov dx, [fr_cenx+bx]
    cmp dx, cx
    jle .c1
    mov dx, cx
.c1:
    neg cx
    cmp dx, cx
    jge .c2
    mov dx, cx
.c2:
    mov [fr_cenx+bx], dx
    sub dx, ax
    mov [fr_x0+bx], dx
    inc bx
    inc bx
    cmp bl, 4
    jb .axis

    xor ax, ax                      ; --- the x-axis mirror (SPEC.md 40.6) ---
    mov [fr_mrc], ax                ; off unless every condition below holds
    cmp word [si+FT_SYM], 1         ; ...this type declares the x-axis (the
    jne .nomir                      ; two Julias declare the ORIGIN, which is
                                    ; a reversed row and not this)
    sub ax, [fr_y0]                 ; AX = -y0, and 0 <= -y0 <= 14336 ...
    js .nomir                       ; ...unless the whole canvas is below the
    cwd                             ; axis (DX = 0 from here)
    div word [fr_step]              ; ...so cy = 0 lands on row AX
    or dx, dx
    jnz .nomir                      ; ...but not EXACTLY on one, so there are
                                    ; no twins: a click off the axis normally
                                    ; lands here, and that is the honest limit
    cmp ax, [fr_ch]
    jae .nomir
    mov [fr_mrc], ax
.nomir:

    ; --- the live palette: fr_pals' packed ramp, expanded into fr_paltab ---
    ; Byte 0 is the ramp's period P (even), then P/2 bytes of two entries
    ; each, low nibble first. P entries are unpacked and then replicated to
    ; FR_CAP by an overlapping forward copy. Entry FR_CAP is never written: it
    ; is the loader's zero, CBLACK, the interior's colour in every palette.
    push es
    push ds
    pop es
    mov bx, [fr_pal]
    shl bx, 1
    mov si, [fr_pals+bx]
    mov di, fr_paltab
    lodsb
    cbw
    xchg dx, ax                     ; DX = P
    push dx
    shr dx, 1
    mov cl, 4
.nib:
    lodsb
    mov ah, al
    and al, 0Fh
    shr ah, cl
    stosw
    dec dx
    jnz .nib
    pop cx
    mov si, fr_paltab
    neg cx
    add cx, FR_CAP                  ; DI = fr_paltab + P: copy the ramp onto
    rep movsb                       ; itself until FR_CAP entries are down
    pop es
    ret

; -----------------------------------------------------------------------------
; fr_trec - SI = the current type's fr_types record; clobbers CL
; -----------------------------------------------------------------------------
fr_trec:
    mov si, [fr_type]
    mov cl, 4
    shl si, cl
    add si, fr_types
    ret

; -----------------------------------------------------------------------------
; fr_zoom_in - one level deeper, if there is one
; in:  [fr_z]; out: [fr_z]; preserves every register (flags go)
;
; Two callers - View > Zoom In and a click on the canvas - so the cap lives
; here rather than at each of them. It is MEASURED and not chosen (see the
; file header): zoom is a shift count, so at z = 5 the step reaches the Q4.12
; format's 1-ulp floor and z = 6 draws the identical picture. A control that
; deepened past it would report a zoom the arithmetic cannot deliver.
; -----------------------------------------------------------------------------
fr_zoom_in:
    cmp word [fr_z], FR_ZMAX
    jae .out
    inc word [fr_z]
.out:
    ret

; -----------------------------------------------------------------------------
; fr_defaults - the current type's own view: its centre, zoom 0
; in:  [fr_type]
; out: [fr_cenx] [fr_ceny] [fr_z]; clobbers AX, CL, SI
; -----------------------------------------------------------------------------
fr_defaults:
    call fr_trec
    mov ax, [si+FT_CENX]
    mov [fr_cenx], ax
    mov ax, [si+FT_CENY]
    mov [fr_ceny], ax
    xor ax, ax
    mov [fr_z], ax
    ret

; -----------------------------------------------------------------------------
; fr_stepv - the (pass, row) state machine, one row on
; in:  AX = pass, BX = row, [fr_ch], [fr_mrc]
; out: AX = pass, BX = row - the next row to draw, or pass 3 = frame complete
; clobbers: CX, DX (SI and DI preserved - the replay walks the cache in SI)
;
; Progressive refinement, three passes over the canvas, counted from the
; MIRROR ROW rather than from row 0 (SPEC.md 40.6). With d = row - [fr_mrc]:
;   pass 0: d = 0, +4, -4, +8, -8, ...  painted as a band of 4 rows
;   pass 1: d = +2, -2, +6, -6, ...     painted as a band of 2
;   pass 2: d = +1, -1, +3, -3, ...     painted as a single row
; ch/4 + ch/4 + ch/2 = ch: NO pixel is ever computed twice, only painted
; twice, so the finished image is exactly the non-progressive image - and a
; full (chunky) preview lands in a quarter of the time.
;
; THE PHASE IS THE WHOLE POINT. mirror(r) = 2*rc - r preserves parity but not
; r mod 4, so counted from row 0 a pass-0 row's twin lands in pass 1 and is a
; whole pass away; counted from the axis, -d is the row IMMEDIATELY after +d
; and fr_line still holds it, so the twin costs nothing and no cache is read.
; [fr_mrc] = 0 means no mirror, and it is then EXACTLY the order this walked
; before the phase existed - checked at every canvas height rather than
; argued, because that is what makes four of the five types byte-identical.
;
; It takes its state in registers because there are FOUR walkers over it and
; they must not be able to disagree: the render, the cache's frontier (both
; through fr_stepm), fr_redraw's replay putting the pass-0 prefix back, and
; fr_redraw working out where the worker resumes. The cache stores rows in
; exactly the order this produces them and nothing else records which row is
; which, so a second copy of this arithmetic would be a second opinion about
; what the cache contains - which the replay was, until SPEC.md 40.6.
; -----------------------------------------------------------------------------
fr_stepv:
    push si
    push di
    mov cx, [fr_mrc]                ; CX = rc
    mov si, [fr_ch]
    dec si
    sub si, cx                      ; SI = max(rc, ch-1-rc), the point past
    cmp si, cx                      ; which neither side of the axis has a
    jae .lim                        ; row left and the pass is exhausted
    mov si, cx
.lim:
    call fr_incv                    ; DX = this pass's |d| increment
    sub bx, cx                      ; BX = d
.step:
    or bx, bx
    jg .flip
    jl .grow
    add bx, dx                      ; d = 0 -> the first pair's +d
    jmp short .chk
.flip:
    neg bx                          ; +d -> -d, the twin, next
    jmp short .chk
.grow:
    neg bx                          ; -d -> the next pair's +d
    add bx, dx
.chk:
    mov di, bx
    or di, di
    jns .pos
    neg di
.pos:
    cmp di, si
    ja .nextpass                    ; |d| past both edges: this pass is done
    mov di, bx
    add di, cx                      ; DI = the candidate row. A d that is off
    cmp di, [fr_ch]                 ; ONE edge is skipped, not a terminator -
    jae .step                       ; and a negative row fails this unsigned
    mov bx, di
    jmp short .out
.nextpass:
    inc ax
    cmp ax, 3
    jae .out                        ; frame complete
    call fr_incv                    ; the new pass's increment...
    mov bx, dx
    shr bx, 1                       ; ...and its first |d|: 2 for pass 1,
    jmp short .chk                  ; 1 for pass 2 - which is inc/2 for both
.out:
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; fr_stepm - step the (pass, row) word pair at [DI] in place
; in:  DI = fr_pass (the render) or fr_cpass (the cache's frontier)
; out: the pair stepped, AX = its new pass; clobbers BX, CX, DX
; -----------------------------------------------------------------------------
fr_stepm:
    mov ax, [di]
    mov bx, [di+2]
    call fr_stepv
    mov [di], ax
    mov [di+2], bx
    ret

; -----------------------------------------------------------------------------
; fr_incv - the |d| increment of pass AX: 4 for passes 0 and 1, 2 for pass 2
; in:  AX = pass
; out: DX; clobbers nothing else
; -----------------------------------------------------------------------------
fr_incv:
    mov dx, 4
    cmp ax, 2
    jne .out
    mov dl, 2
.out:
    ret

; -----------------------------------------------------------------------------
; fr_band - the SCREEN band canvas row BX paints in pass AX
; in:  AX = pass, BX = canvas row, [fr_ch] [fr_oy]
; out: BX = top, DX = bottom (screen rows, inclusive); clobbers AX, CX
;
; fr_emit and the replay both need this and both had their own copy - the
; second one hard-coding pass 0's +3 - which was survivable while the band was
; row..row+3 and is not now. SPEC.md 40.6 phases the passes from the axis, so
; pass 0 opens at rc mod 4 and up to three rows sit ABOVE every pass-0 band;
; left alone they stay unpainted until pass 2, which is a white line along the
; top of the canvas for the first quarter of a render. The topmost pass-0 band
; reaches row 0 instead. The band's extra rows are 3 >> pass - 3, 1, 0 - and
; the answer comes back in the two registers OSAPI_GFX_FILL takes it in, so
; neither caller stores it.
; -----------------------------------------------------------------------------
fr_band:
    xchg cx, ax                     ; CL = pass
    mov ax, 3
    shr ax, cl
    add ax, bx                      ; AX = row + the band's extra rows
    jcxz .p0
.top:
    mov cx, [fr_ch]                 ; ...clipped to the last canvas row
    dec cx
    cmp ax, cx
    jle .b
    xchg ax, cx
.b:
    mov dx, [fr_oy]
    add dx, FR_STRIP_H              ; DX = the canvas's top on screen
    add bx, dx
    add dx, ax
    ret
.p0:
    cmp bx, 4                       ; the topmost pass-0 band reaches the top
    jae .top                        ; of the canvas - see the header
    xor bx, bx
    jmp short .top


; -----------------------------------------------------------------------------
; frac_iter - THE iteration core: five fractals, one loop body
; in:  BX = zx0, SI = zy0 (Q4.12); [fr_cx]/[fr_cy] = c; [fr_flg] = FF_ABS |
;      FF_NEG
; out: AX = escape index 0..FR_CAP-1, or FR_CAP for a point that never escaped
; clobbers: AX, BX, CX, DX, SI, DI, BP
;
; Register map: BX = zx, SI = zy, DI = countdown, CX = x2, BP = y2 (a VALUE
; register - [bp+..] would address SS, and this code never dereferences it),
; AX/DX scratch.
;
; The Q4.12 multiply is `imul` then FOUR shl/rcl pairs: the 32-bit product in
; DX:AX shifted LEFT by 4 leaves bits 12..27 in DX. That is 16 clocks.
; `shr ax,12` + `shl dx,4` + `or` is the obvious alternative and costs 91,
; because a multi-bit shift on an 8086 is 8 + 4*CL. Do not write it that way.
;
; The escape ordering and the 8191 guard are explained in the file header and
; are not adjustable.
; -----------------------------------------------------------------------------
frac_iter:
    mov di, FR_CAP
    mov [fr_hx], bx                 ; SPEC.md 40.7: the cycle reference starts
    mov [fr_hy], si                 ; as z0 itself - a real state of the orbit
                                    ; rather than a sentinel, which is what
                                    ; lets a fixed point at the origin be
                                    ; caught on the very first comparison
    jmp short .loop
.escaped:                           ; placed BEFORE the body on purpose: every
    mov ax, FR_CAP                  ; exit branch below is a backward rel8, and
    sub ax, di                      ; the body is ~150 bytes long
    ret
.loop:
    mov ax, bx                      ; guard: -8191 <= zx <= 8191, one unsigned
    add ax, FR_GUARD                ; range test. |zx| >= 2 means x2+y2 >= 4,
    cmp ax, FR_GUARD*2+1            ; so this is exact, not an approximation -
    jae .escaped                    ; and it is what keeps the squares in range
    mov ax, si
    add ax, FR_GUARD
    cmp ax, FR_GUARD*2+1
    jae .escaped

    mov ax, bx                      ; x2 = qmul(zx,zx): never negative, so no
    imul bx                         ; toward-zero bias is needed
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    mov cx, dx                      ; CX = x2, 0..16379
    mov ax, si                      ; y2 = qmul(zy,zy)
    imul si
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    mov bp, dx                      ; BP = y2, 0..16379
    add dx, cx                      ; r2 = x2 + y2, 0..32758: fits a signed
    cmp dx, FR_FOUR                 ; word with 9 to spare
    jge .escaped

    mov ax, bx                      ; t = qmul(zx,zy): SIGNED, so bias the
    imul si                         ; product so the shift truncates toward
    or dx, dx                       ; zero instead of toward -infinity. That
    jns .pos                        ; identity is what keeps the conjugate
    add ax, FR_ONE-1                ; symmetry exact.
    adc dx, 0
.pos:
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    test byte [fr_flg], FF_ABS      ; Burning Ship: (|Re z| + i|Im z|)^2 leaves
    jz .noabs                       ; both squares untouched, so the ONLY
    or dx, dx                       ; difference is y' = 2|x||y| + cy
    jns .noabs
    neg dx
.noabs:
    add dx, dx                      ; 2t
    test byte [fr_flg], FF_NEG      ; Tricorn: conj(z)^2 -> y' = -2xy + cy
    jz .noneg
    neg dx
.noneg:
    add dx, [fr_cy]
    mov si, dx                      ; zy'
    mov ax, cx
    sub ax, bp                      ; x2 - y2
    add ax, [fr_cx]
    mov bx, ax                      ; zx'

    cmp bx, [fr_hx]                 ; SPEC.md 40.7: has this orbit been here
    je .cycy                        ; before? zx first, so the zy compare is
.nocyc:                             ; only paid on a match
    dec di
    jz .interior
    test di, FR_CYCK-1              ; ...and the reference is replaced every
    jz .cycref                      ; FR_CYCK iterations. The refresh test is
    jmp .loop                       ; rel16: the body is out of rel8 range
.cycref:
    mov [fr_hx], bx
    mov [fr_hy], si
    jmp .loop
.cycy:
    cmp si, [fr_hy]                 ; a full match: the map is a function of
    jne .nocyc                      ; (zx, zy) alone, so this orbit retraces
                                    ; itself forever and can never reach the
                                    ; escape test. It IS an FR_CAP point, and
                                    ; saying so now is the same answer sooner
.interior:
    mov ax, FR_CAP
    ret

; -----------------------------------------------------------------------------
; fr_q12 - DX:AX (a signed 32-bit product) -> DX = product >> 12, truncated
;          TOWARD ZERO, which is bit for bit what frac_iter's inline chain
;          does and is the whole reason this is one routine and not two
; in:  DX:AX = the product
; out: DX = the Q4.12 result; AX clobbered, flags clobbered
;
; frac_iter inlines this three times because it is the innermost loop in the
; package and a near call+ret is ~11us on the target (PERFORMANCE.md Part 2).
; fr_inset does not: it runs at most four multiplies for a pixel that reaches
; the shapes at all, off the hot path, so the four calls cost less than the
; 64 bytes four copies of the chain would.
; -----------------------------------------------------------------------------
fr_q12:
    or dx, dx                       ; a negative product is biased before the
    jns .pos                        ; shift so the truncation goes toward zero
    add ax, FR_ONE-1                ; instead of toward -infinity
    adc dx, 0
.pos:
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    ret

; -----------------------------------------------------------------------------
; fr_inset - is this c PROVABLY interior?  (SPEC.md 40.5)
; in:  [fr_cx] [fr_cy] = c in Q4.12, and [fr_flg] ALREADY KNOWN ZERO by the
;      caller - see the precondition below
; out: CF set   = frac_iter would return FR_CAP for this c; do not iterate
;      CF clear = unknown; iterate
; clobbers: AX, BX, CX, DX, SI - the same freedom frac_iter has, and for the
;      same reason: fr_rowcalc keeps every scrap of its loop state in memory
;
; An interior pixel costs FR_CAP iterations, which is 144 multiplies, and at
; the default view they are 76.9% of the frame (SPEC.md 40.5). The main
; cardioid and the period-2 bulb are the two largest interior components and
; both have a closed form, so this answers them in at most four multiplies:
;
;     cardioid   dx = cx - 1/4,  q = dx^2 + cy^2,  q(q + dx) < cy^2/4
;     bulb       (cx + 1)^2 + cy^2 < 1/16
;
; THE CLAIM IS ABOUT THIS CORE, NOT ABOUT THE MATHEMATICAL SET, and that is
; the only claim worth making: what was checked is that every Q4.12 lattice
; point this routine can claim is one frac_iter returns FR_CAP for. The sweep
; is exhaustive over the region the gates admit, and it was run again at a
; margin of ZERO, where the test claims 22,951,518 points and still disagrees
; with the core nowhere. That one run settles every margin at once, because
; raising the margin claims a strict subset. tests/unit/t_frinset.py is it. Widening a gate or cutting FR_INMARG without
; re-running that is how this becomes a black speck in the outer field, which
; is the failure SPEC.md 40 warns about for the mirror and warns about here.
;
; PRECONDITION: [fr_flg] IS ZERO - the Mandelbrot, which is the one type it
; is, and the one type this shape describes. Burning Ship carries FF_ABS and
; Tricorn FF_NEG, both of which have an interior of a different shape; both
; Julias carry FF_JUL, where c is a constant for the whole frame and a
; per-pixel test of c means nothing at all. fr_rowcalc tests the flag at the
; SINGLE call site rather than here, so those four pay one compare a pixel
; instead of a compare wrapped in a near call, and they stay byte-identical.
; tests/unit/t_frinset.py is what keeps the precondition true: it fails if
; the Mandelbrot ever stops being the only type with a zero flag word.
; -----------------------------------------------------------------------------
fr_inset:
    mov cx, [fr_cy]                 ; CX = |cy|, which both shapes are bounded
    or cx, cx                       ; in and which is therefore the FIRST gate:
    jns .ay                         ; a view above them both - a click on the
    neg cx                          ; north tip, say - is rejected in two
.ay:                                ; instructions and pays nothing else
    cmp cx, FR_SYMAX
    ja .no

    mov ax, cx                      ; SI = cy^2, wanted by both shapes
    imul ax
    call fr_q12
    mov si, dx

    mov bx, [fr_cx]                 ; --- the main cardioid ---
    sub bx, FR_QTR                  ; BX = dx
    cmp bx, FR_CDXHI
    jg .bulb
    cmp bx, FR_CDXLO
    jl .bulb
    mov ax, bx
    imul bx
    call fr_q12
    add dx, si                      ; DX = q = dx^2 + cy^2
    cmp dx, FR_ONE                  ; q >= 1 is outside the cardioid, and it is
    jge .bulb                       ; also what keeps the product below in range
    mov ax, dx
    add ax, bx                      ; AX = q + dx
    imul dx                         ; DX:AX = (q + dx) * q
    call fr_q12
    mov ax, si                      ; AX = cy^2 / 4, less the margin
    shr ax, 1
    shr ax, 1
    sub ax, FR_INMARG
    cmp dx, ax
    jl .yes

.bulb:                              ; --- the period-2 bulb ---
    cmp cx, FR_BULBR                ; its box is disjoint from the cardioid's
    jae .no                         ; in cx, so reaching here costs two compares
    mov bx, [fr_cx]
    add bx, FR_ONE                  ; BX = cx + 1
    cmp bx, FR_BULBR
    jge .no
    cmp bx, -FR_BULBR
    jle .no
    mov ax, bx
    imul bx
    call fr_q12
    add dx, si                      ; DX = (cx + 1)^2 + cy^2
    cmp dx, FR_BULBR2-FR_INMARG
    jl .yes
.no:
    clc
    ret
.yes:
    stc
    ret

; -----------------------------------------------------------------------------
; fr_pctcalc - the render progress as 0..100
; in:  [fr_prog], [fr_ch]
; out: AL = the percentage (AX); clobbers DX
; Its own routine because fr_redraw wants the number without the comparison:
; after a repaint the whole strip has to go out whatever the number is, and
; that is fr_status, not fr_emit's incremental path.
; -----------------------------------------------------------------------------
fr_pctcalc:
    mov ax, 100
    mul word [fr_prog]              ; prog <= ch <= 460, so DX = 0 and the
    div word [fr_ch]                ; quotient 0..100 always fits AL
    ret

; -----------------------------------------------------------------------------
; fr_status_pct - the percentage field alone: one opaque, space-padded run
; in:  [fr_pct] already stored; [fr_ox]/[fr_oy] valid; lock held
; out: nothing; clobbers AX, BX, CX, DX, SI, DI
;
; The padding is what makes this safe to call without erasing first: the run is
; FR_PCT_CELLS wide whatever the number is, so the field carries its own erase.
; That is INSURANCE rather than a case the incremental path reaches - fr_prog
; only ever increments while a render runs, so the number only grows - and
; fr_status draws the field through here too, onto the ground it has just
; filled, so there is one way to letter a percentage. The pad is laid first and
; the digits over it: "100%" is the longest at four, so cell 4 is always a
; space and cell 5 the NUL.
; -----------------------------------------------------------------------------
fr_status_pct:
    mov word [fr_numbuf+2], 2020h   ; '  '
    mov word [fr_numbuf+4], 0020h   ; ' ', NUL
    mov di, fr_numbuf
    mov al, [fr_pct]
    cbw
    mov bx, 10
    xor cx, cx
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in CX
.div:
    xor dx, dx
    div bx
    push dx                         ; digits come out backwards
    inc cx
    or ax, ax
    jnz .div
.wr:
    pop ax
    add al, '0'
    mov [di], al
    inc di
    loop .wr
    mov byte [di], '%'
    mov si, fr_numbuf
    mov cx, FR_X_PCT
    ; fall through to fr_text

; -----------------------------------------------------------------------------
; fr_text / fr_textxy - one opaque run of black-on-white text in the content
; in:  SI = NUL string; CX = x offset in the content; for fr_textxy DX = y
;      offset in the content (fr_text puts it on the status strip's baseline);
;      [fr_ox]/[fr_oy] valid, lock held
; out: nothing; clobbers AX, CX, DX
;
; Every string this package draws goes through here: the four strip fields
; and the two lines of the no-worker notice. The pair is the strip's and the
; canvas's own ground (SPEC.md 40.2.2), so no field reads [gfx_color].
; -----------------------------------------------------------------------------
fr_text:
    mov dx, FR_TXT_Y
fr_textxy:
    add cx, [fr_ox]
    add dx, [fr_oy]
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = the ground
    call OSAPI_FONT_RUN
    ret

; -----------------------------------------------------------------------------
; fr_white - the pen to white, and the content's x span
; in:  [fr_ox]/[fr_oy]/[fr_cw]
; out: AX = content left, CX = content right (inclusive), BX = content top;
;      the pen CWHITE
; Both of this package's fills - the strip and the canvas - are white and
; full width; this is the part they share.
; -----------------------------------------------------------------------------
fr_white:
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [fr_ox]
    mov cx, ax
    add cx, [fr_cw]
    dec cx
    mov bx, [fr_oy]
    ret

; -----------------------------------------------------------------------------
; fr_status - the one-line strip: fractal name, zoom level, render progress,
;             palette
; in:  [fr_ox]/[fr_oy] valid; lock held
; out: nothing; clobbers AX, BX, CX, DX, SI, DI
;
; The strip is a UNIT: it is white-filled and then written over with four
; runs, and those two operations do not clip alike (SPEC.md 11.3 - the fill
; goes per pixel, the glyphs per whole 8x8 cell). Cut horizontally by another
; window's edge, an ungated strip would erase the rows you can see and then
; decline to put any text back in them. So it asks first, whole rect, and
; leaves the old strip alone when the answer is no. Unclipped - every fr_kick
; and fr_redraw path - the test passes and nothing changes. The pen is set
; before the question rather than after it; it belongs to this hold either
; way (SPEC.md 68.2.5) and no field below reads it.
;
; 'Zoom' and its digit are ONE run, "Zoom N": FR_X_ZNUM is FR_X_ZOOM plus
; five cells, so the space between them is a white cell drawn over the white
; ground and the picture is the one two runs drew. The digit is written into
; the string itself, which lives in this instance's own image.
; -----------------------------------------------------------------------------
fr_status:
    call fr_white
    mov dx, bx
    add dx, FR_STRIP_H-1
    call OSAPI_WM_CLIP_TEST
    jc .out
    call OSAPI_GFX_FILL
    call fr_trec                    ; the fractal's name
    mov si, [si+FT_NAME]
    mov cx, FR_X_NAME
    call fr_text
    mov al, [fr_z]                  ; 'Zoom' + the exponent 0..4
    add al, '0'
    mov [fr_s_zdig], al
    mov si, fr_s_zoom
    mov cx, FR_X_ZOOM
    call fr_text
    call fr_status_pct              ; the render progress, 0..100%
    mov bx, [fr_pal]                ; the palette, named by its own menu item
    shl bx, 1
    mov si, [fr_mi_col+bx]
    mov cx, FR_X_PAL
    jmp fr_text
.out:
    ret

; =============================================================================
; 'About Fractal' - the credit card (SPEC.md 12.2, 20.5.1)
; =============================================================================
; The card is os88ui.inc's. What is here is the flag, the painter drawing it
; last, the two handlers taking it down - and the one thing this app has that
; Mines and Piano do not: a WORKER painting bands into the same content
; eighteen times a second. fr_emit checks [fr_abon] under the lock right
; after it has cached the row, so the picture is complete in the cache even
; though those bands never reached the glass, and fr_redraw below puts every
; one of them back.

; -----------------------------------------------------------------------------
; fr_about - the OSAPI_ABOUT_SET handler (slot 0x018A)
; in:  SI = our window ptr; the UI task, gfx lock HELD
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
fr_about:
    push bx
    push si
    mov [fr_win], si
    mov byte [fr_abon], 1
    mov bx, si
    mov si, fr_ablines
    call os88ui_about               ; arms the clip itself: a menu dispatch
    pop si                          ; arrives without one (SPEC.md 11.3)
    pop bx
    ret

; -----------------------------------------------------------------------------
; fr_abdismiss - take the card down if it is up
; in:  SI = our window ptr; gfx lock held
; out: [fr_win] = SI. CF = 0 nothing was up: every register preserved. CF = 1
;      the click was spent doing it: AX, SI, ES and BP preserved, BX, CX,
;      DX and DI clobbered - which is what both callers can take: fr_onclick
;      restores everything on that path, and fr_oncmd goes on to use AX and SI
;
; fr_redraw and not the card's own rect: it replays the pass-0 cache, which
; is both what was under the card AND every band the worker skipped while it
; was up - one repaint settles both debts. fr_redraw re-reads the content
; origin itself, since the window may have been dragged since the card went up.
; -----------------------------------------------------------------------------
fr_abdismiss:
    mov [fr_win], si
    cmp byte [fr_abon], 0
    je .none                        ; CF = 0: equal
    mov byte [fr_abon], 0
    push ax
    push si
    mov bx, si
    call OSAPI_WM_CLIP_SET          ; nothing armed a region for a click
    jc .gone                        ; either (SPEC.md 11.3)
    call fr_redraw
.gone:
    pop si
    pop ax
    stc
.none:
    ret

; --- the About card's lines (SPEC.md 20.5.1) ----------------------------------
fr_ablines:
    dw fr_ab1, fr_ab2, fr_ab3, 0
fr_ab1:     db 'Fractal for os8088', 0
fr_ab2:     db 0
fr_ab3:     db 'Contributed by Jorge Gonzalez', 0

; --- window template (SPEC.md 11: 16 bytes, 8 words) ---------------------------
; 322 x 199 -> content 320 x 180 -> canvas 320 x 170. No W_ONKEY: everything
; this app does is a menu command or a click.
fr_tpl:
    dw 150, 60, 322, 199
    dw fr_ttl, fr_paint, 0, fr_onclick

fr_ttl:      db 'Fractal', 0

; --- app menu set (SPEC.md 12.2) -----------------------------------------------
; Three menus of the four the bar can host. Name + titles are short on
; purpose: they must clear the menu-bar clock's hit band at x 434, and
; 'Fractal' + 'Fractal' + 'Colour' + 'View' ends well short of it.
    OS88_MENUSET fr_menus, fr_ttl, fr_oncmd
        OS88_MENU fr_ttl,    fr_mi_frac, 5  ; the menu is titled 'Fractal' too,
        OS88_MENU fr_m_col,  fr_mi_col,  4  ; so it is the window title's string
        OS88_MENU fr_m_view, fr_mi_view, 4
    OS88_MENUSET_END fr_menus

fr_mi_frac:  dw fr_s_mandel, fr_s_dendrite, fr_s_rabbit, fr_s_ship, fr_s_tricorn
fr_m_col:    db 'Colour', 0
fr_mi_col:   dw fr_s_spectrum, fr_s_fire, fr_s_ice, fr_s_contour
fr_m_view:   db 'View', 0
fr_mi_view:  dw fr_s_zin, fr_s_zout, fr_s_reset, fr_s_redraw

; The five names are shared by the menu and the status strip (FT_NAME).
fr_s_mandel:   db 'Mandelbrot', 0
fr_s_dendrite: db 'Julia Dendrite', 0
fr_s_rabbit:   db 'Julia Rabbit', 0
fr_s_ship:     db 'Burning Ship', 0
fr_s_tricorn:  db 'Tricorn', 0
; ...and the four palette names likewise (fr_mi_col doubles as the table).
fr_s_spectrum: db 'Spectrum', 0
fr_s_fire:     db 'Fire', 0
fr_s_ice:      db 'Ice', 0
fr_s_contour:  db 'Contour', 0
fr_s_zin:      db 'Zoom In', 0
fr_s_zout:     db 'Zoom Out', 0
fr_s_reset:    db 'Reset', 0
fr_s_redraw:   db 'Redraw', 0
; The strip's zoom field, ONE run: fr_status writes the digit in place.
fr_s_zoom:     db 'Zoom '
fr_s_zdig:     db '0', 0
%if (fr_s_zdig - fr_s_zoom) * 8 != FR_X_ZNUM - FR_X_ZOOM
  %error "fr_s_zoom no longer puts the digit at FR_X_ZNUM"
%endif
; The no-worker notice (fr_nowork): 32 glyphs each, 256px of the 320px canvas.
fr_s_now1:     db 'No free task slot for the render', 0
fr_s_now2:     db 'Close an app, then View > Redraw', 0

; --- the five fractals ---------------------------------------------------------
; Every literal is Q4.12. -504 and 3052 are round(-0.123*4096) and
; round(0.745*4096): the realised rabbit constant is -0.12305 + 0.74512i,
; three orders of magnitude closer to nominal than the 0.0112/pixel sampling
; step, so the rabbit is exactly the rabbit.
;
; Screen y grows downward and so does the imaginary part. Four of the five
; are symmetric about the real axis or the origin, so it is invisible;
; Burning Ship is not, and its centre (-0.5,-0.5) is chosen for this
; orientation - it produces the classic ship, hull to the lower left.
;
; Every default view satisfies the FR_CLAMP bound with room: the worst is
; Burning Ship at 2048 + 8160 = 10208 against 14336.
fr_types:
;        name            flags   jcx    jcy   cen_x  cen_y   span  sym
    dw fr_s_mandel,     0x0000,     0,     0, -2048,     0, 14336, 1
    dw fr_s_dendrite,   0x0004,     0,  4096,     0,     0, 16384, 2
    dw fr_s_rabbit,     0x0004,  -504,  3052,     0,     0, 14746, 2
    dw fr_s_ship,       0x0001,     0,     0, -2048, -2048, 16384, 0
    dw fr_s_tricorn,    0x0002,     0,     0,     0,     0, 16384, 1

; --- palettes: escape count -> EGA colour index, 48 entries each ---------------
; The interior is CBLACK in every palette - a separate constant, not entry 0 -
; so each ramp is free to start bright. 42% of Mandelbrot pixels escape in
; under 4 iterations, which is why none of the four opens on a dark colour
; that would merge with the set.
;
; Each is stored as its PERIOD - the ramp repeats to fill 48 - packed two
; entries to a byte, and fr_setup expands the live one into fr_paltab. That
; table is what the compute loop indexes, with entry 48 the interior.
%macro FR_PAL 2-*                   ; the entries of one period, an even count
    db %0
  %if %0 % 2 || FR_CAP % %0
    %error "a palette period must be even and divide FR_CAP"
  %endif
  %rep %0 / 2
    db (%2 << 4) | %1
    %rotate 2
  %endrep
%endmacro
fr_pals:     dw fr_pal_spectrum, fr_pal_fire, fr_pal_ice, fr_pal_contour

; Spectrum: a 12-hue wheel, four times round.
fr_pal_spectrum:
    FR_PAL  1, 9,11, 3,10, 2,14, 6,12, 4,13, 5

; Fire: dark -> red -> orange -> yellow -> white -> back, twice.
fr_pal_fire:
    FR_PAL  8, 8, 4, 4, 4,12,12,12, 6, 6,14,14, \
           15,15,14,14, 6, 6,12,12, 4, 4, 8, 8

; Ice: dark -> blue -> cyan -> white -> back, twice.
fr_pal_ice:
    FR_PAL  8, 8, 1, 1, 1, 9, 9, 9, 3, 3,11,11, \
           15,15,11,11, 3, 3, 9, 9, 1, 1, 8, 8

; Contour: the 1bpp-safe one. SPEC.md 39.4 collapses 16 colours into three
; classes (0..6 black, 7/8/9/10/11/13 dither, 12/14/15 white); this ramp uses
; ONLY white and dither, in strict runs of four. Twelve evenly spaced contour
; bands whose every boundary is a white<->dither transition, and - uniquely
; among the four - no ramp entry is ever black, so on a mono screen the
; interior is the only black region and the set reads as a solid silhouette.
fr_pal_contour:
    FR_PAL 15,15,14,14, 7, 7,11,11,12,12,15,15, \
            9, 9,13,13,14,14,12,12,11,11, 7, 7, \
           15,15,14,14,13,13, 9, 9,12,12,15,15, \
            7, 7,11,11,14,14,12,12, 9, 9,13,13

; --- the shared controls (SPEC.md 20.5.1) -------------------------------------
%define OS88UI_ABOUT            ; the standard About card, and NOTHING else:
%define OS88UI_NOBTN            ; this window's controls are all menu items
%include "os88ui.inc"

    OS88_BSS FR_BSS_TOTAL
    OS88_IMAGE_END

; --- loader-zeroed bss (SPEC.md 21 step 5) -------------------------------------
; All zero is type 0 (Mandelbrot), palette 0 (Spectrum), zoom 0, no worker -
; but NOT the Mandelbrot's centre, which fr_entry loads via fr_defaults.
; Each symbol is placed after the one before it, so a reorder is an edit to
; one line; the ORDER is load-bearing in four places and each says so.
fr_ox      equ os88_image_end        ; word: content left (re-read per call)
fr_oy      equ fr_ox + 2             ; word: content top
fr_win     equ fr_oy + 2             ; word: our window ptr (spawn + worker)
; --- the view state: four words, and everything else derives from them
fr_type    equ fr_win + 2            ; word: 0..4, index into fr_types (stored
                                     ; as a byte: the high byte stays zero)
fr_pal     equ fr_type + 2           ; word: 0..3, index into fr_pals (ditto)
fr_z       equ fr_pal + 2            ; word: zoom exponent 0..FR_ZMAX
fr_cenx    equ fr_z + 2              ; word: Q4.12 centre - a PAIR with fr_ceny,
fr_ceny    equ fr_cenx + 2           ; word:  for fr_setup's and fr_onclick's
                                     ; per-axis loops
; --- derived once per frame by fr_setup
fr_cw      equ fr_ceny + 2           ; word: canvas width  (content width)
fr_ch      equ fr_cw + 2             ; word: canvas height (content less strip)
fr_mrc     equ fr_ch + 2             ; word: the canvas row cy = 0 falls on, or
                                     ; 0 = no mirror (SPEC.md 40.6). It is BOTH
                                     ; the twin's pivot and fr_stepv's PHASE,
                                     ; and 0 for "none" costs nothing to
                                     ; conflate with row 0: rc = 0 has no
                                     ; in-range twin anyway, and the phase is
                                     ; then the order this package walked
                                     ; before 40.6. fr_cw/fr_ch/fr_mrc are the
                                     ; cache's KEY, in this order, and are
                                     ; copied and compared as one string
fr_step    equ fr_mrc + 2            ; word: (span / cw) >> z, floored at 1
fr_x0      equ fr_step + 2           ; word: complex coord of canvas column 0 -
fr_y0      equ fr_x0 + 2             ; word:  ...and of row 0, a PAIR
fr_cx      equ fr_y0 + 2             ; word: c for the core (Julia: constant;
fr_cy      equ fr_cx + 2             ; word:  Mandelbrot-type: the pixel)
fr_pend    equ fr_cy + 2             ; word: fr_line + cw, where a row ends
; --- the compute loop's own state
fr_pcx     equ fr_pend + 2           ; word: running pixel coord, this row
fr_pcy     equ fr_pcx + 2            ; word
fr_px      equ fr_pcy + 2            ; word: -> the byte of fr_line being made
fr_hx      equ fr_px + 2             ; word: the cycle check's reference state
fr_hy      equ fr_hx + 2             ; word:  (SPEC.md 40.7). Live only inside
                                     ; frac_iter, which seeds it from z0 on
                                     ; entry, so nothing outside has to know
                                     ; it exists or reset it
fr_flg     equ fr_hy + 2             ; byte: FF_* for the live type
fr_spawned equ fr_flg + 1            ; byte: 1 = this instance owns its worker.
                                     ; Latches on SUCCESS only: while it is 0
                                     ; every kick retries the spawn, so a slot
                                     ; freed later is picked up (SPEC.md 20.6)
fr_cfrom   equ fr_spawned + 1        ; byte: 1 = fr_line was REPLAYED, not
                                     ; computed
fr_abon    equ fr_cfrom + 1          ; byte: the About card is up
fr_numbuf  equ fr_abon + 1           ; 6 bytes: "100% " + NUL
fr_line    equ fr_numbuf + 6         ; 320 bytes: one colour index per column,
                                     ; and one more for FR_SENT after the last
; --- the restore cache (see the file header). The runs themselves are a HEAP
; claim that regrows, not bss: a busy view wants 32KB and a plain one 340
; bytes, and a package that reserved the larger in its region would be
; refused a launch on a machine that can run it perfectly well with the
; smaller - or with no cache at all.
fr_cseg    equ fr_line + 321         ; word: the claim, or 0 = we have none
fr_cmax    equ fr_cseg + 2           ; word: its size in BYTES
fr_ckb     equ fr_cmax + 2           ; word: the claim's size in KB, which is
                                     ; what doubles
fr_cnext   equ fr_ckb + 2            ; word: the offset past the row fr_take
                                     ; decoded
; --- fr_kick's reset block: written front to back by one string walk, so
; these are in exactly the order fr_kick stores them (see there)
fr_zblk    equ fr_cnext + 2
fr_cn      equ fr_zblk               ; word: BYTES of the cache in use, not runs
fr_cnrow   equ fr_cn + 2             ; word: ROWS in it - what fr_prog becomes
                                     ; across a repaint
fr_c0n     equ fr_cnrow + 2          ; word: BYTES of the pass-0 prefix, which
                                     ; is the part fr_redraw replays inline
fr_c0row   equ fr_c0n + 2            ; word: the LAST cached pass-0 row, which
                                     ; is what fr_redraw takes one fr_stepv
                                     ; step off (SPEC.md 40.6)
fr_cpos    equ fr_c0row + 2          ; word: the worker's replay cursor
fr_prog    equ fr_cpos + 2           ; word: rows computed this frame
fr_pct     equ fr_prog + 2           ; byte (a word of the block): the last
                                     ; percentage drawn
FR_ZWORDS  equ (fr_pct + 2 - fr_zblk) / 2
fr_ccw     equ fr_pct + 2            ; word: the canvas the cache was built for
fr_cch     equ fr_ccw + 2            ; word:  - fr_cw/fr_ch/fr_mrc's copy, and a
fr_cmrc    equ fr_cch + 2            ; word:  mismatch invalidates it
fr_pass    equ fr_cmrc + 2           ; word: 0/1/2 progressive pass, 3 = done -
fr_row     equ fr_pass + 2           ; word:  and the canvas row: a PAIR (fr_stepm)
fr_cpass   equ fr_row + 2            ; word: the frontier - the (pass, row) the
fr_crow    equ fr_cpass + 2          ; word:  cache is waiting to be handed
fr_lrow    equ fr_crow + 2           ; word: the canvas row fr_line holds, or
                                     ; 0FFFFh = nothing. The twin test compares
                                     ; against this and nothing else, so a
                                     ; stale fr_line can only ever cost a
                                     ; recompute - never a wrong row
fr_restart equ fr_lrow + 2           ; word: UI -> worker "the row in hand is
                                     ; stale" - the LAST word fr_kick writes
; --- the live palette
fr_paltab  equ fr_restart + 2        ; FR_CAP+1 bytes: escape index -> colour;
                                     ; entry FR_CAP is CBLACK and never written
fr_bss_end equ fr_paltab + FR_CAP + 1

%if fr_bss_end - os88_image_end != FR_BSS_TOTAL
  %error "FR_BSS_TOTAL does not match the bss layout"
%endif
%if CBLACK != 0
  %error "fr_paltab's last entry is the loader's zero, which must be CBLACK"
%endif
%if FR_SENT < 16
  %error "FR_SENT must not be a colour index"
%endif

; The percentage field's padding is written into fr_numbuf, and fr_numbuf's
; size is the gap to the next bss symbol rather than a declaration - so
; nothing but this check stands between a wider field and FR_PCT_CELLS
; silently overwriting the first byte of fr_line.
%if FR_PCT_CELLS + 1 > fr_line - fr_numbuf
  %error "FR_PCT_CELLS + a NUL does not fit fr_numbuf - widen the gap to fr_line"
%endif
%if FR_PCT_CELLS != 5
  %error "fr_status_pct's pad stores assume a five-cell field"
%endif
