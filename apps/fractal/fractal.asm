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

    OS88_HEADER 'FRACTAL', fr_entry, 1

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
; more, but by replaying the (pass, row) state machine from (0, 0): rows are
; appended in the order fr_advance produces them, so stepping fr_stepv once
; per stored row names every one of them. That is why there is exactly one
; copy of that arithmetic and why fr_stepv exists.
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

FR_BSS_TOTAL equ 414                ; see the bss layout after OS88_IMAGE_END

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
    mov word [fr_pal], 3            ; Contour: the 1bpp-safe ramp
.colour:
    call fr_defaults                ; centre + zoom for the starting type
    mov si, fr_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr, CF on table full
    jc .out                         ; no window: nothing to attach menus to
    mov [fr_win], bx
    mov si, fr_menus
    call OSAPI_MENU_SET             ; BX = the window, SI = our set
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
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov [fr_ox], ax
    mov [fr_oy], dx
    call fr_redraw
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
; overflow before the clamp brings it back.
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
    mov [fr_win], si
    mov bx, si
    push cx
    push dx
    call OSAPI_WM_CONTENT
    mov [fr_ox], ax
    mov [fr_oy], dx
    pop dx
    pop cx
    sub cx, [fr_ox]                 ; -> content-relative
    sub dx, [fr_oy]
    sub dx, FR_STRIP_H              ; -> canvas-relative
    js .no
    cmp dx, [fr_ch]
    jge .no
    or cx, cx
    jl .no
    cmp cx, [fr_cw]
    jl .hit
.no:
    jmp .out
.hit:
    mov ax, [fr_cw]
    shr ax, 1
    sub cx, ax                      ; CX = px - cw/2
    mov ax, [fr_ch]
    shr ax, 1
    sub dx, ax                      ; DX = py - ch/2
    push dx
    mov ax, cx
    imul word [fr_step]             ; |product| <= 10240: AX is the answer
    add ax, [fr_cenx]
    mov [fr_cenx], ax
    pop dx
    mov ax, dx
    imul word [fr_step]
    add ax, [fr_ceny]
    mov [fr_ceny], ax
    call fr_zoom_in                 ; ...and only now, with both products taken
    call fr_kick                    ; fr_setup re-clamps both axes
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
; range check is needed - only an order matching the declaration.
; -----------------------------------------------------------------------------
fr_oncmd:
    mov [fr_win], si
    push ax
    mov bx, si
    call OSAPI_WM_CONTENT
    mov [fr_ox], ax
    mov [fr_oy], dx
    pop ax
    or ah, ah
    jnz .m1

    mov ah, 0                       ; --- Fractal: the five types ---
    mov [fr_type], ax
    call fr_defaults                ; a new type brings its own view
    jmp .kick
.m1:
    dec ah
    jnz .m2
    mov ah, 0                       ; --- Colour: the four palettes ---
    mov [fr_pal], ax
    jmp .kick
.m2:
    or al, al                       ; --- View: Zoom In / Out / Reset / Redraw
    jnz .v1
    call fr_zoom_in
    jmp .kick
.v1:
    cmp al, 1
    jne .v2
    cmp word [fr_z], 0
    je .kick
    dec word [fr_z]
    jmp .kick
.v2:
    cmp al, 2
    jne .kick                       ; item 3 = Redraw: restart, keep the view
    call fr_defaults
.kick:
    call fr_kick
    ret

; -----------------------------------------------------------------------------
; fr_worker - THE background task (SPEC.md 20.6)
; in:  DX = our instance index, DS = ES = CS = KERNEL_SEG, IF = 1, gfx lock
;      free. NEVER returns and never exits on its own - the only way out is
;      OSAPI_TASK_ALIVE not coming back.
;
; Outer loop, in the order the contract demands:
;   1. OSAPI_TASK_ALIVE with the lock NOT held (rule 4: it takes the lock
;      itself, and gfx_lock is not reentrant).
;   2. Pick up whatever the UI task asked for: 1 = restart from row 0 (a view
;      change), 2 = resume (a repaint replayed the cache and already set the
;      pass and row to continue from). The flag is a plain word store on one
;      side and a read-and-clear XCHG on the other: the 8086 recognises
;      interrupts only at instruction boundaries, so both are atomic against
;      the PIT switch whatever their alignment. The XCHG matters now that
;      there are two values - a separate test and clear could see the resume,
;      have fr_kick overwrite it with a restart, and then clear the restart
;      away. No lock, no protocol.
;   3. Nothing to do -> sleep. Otherwise get ONE row lock-free - out of the
;      cache if a repaint left rows there to replay, else computed - re-check
;      the restart flag (a view change mid-row makes the row stale - drop
;      it), emit under the lock, yield. The check below is only the cheap
;      early-out: the binding one is inside fr_emit_body, under the lock,
;      which is the only place it can be atomic against fr_kick.
;
; fr_take is where a repaint stops costing anything. A replayed row is the
; same row through the same emit path - same band, same clip, same visibility
; test - so nothing downstream knows the difference, and the loop paces it
; exactly as it paces a computed one: one row per lock hold, yield between.
; That is what keeps a 5-second worth of restored detail off the UI task.
;
; Note what is NOT here: fr_advance, and the fr_prog increment. Both belong
; to "this row was consumed", and both live inside fr_emit_body so that they
; happen under the same lock hold as the check that decides it. Outside it
; they are a race the resume path cannot survive: fr_redraw publishes a
; (pass, row) to continue from, and a stale fr_advance out here would step
; straight past it - leaving a canvas row no pass ever paints and an
; fr_crow the cache can never match again. While the flag only ever meant
; "restart" that was harmless, because the loop top rewrote pass and row
; anyway; the resume value 2 deliberately keeps them, which is exactly why
; it cannot stay out here.
; -----------------------------------------------------------------------------
fr_worker:
.loop:
    mov bx, [fr_win]
    call OSAPI_TASK_ALIVE           ; may never return; preserves everything
    xor ax, ax
    xchg ax, [fr_restart]           ; read and clear in ONE instruction
    or ax, ax
    je .norst
    call fr_setup
    cmp ax, 2
    je .norst                       ; resume: keep the pass and row the UI set
    mov word [fr_pass], 0
    mov word [fr_row], 0
    mov word [fr_prog], 0
    mov byte [fr_pct], 0
.norst:
    cmp word [fr_pass], 3
    jae .idle                       ; frame complete
    cmp word [fr_ch], 1
    jge .work
.idle:
    mov ax, 4
    call OSAPI_TASK_SLEEP
    jmp .loop
.work:
    call fr_take                    ; CF=0: a repaint left this row cached
    jnc .ready
    call fr_rowcalc                 ; the expensive part: NO lock held
.ready:
    cmp word [fr_restart], 0
    jne .again                      ; the view changed under us: drop the row
    call fr_emit                    ; lock, cache, paint the band, step, unlock
    call OSAPI_TASK_YIELD
.again:
    jmp .loop

; -----------------------------------------------------------------------------
; fr_hire - make sure this instance owns its worker, and say so when it does
;           not (SPEC.md 20.6)
; in:  gfx lock HELD (every caller is a window callback or a menu handler),
;      [fr_win] valid, the canvas freshly cleared by fr_kick
; out: nothing; preserves all registers
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
    push ax
    push bx
    cmp byte [fr_spawned], 1
    je .out                         ; we own one already: a second
                                    ; OSAPI_TASK_SPAWN would only be refused
    mov al, 1                       ; PARK-SAFE (SPEC.md 66.5.4): nothing here
    call OSAPI_MEM_PARKSAFE         ; holds a cache-derived pointer across a
                                    ; call that can yield. The worker's ONLY
                                    ; lock site is fr_emit, which takes the
                                    ; lock and then calls fr_emit_body - and
                                    ; the body re-reads [fr_cseg] rather than
                                    ; inheriting it, so the interval spent
                                    ; blocked in OSAPI_GFX_LOCK holds nothing
                                    ; at all. fr_take DOES hold [fr_cseg] in
                                    ; AX for its whole walk, and is lock-free,
                                    ; but a task pre-empted there is not
                                    ; parked: this marks the task only while
                                    ; it is descheduled INSIDE gfx_lock.
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
    jc .none
    mov byte [fr_spawned], 1
    jmp short .out
.none:
    call fr_nowork                  ; the canvas would otherwise stay blank
.out:                               ; with no explanation at all
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fr_nowork - "there is no worker" on the empty canvas
; in:  gfx lock held; [fr_ox]/[fr_oy]/[fr_cw]/[fr_ch] valid
; out: nothing; preserves all registers
;
; Two centred lines. Each is 32 glyphs at 8px, so 256px against the 320px
; canvas - it fits on every adapter, since the window is not WF_SIZABLE and
; only its height is ever clamped (SPEC.md 39.7).
; -----------------------------------------------------------------------------
fr_nowork:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov si, fr_s_now1
    mov bx, -10
    call .line
    mov si, fr_s_now2
    mov bx, 2
    call .line
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.line:                              ; SI = string, BX = y offset from the
    call OSAPI_FONT_WIDTH           ; canvas centre
    mov cx, [fr_cw]
    sub cx, ax
    jg .half
    xor cx, cx                      ; wider than the canvas: flush left
.half:
    shr cx, 1
    add cx, [fr_ox]
    mov dx, [fr_ch]
    shr dx, 1
    add dx, bx
    jns .y_ok
    xor dx, dx
.y_ok:
    add dx, [fr_oy]
    add dx, FR_STRIP_H
    call OSAPI_FONT_STR
    ret

; -----------------------------------------------------------------------------
; fr_kick - UI-side restart: recompute the geometry, reset the state machine,
;           clear the canvas, redraw the status strip, ask the worker to
;           start over
; in:  [fr_ox]/[fr_oy] valid; gfx lock held (every caller is a window
;      callback or a menu handler)
; out: nothing; clobbers AX, BX, CX, DX, SI, DI
;
; fr_setup runs on BOTH sides - here for the clamp (which must happen before
; the caller returns, because the clamp is what keeps the core's arithmetic
; in range) and again in the worker when it picks up the flag. It is a pure
; function of the state words, so computing it twice is harmless.
;
; fr_hire is last, after the clear: it draws on the canvas when there is no
; worker to fill it.
;
; This is also the pass-0 cache's SINGLE invalidation point. Every user-side
; view change - type, palette, centre, zoom - funnels through here, so
; emptying the cache here is the whole of the invalidation rule and there is
; nowhere else to get it wrong. fr_paint deliberately does NOT come through
; here any more: a repaint is not a view change.
; -----------------------------------------------------------------------------
fr_kick:
    call fr_setup
    call fr_cache_reset
    mov word [fr_pass], 0
    mov word [fr_row], 0
    mov word [fr_prog], 0
    mov byte [fr_pct], 0
    mov word [fr_restart], 1
    call fr_clear
    call fr_status
    call fr_hire
    ret

; -----------------------------------------------------------------------------
; fr_redraw - W_PAINT-side repaint: replay the coarse image here, hand the
;             rest to the worker
; in:  [fr_ox]/[fr_oy] valid; gfx lock held (the caller is W_PAINT)
; out: nothing; clobbers AX, BX, CX, DX, SI, DI
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
; So the resume point is the row after the last cached PASS-0 one, which is
; fr_stepv from (0, c0row-4) - one step of the one state machine, and it
; handles both cases without a branch: pass 0 interrupted lands back in pass
; 0, pass 0 complete lands at the head of pass 1 (and skips a pass a
; degenerate canvas has no rows for). The worker is told to RESUME -
; fr_restart = 2 - rather than start over, so it keeps what this sets.
;
; fr_prog becomes the cached row count, which is a no-op when the cache holds
; everything (it does, up to its ceiling) and a truthful reduction when it
; overflowed, because those rows are about to be computed a second time and
; will be counted a second time.
;
; The geometry compare is belt and braces: fr_setup re-reads W_W/W_H every
; time and wm_fit can clamp them, and a cache built for a different canvas
; would replay runs at the wrong columns.
;
; With nothing cached this is exactly the old behaviour, spelled fr_kick.
; -----------------------------------------------------------------------------
fr_redraw:
    call fr_setup
    cmp word [fr_cn], 0
    je fr_kick                      ; nothing cached: restart from row 0
    mov ax, [fr_ccw]
    cmp ax, [fr_cw]
    jne fr_kick
    mov ax, [fr_cch]
    cmp ax, [fr_ch]
    jne fr_kick

    call fr_replay                  ; the pass-0 prefix, inline
    mov ax, [fr_c0n]                ; the worker picks the cache up where that
    mov [fr_cpos], ax               ; stopped, and replays what is past it
    xor ax, ax
    mov bx, [fr_c0row]
    sub bx, 4                       ; the last cached pass-0 row...
    call fr_stepv                   ; ...and the row after it
    mov [fr_pass], ax
    mov [fr_row], bx
    mov ax, [fr_cnrow]
    mov [fr_prog], ax
    mov word [fr_restart], 2        ; 2 = resume, not restart
    call fr_pctcalc                 ; the WHOLE strip, with the RESUMED number
    mov [fr_pct], al                ; rather than 0% - wm_paint_all has just
    call fr_status                   ; white-filled it, so every field is owed.
                                    ; It used to park an impossible [fr_pct]
                                    ; and go through fr_status_maybe; that path
                                    ; draws one field now, so a repaint asking
                                    ; it for the strip would get the percentage
                                    ; alone on an empty band
    call fr_hire
    ret

; -----------------------------------------------------------------------------
; fr_cache_claim - take the run cache off the heap (SPEC.md 50.3)
; in:  nothing; out: [fr_cseg]/[fr_ckb]/[fr_cmax], or [fr_cseg] = 0 if refused
; preserves all registers
;
; Called from fr_cache_reset, so the FIRST kick claims it and every later one
; retries - fr_hire's reasoning exactly: a heap that is full while Paint has
; a canvas open is a transient fact, and the user who closes Paint should get
; the cache back without relaunching. The claim is freed for us when the
; instance dies (SPEC.md 50.3), so there is no teardown hook here.
;
; It asks for the first 4KB only, and fr_cache_grow earns the rest.
; -----------------------------------------------------------------------------
fr_cache_claim:
    push ax
    push dx
    mov ax, FR_CACHE_KB0
    call OSAPI_MEM_CLAIM            ; DX = base segment, CF=1 refused
    jc .out
    mov [fr_cseg], dx
    mov word [fr_ckb], FR_CACHE_KB0
    push ax                         ; ...and it MOVES (SPEC.md 66.5.7). Safe
    mov ax, fr_reloc                ; from the instant it exists, unlike
    call OSAPI_MEM_MOVABLE          ; Tracker's module (66.5.2): nothing has
    pop ax                          ; been read into it yet, and every later
                                    ; reader re-aims ES from [fr_cseg]
    call fr_cache_size
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fr_reloc - the compactor moved the run cache (SPEC.md 66.5.7)
; in:  BX = the base it WAS at, DX = the base it is at NOW, DS = CS = ours
; out: nothing; preserves every register
;
; ONE word, and that is a property of the cache's design rather than luck:
; every cursor into it - [fr_cpos], [fr_cn], [fr_cmax], [fr_ctlen] - is a
; byte OFFSET, and an offset is exactly what a move does not change. The
; three walkers (the render, the frontier and the replay cursor) all re-aim
; ES from [fr_cseg] per row or per run, so there is no second copy of the
; base anywhere to fall out of step with this one.
; -----------------------------------------------------------------------------
fr_reloc:
    cmp bx, [fr_cseg]
    jne .out
    mov [fr_cseg], dx
.out:
    ret

; -----------------------------------------------------------------------------
; fr_cache_grow - double the cache, if the heap can spare it (SPEC.md 50.3)
; in:  [fr_cseg] valid; out: CF=0 it is bigger and [fr_cseg] may have MOVED,
;      CF=1 it is untouched and the caller must stop growing
; preserves all registers
;
; Called from fr_cache_row's out-of-room path, so the cache is exactly as big
; as the pictures this session has drawn and no bigger. Three things about it:
;
; ALWAYS take the base from DX. A grow that had to move leaves the old
; segment pointing at memory that is no longer ours (SPEC.md 50.3), so the
; caller reloads ES from [fr_cseg] and lays the row down again.
;
; It never shrinks, on a view change or otherwise. The next view is drawn on
; the same screen at the same zoom range as the one before it, so the size
; this reached is the best available guess at what the next one wants, and a
; shrink-then-regrow per kick is two heap operations to arrive back where it
; started - possibly via a move, which copies.
;
; A move copies inside mem_regrow's IF=0 window, so it is a tick or two of
; lost time on a 4.77MHz machine. That is affordable BECAUSE it doubles:
; three moves buys the ceiling, and the total copied is bounded by twice the
; final size however big that gets.
; -----------------------------------------------------------------------------
fr_cache_grow:
    push ax
    push bx
    push cx
    push dx
    mov cx, [fr_ckb]
    cmp cx, FR_CACHE_MAXKB
    jae .no                         ; the ceiling: our offsets are words
    call OSAPI_MEM_AVAIL            ; AX = largest free run in KB
    mov bx, cx                      ; the increment is the size again, and we
    add bx, bx                      ; want that much left for everybody else
    cmp ax, bx
    jb .no
    add cx, cx
    mov ax, cx
    mov dx, [fr_cseg]
    call OSAPI_MEM_REGROW           ; out DX = the base NOW: it may have MOVED
    jc .no
    mov [fr_cseg], dx
    mov [fr_ckb], cx
    call fr_cache_size
    clc
    jmp short .out
.no:
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fr_cache_size - [fr_ckb] KB -> [fr_cmax] bytes, the one place that converts
; in:  [fr_ckb]; out: [fr_cmax]; preserves all registers except the flags
; -----------------------------------------------------------------------------
fr_cache_size:
    push ax
    push cx
    mov ax, [fr_ckb]
    mov cl, 10
    shl ax, cl                      ; 32,768 at the ceiling: still a word
    mov [fr_cmax], ax
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fr_cache_reset - empty the cache and stamp it with this canvas
; in:  [fr_cw]/[fr_ch] valid (fr_setup has run)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
fr_cache_reset:
    push ax
    cmp word [fr_cseg], 0
    jne .have
    call fr_cache_claim             ; a refusal is transient: retry every kick
.have:
    mov word [fr_cn], 0
    mov word [fr_cnrow], 0
    mov word [fr_cpass], 0
    mov word [fr_crow], 0
    mov word [fr_c0n], 0
    mov word [fr_c0row], 0
    mov word [fr_cpos], 0
    mov byte [fr_cfrom], 0
    mov ax, [fr_cw]
    mov [fr_ccw], ax
    mov ax, [fr_ch]
    mov [fr_cch], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; fr_cache_row - append the row in fr_line to the cache
; in:  fr_line valid for canvas row [fr_row]; the gfx lock is HELD
; out: nothing; preserves all registers
;
; Called from fr_emit_body, under the lock and after its restart check, for
; two reasons. It has to be atomic against fr_kick for the same reason the
; paint does - a row computed for the old view must not be cached for the
; new one - and it has to happen whether or not the window is visible,
; because a fully covered fractal still renders and its cache is exactly
; what makes uncovering it cheap.
;
; EVERY pass is cached, and the (fr_cpass, fr_crow) frontier is what keeps
; the rows in step with the state machine that names them. It is stepped by
; fr_stepv - the same routine fr_advance steps the render with - so the two
; cannot drift, which is the whole reason that arithmetic was factored out.
; [fr_c0n] tracks the byte the pass-0 prefix ends at, because that is the
; part fr_redraw replays inline, and [fr_c0row] the pass-0 row it stops at,
; because that is where the worker resumes.
;
; A row is committed whole or not at all: a partial row would desync every
; row after it, since the start column of each run is implied by the end of
; the last. Out of room, it asks fr_cache_grow for more and lays the row down
; again from the rollback point - re-encoding one row is a few hundred byte
; compares against the half-second that computed it, and it is a great deal
; simpler than resuming a run walk into a claim that may have moved. When the
; heap says no the cache simply stops growing: the frontier stays put, so
; every later row fails the test above and the cached prefix stays replayable.
; -----------------------------------------------------------------------------
fr_cache_row:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, [fr_cseg]
    or ax, ax
    jz .out                         ; the heap refused us: no cache at all
    mov es, ax
    mov ax, [fr_pass]
    cmp ax, [fr_cpass]
    jne .out
    mov ax, [fr_row]
    cmp ax, [fr_crow]
    jne .out                        ; not the row the cache is waiting for
    mov di, [fr_cn]
    mov dx, di                      ; DX = rollback point
    xor si, si                      ; SI = first column of the run
.run:
    mov bx, si
    mov al, [fr_line+bx]
.ext:
    inc bx
    cmp bx, [fr_cw]
    jae .flush
    cmp al, [fr_line+bx]
    je .ext
.flush:
    mov cx, [fr_cmax]
    sub cx, FR_CRUN
    cmp di, cx
    ja .full
    mov ah, al                      ; pack: colour << 12 | last column
    mov al, 0
    mov cl, 4
    shl ax, cl
    dec bx                          ; BX = last column of the run
    or ax, bx
    mov [es:di], ax
    inc bx
    add di, FR_CRUN
    mov si, bx
    cmp si, [fr_cw]
    jb .run
    mov [fr_cn], di                 ; commit the whole row
    inc word [fr_cnrow]
    cmp word [fr_cpass], 0
    jne .adv                        ; past pass 0: the inline prefix is fixed
    mov [fr_c0n], di
.adv:
    mov ax, [fr_cpass]              ; step the frontier the ONE way rows step
    mov bx, [fr_crow]
    call fr_stepv
    mov [fr_cpass], ax
    mov [fr_crow], bx
    or ax, ax
    jz .p0
    mov bx, [fr_ch]                 ; out of pass 0: the prefix is complete,
.p0:                                ; which is what fr_redraw reads c0row for
    mov [fr_c0row], bx
    jmp short .out
.full:
    call fr_cache_grow              ; CF=0: bigger now - lay the row again
    jc .stop
    mov es, [fr_cseg]               ; ...and it may be somewhere else entirely
    mov di, dx                      ; back to where this row started
    xor si, si
    jmp .run
.stop:
    mov [fr_cn], dx                 ; never a partial row
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
; fr_take - fill fr_line from the cached row at [fr_cpos], if there is one
; in:  the cache; NO lock held - this is where fr_rowcalc would be, and it is
;      a read of memory only the lock's holders ever write
; out: CF=0 fr_line holds the row and [fr_ctlen] its bytes, [fr_cfrom] = 1;
;      CF=1 nothing to replay here - the caller must compute the row
; clobbers: AX, BX, CX, SI, DI, ES
;
; The cursor is NOT advanced here. It belongs to "this row was consumed",
; which is fr_emit_body's business under the lock and behind its restart
; check, for the same reason fr_advance is (see fr_worker): a repaint
; republishes the cursor together with the (pass, row) it names, and a step
; taken out here would walk past it.
;
; Every read is bounded by [fr_cn] - re-read per run, not banked - so a
; fr_kick emptying the cache under this walk ends it rather than running it
; off the claim. The row it half-decoded is then dropped by the restart
; check, like any other stale row.
; -----------------------------------------------------------------------------
fr_take:
    mov byte [fr_cfrom], 0
    mov ax, [fr_cseg]
    or ax, ax
    jz .none
    mov si, [fr_cpos]
    cmp si, [fr_cn]
    jae .none                       ; the cursor is at the frontier: compute
    xor di, di                      ; DI = the column this run starts at
.run:
    mov es, ax                      ; (AX holds fr_cseg for the whole walk)
    cmp si, [fr_cn]
    jae .none                       ; ran out mid-row: not a row, so not ours
    mov bx, [es:si]
    add si, FR_CRUN
    mov ax, bx
    and bx, 0x0FFF                  ; BX = the run's last column
    cmp bx, di
    jb .none                        ; runs only ever move forward
    cmp bx, [fr_cw]
    jae .none
    mov al, ah                      ; AH = colour<<4 | column bits 8..11, and
    shr al, 1                       ; a 320-wide canvas never sets more than
    shr al, 1                       ; one of those, so four shifts leave the
    shr al, 1                       ; colour alone in AL
    shr al, 1
    mov cx, bx
    sub cx, di
    inc cx                          ; CX = pixels in the run
    push ds
    pop es                          ; stos writes through ES: aim it at us
    add di, fr_line
    cld
    rep stosb
    sub di, fr_line                 ; DI = the next run's first column
    mov ax, [fr_cseg]
    cmp di, [fr_cw]
    jb .run
    sub si, [fr_cpos]
    mov [fr_ctlen], si              ; the whole row, and only a whole row
    mov byte [fr_cfrom], 1
    clc
    ret
.none:
    stc
    ret

; -----------------------------------------------------------------------------
; fr_replay - put the cached PASS-0 rows back onto the canvas
; in:  the cache, [fr_ox]/[fr_oy]/[fr_cw]/[fr_ch]; the gfx lock is HELD
; out: nothing; preserves all registers
;
; The same 4-row bands fr_emit_body paints, from the same run representation
; - this IS the emit loop with the arithmetic removed. It stops at [fr_c0n],
; the end of the pass-0 prefix, because everything past that is a refinement
; and refinements are the worker's to replay (fr_redraw). Rows the cache does
; not reach are left as wm_paint_all's white fill and the worker computes
; them; the run walk is bounded by the byte count, not by the row loop, so a
; truncated cache stops cleanly.
; -----------------------------------------------------------------------------
fr_replay:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    xor si, si
    mov di, [fr_c0n]
    mov word [fr_crrow], 0
.row:
    mov ax, [fr_crrow]              ; band bottom = row + 3, clipped to the
    add ax, 3                       ; last canvas row
    mov bx, [fr_ch]
    dec bx
    cmp ax, bx
    jle .b2
    mov ax, bx
.b2:
    add ax, [fr_oy]
    add ax, FR_STRIP_H
    mov [fr_by2], ax
    mov ax, [fr_crrow]
    add ax, [fr_oy]
    add ax, FR_STRIP_H
    mov [fr_by1], ax
    mov word [fr_p], 0
.run:
    or di, di
    jz .out                         ; the cache ran out mid-frame
    mov es, [fr_cseg]               ; re-aimed per run: the two API calls
    mov ax, [es:si]                 ; below go through stubs that own ES
    mov cx, ax                      ; unpack (see the FR_CRUN comment)
    and cx, 0x0FFF                  ; CX = last column of the run
    mov al, ah                      ; AH = colour<<4 | column bits 8..11, and
    shr al, 1                       ; a 320-wide canvas never sets more than
    shr al, 1                       ; one of those, so four shifts are enough
    shr al, 1                       ; to leave the colour alone in AL
    shr al, 1
    add si, FR_CRUN
    sub di, FR_CRUN
    call OSAPI_SET_COLOR
    push cx
    mov ax, [fr_p]
    add ax, [fr_ox]
    add cx, [fr_ox]
    mov bx, [fr_by1]
    mov dx, [fr_by2]
    call OSAPI_GFX_FILL
    pop cx
    inc cx
    mov [fr_p], cx
    cmp cx, [fr_cw]
    jb .run
    mov ax, [fr_crrow]
    add ax, 4
    mov [fr_crrow], ax
    cmp ax, [fr_ch]
    jb .row
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
; fr_setup - derive everything a frame needs from the view state
; in:  [fr_win], [fr_type], [fr_pal], [fr_z], [fr_cenx], [fr_ceny]
; out: [fr_cw] [fr_ch] [fr_flg] [fr_cx]/[fr_cy] (Julia only) [fr_step0]
;      [fr_step] [fr_x0] [fr_y0] [fr_palp]; the centre re-clamped
; clobbers: nothing
;
; The content rectangle is re-read from the window record every time, never
; from the template: wm_fit clamps the frame onto the live screen and the
; record, not the template, is the truth (SPEC.md 39.7/39.10).
; -----------------------------------------------------------------------------
fr_setup:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, [fr_win]
    or bx, bx
    jnz .have
    jmp .out
.have:
    call OSAPI_WM_GEOM              ; CX/DX = CONTENT w/h (SPEC.md 11) - the
                                    ; kernel does the w-2 / h-TITLE_H-1 that
                                    ; every caller used to repeat off the
                                    ; record, so this package no longer needs
                                    ; ES to point at kernel memory at all
    mov ax, cx
    cmp ax, 1
    jge .cwok
    mov ax, 1                       ; never 0: it is a divisor below
.cwok:
    mov [fr_cw], ax
    mov ax, dx
    sub ax, FR_STRIP_H              ; ...less our own status strip
    cmp ax, 1
    jge .chok
    mov ax, 1
.chok:
    mov [fr_ch], ax

    mov ax, [fr_type]               ; SI = the type's 16-byte record
    mov cl, 4
    shl ax, cl
    mov si, fr_types
    add si, ax
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
    or ax, ax
    jnz .s0ok
    mov ax, 1
.s0ok:
    mov [fr_step0], ax
    mov cl, [fr_z]                  ; step = step0 >> z, floored at 1 ulp
    shr ax, cl
    or ax, ax
    jnz .stok
    mov ax, 1
.stok:
    mov [fr_step], ax

    call fr_clamp                   ; unconditional: see the file header

    mov ax, [fr_cw]                 ; x0 = cen_x - (cw>>1)*step
    shr ax, 1
    imul word [fr_step]
    neg ax
    add ax, [fr_cenx]
    mov [fr_x0], ax
    mov ax, [fr_ch]                 ; y0 likewise (screen y and the imaginary
    shr ax, 1                       ; part both grow downward)
    imul word [fr_step]
    neg ax
    add ax, [fr_ceny]
    mov [fr_y0], ax

    mov bx, [fr_pal]
    shl bx, 1
    mov ax, [fr_pals+bx]
    mov [fr_palp], ax
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fr_clamp - keep |cx|,|cy| <= 3.5 over the WHOLE view, both axes
; in:  [fr_cw] [fr_ch] [fr_step] [fr_cenx] [fr_ceny]
; out: the centre clamped; clobbers nothing
;
; The overflow proof in the file header assumes the bound for every pixel,
; not just the centre, so the limit is FR_CLAMP minus the half-span. With
; step <= 51 and cw = 320 the half-span is at most 8160, so the limit never
; goes negative.
; -----------------------------------------------------------------------------
fr_clamp:
    push ax
    push bx
    push dx
    mov ax, [fr_cw]
    shr ax, 1
    imul word [fr_step]
    mov bx, FR_CLAMP
    sub bx, ax                      ; BX = +limit on cen_x
    mov ax, [fr_cenx]
    cmp ax, bx
    jle .x1
    mov ax, bx
.x1:
    neg bx
    cmp ax, bx
    jge .x2
    mov ax, bx
.x2:
    mov [fr_cenx], ax
    mov ax, [fr_ch]
    shr ax, 1
    imul word [fr_step]
    mov bx, FR_CLAMP
    sub bx, ax                      ; BX = +limit on cen_y
    mov ax, [fr_ceny]
    cmp ax, bx
    jle .y1
    mov ax, bx
.y1:
    neg bx
    cmp ax, bx
    jge .y2
    mov ax, bx
.y2:
    mov [fr_ceny], ax
    pop dx
    pop bx
    pop ax
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
; out: [fr_cenx] [fr_ceny] [fr_z]; clobbers nothing
; -----------------------------------------------------------------------------
fr_defaults:
    push ax
    push cx
    push si
    mov ax, [fr_type]
    mov cl, 4
    shl ax, cl
    mov si, fr_types
    add si, ax
    mov ax, [si+FT_CENX]
    mov [fr_cenx], ax
    mov ax, [si+FT_CENY]
    mov [fr_ceny], ax
    mov word [fr_z], 0
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fr_stepv - the (pass, row) state machine, one row on
; in:  AX = pass, BX = row, [fr_ch]
; out: AX = pass, BX = row - the next row to draw, or pass 3 = frame complete
; clobbers: nothing else
;
; Progressive refinement, three passes over the canvas:
;   pass 0: rows r = 0, 4, 8, ...  painted as a band of 4 rows
;   pass 1: rows r = 2, 6, 10, ... painted as a band of 2
;   pass 2: rows r = 1, 3, 5, ...  painted as a single row
; ch/4 + ch/4 + ch/2 = ch: NO pixel is ever computed twice, only painted
; twice, so the finished image is exactly the non-progressive image - and a
; full (chunky) preview lands in a quarter of the time.
;
; It takes its state in registers because there are THREE walkers over it and
; they must not be able to disagree: the render (fr_advance), the cache's
; frontier (fr_cache_row), and fr_redraw working out where the worker resumes.
; The cache stores rows in exactly the order this produces them and nothing
; else records which row is which, so a second copy of this arithmetic would
; be a second opinion about what the cache contains.
; -----------------------------------------------------------------------------
fr_stepv:
    push dx
    mov dx, 4
    cmp ax, 2
    jne .step
    mov dx, 2
.step:
    add bx, dx
    cmp bx, [fr_ch]
    jb .out
.next:
    inc ax
    cmp ax, 3
    jae .out                        ; frame complete
    mov bx, 2                       ; pass 1 starts at row 2, pass 2 at row 1
    cmp ax, 1
    je .set
    mov bx, 1
.set:
    cmp bx, [fr_ch]
    jae .next                       ; degenerate canvas: skip the empty pass
.out:
    pop dx
    ret

; -----------------------------------------------------------------------------
; fr_advance - move the render's (pass, row) on by one drawn row
; in:  [fr_pass] [fr_row] [fr_ch]
; out: the next row to draw, or pass 3 = frame complete
; clobbers: nothing
; -----------------------------------------------------------------------------
fr_advance:
    push ax
    push bx
    mov ax, [fr_pass]
    mov bx, [fr_row]
    call fr_stepv
    mov [fr_pass], ax
    mov [fr_row], bx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fr_rowcalc - compute canvas row [fr_row] into fr_line
; in:  the derived frame state; NO LOCK HELD, and that is the whole point:
;      this is tens of thousands to millions of cycles
; out: fr_line[0..cw-1] = colour indices
; clobbers: AX, BX, CX, DX, SI, DI, BP
;
; Per-pixel arithmetic is exactly one ADD (cx += step) plus the core call -
; no multiply, no divide, per the mapping cx = x0 + p*step maintained
; incrementally. All the loop's own state lives in memory because frac_iter
; clobbers every register.
; -----------------------------------------------------------------------------
fr_rowcalc:
    mov ax, [fr_row]                ; cy = y0 + row*step  (<= 191*51: fits AX)
    imul word [fr_step]
    add ax, [fr_y0]
    mov [fr_pcy], ax
    mov ax, [fr_x0]
    mov [fr_pcx], ax
    mov word [fr_px], 0
.pixel:
    test byte [fr_flg], FF_JUL
    jz .mand
    mov bx, [fr_pcx]                ; Julia: z0 = the pixel, c already set
    mov si, [fr_pcy]
    jmp short .go
.mand:
    mov ax, [fr_pcx]                ; Mandelbrot-type: z0 = 0, c = the pixel
    mov [fr_cx], ax
    mov ax, [fr_pcy]
    mov [fr_cy], ax
    xor bx, bx
    xor si, si
.go:
    call frac_iter                  ; AX = escape index, or FR_CAP = interior
    cmp ax, FR_CAP
    jb .escaped
    xor al, al                      ; the interior is CBLACK in every palette -
    jmp short .store                ; a separate constant, not table entry 0
.escaped:
    mov bx, [fr_palp]
    add bx, ax
    mov al, [bx]
.store:
    mov bx, [fr_px]
    mov [fr_line+bx], al
    mov ax, [fr_step]
    add [fr_pcx], ax
    inc word [fr_px]
    mov ax, [fr_px]
    cmp ax, [fr_cw]
    jb .pixel
    ret

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
    dec di
    jz .interior
    jmp .loop                       ; rel16: the body is out of rel8 range
.interior:
    mov ax, FR_CAP
    ret

; -----------------------------------------------------------------------------
; fr_emit - take the lock, consume the computed row, release it
; in:  fr_line + the (pass, row) state; lock NOT held
; out: nothing; clobbers nothing (fr_emit_body preserves everything)
; -----------------------------------------------------------------------------
fr_emit:
    call OSAPI_GFX_LOCK
    call fr_emit_body
    call OSAPI_GFX_UNLOCK
    ret

; -----------------------------------------------------------------------------
; fr_emit_body - consume one computed row: cache it, paint it, step; the gfx
;                lock is HELD
; in:  fr_line, [fr_pass], [fr_row]; lock held
; out: nothing; preserves all registers
;
; Everything that means "this row was consumed" happens here, inside one lock
; hold, behind one restart check: the cache append, the progress count and
; fr_advance. That is the whole point of the routine. Outside the lock they
; race fr_redraw, which publishes a (pass, row) to resume from and would find
; a stale fr_advance had already stepped past it - leaving a canvas row no
; later pass paints and an fr_crow the cache can never match again.
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
; view's scanline as a band at the NEW state's row - a stale stripe across
; the top of a canvas fr_kick has just cleared, for a row time (~0.6 s on a
; 4.77 MHz 8088). Checking it under the lock makes test-and-paint atomic
; against the only writer.
;
; Worst case is 320 one-pixel runs (~80k clocks) against ~3M clocks to
; compute the row: under 3%, and typical run counts are around 40. That
; ratio is what keeps the cursor, the clock and every other window
; responsive while a frame renders.
; -----------------------------------------------------------------------------
fr_emit_body:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp word [fr_restart], 0        ; the view changed while we waited for
    jne .out                        ; the lock: this row is the old view's -
                                    ; do not cache it, paint it OR step past it
    cmp byte [fr_cfrom], 0
    jne .cached
    call fr_cache_row               ; computed: keep it, seen or not
    mov ax, [fr_cn]                 ; the cursor FOLLOWS the frontier, or the
    mov [fr_cpos], ax               ; next fr_take replays the row just
    inc word [fr_prog]              ; appended onto the row after it
    jmp short .vis
.cached:
    mov ax, [fr_ctlen]              ; replayed: step the cursor instead, here
    add [fr_cpos], ax               ; and not in fr_take, for fr_advance's
                                    ; reason - fr_redraw republishes the
                                    ; cursor with the row it names, and a step
                                    ; taken outside the lock walks past it.
                                    ; fr_prog counts COMPUTED rows only, which
                                    ; is what makes a repaint free of it
.vis:
    mov bx, [fr_win]
    or bx, bx
    jz .step
    call OSAPI_WM_GEOM              ; CF=1: not visible (SPEC.md 11). Was a
    jc .step                        ; [es:bx+W_FLAGS] test, which needed ES
                                    ; still pointing at the record inside the
                                    ; WORKER's frame - true, but only by
                                    ; convention, and one stray `mov es` away
                                    ; from reading a flag out of our own image
    call OSAPI_WM_CLIP_SET          ; how much of it shows? (SPEC.md 11.3)
    jc .step
    jmp short .draw
.step:
    jmp .adv
.draw:
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov [fr_ox], ax
    mov [fr_oy], dx
    mov bx, [fr_pass]               ; band bottom = row + extra, clipped to the
    mov al, [fr_bhtab+bx]           ; last canvas row
    mov ah, 0
    add ax, [fr_row]
    mov bx, [fr_ch]
    dec bx
    cmp ax, bx
    jle .b2
    mov ax, bx
.b2:
    add ax, [fr_oy]
    add ax, FR_STRIP_H
    mov [fr_by2], ax
    mov ax, [fr_row]
    add ax, [fr_oy]
    add ax, FR_STRIP_H
    mov [fr_by1], ax
    mov word [fr_p], 0
.run:
    mov bx, [fr_p]
    mov al, [fr_line+bx]
    mov [fr_col], al
    mov si, bx                      ; SI = last column of the run
.ext:
    mov di, si
    inc di
    cmp di, [fr_cw]
    jae .flush
    cmp al, [fr_line+di]
    jne .flush
    mov si, di
    jmp short .ext
.flush:
    mov al, [fr_col]
    call OSAPI_SET_COLOR
    mov ax, [fr_p]
    add ax, [fr_ox]
    mov cx, si
    add cx, [fr_ox]
    mov bx, [fr_by1]
    mov dx, [fr_by2]
    call OSAPI_GFX_FILL
    inc si
    mov [fr_p], si
    cmp si, [fr_cw]
    jb .run
    call fr_status_maybe
.adv:
    call fr_advance                 ; the row is consumed - step the state
                                    ; machine here, under the same lock hold
                                    ; and the same restart check that decided
                                    ; it was ours to consume
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fr_status_maybe - the PERCENTAGE, and only when it moved
; in:  [fr_prog], [fr_ch]; lock held, [fr_ox]/[fr_oy] valid
; out: nothing; preserves all registers
;
; At most 100 of these per frame instead of one per row. What each one costs is
; the change: it used to call fr_status, which white-fills the whole strip and
; re-letters all five fields - about 27 glyph cells and a fill, ~100 times a
; pass, to move a digit. PERFORMANCE.md prices a cell at ~1ms on the machine
; this app exists for, so that was seconds of drawing per pass, and the old
; comment here admitted the shape of it ("the strip would otherwise flicker
; white-then-text on every scanline") while still doing it a hundred times.
;
; Nothing but the percentage can change while a render runs: the type, the zoom
; and the palette all move through fr_kick, which calls fr_status itself. So
; this draws ONE field, as one OPAQUE run - SPEC.md 12.9's rule at the menu bar,
; 48.9.3's at Missile's banner and 56.12's at ModPlug's face, which is to say
; only the segment that changed may put pixels on a strip.
;
; Two things it no longer needs. There is no FILL, so the field is never
; momentarily blank - the padding IS the erase (SPEC.md 6.1), which is what
; removes the flicker rather than merely reducing it. And there is no
; wm_clip_test gate: font_run decides one cell at a time and cannot produce
; 11.3's granularity failure, so a clipped caller may draw through it
; unguarded, where fr_status has to ask about its whole rect because its fill
; and its glyphs clip differently.
; -----------------------------------------------------------------------------
fr_status_maybe:
    push ax
    call fr_pctcalc
    cmp al, [fr_pct]
    je .out
    mov [fr_pct], al
    call fr_status_pct
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; fr_pctcalc - the render progress as 0..100
; in:  [fr_prog], [fr_ch]
; out: AL = the percentage; every other register preserved
; Its own routine because fr_redraw wants the number without the comparison:
; after a repaint the whole strip has to go out whatever the number is, and
; that is fr_status, not this file's incremental path.
; -----------------------------------------------------------------------------
fr_pctcalc:
    push bx
    push dx
    mov ax, [fr_prog]
    mov bx, 100
    mul bx                          ; prog <= ch <= 460, so DX = 0 and the
    div word [fr_ch]                ; quotient 0..100 always fits AL
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; fr_status_pct - the percentage field alone: one opaque, space-padded run
; in:  [fr_pct] already stored; [fr_ox]/[fr_oy] valid; lock held
; out: nothing; preserves all registers
;
; The padding is what makes this safe to call without erasing first: the run is
; FR_PCT_CELLS wide whatever the number is, so the field carries its own erase.
; That is INSURANCE rather than a case this path reaches today - fr_prog only
; ever increments while a render runs, so the number only grows, and the one
; thing that lowers it (fr_redraw, resuming from the cache) draws the whole
; strip through fr_status instead. The padding costs four instructions and
; means nobody has to re-derive that argument before adding a field.
; -----------------------------------------------------------------------------
fr_status_pct:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, fr_numbuf
    mov al, [fr_pct]
    mov ah, 0
    call fr_u2s                     ; DI -> the NUL it wrote
    mov byte [di], '%'
    inc di
    mov bx, fr_numbuf               ; pad to the field width with spaces
    add bx, FR_PCT_CELLS
.pad:
    cmp di, bx
    jae .done
    mov byte [di], ' '
    inc di
    jmp short .pad
.done:
    mov byte [di], 0
    mov si, fr_numbuf
    mov cx, [fr_ox]
    add cx, FR_X_PCT
    mov dx, [fr_oy]
    add dx, FR_TXT_Y
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fr_status - the one-line strip: fractal name, zoom level, render progress,
;             palette
; in:  [fr_ox]/[fr_oy] valid; lock held
; out: nothing; preserves all registers
;
; The strip is a UNIT: it is white-filled and then written over with four
; strings, and those two operations do not clip alike (SPEC.md 11.3 - the
; fill goes per pixel, the glyphs per whole 8x8 cell). Cut horizontally by
; another window's edge, an ungated strip would erase the rows you can see
; and then decline to put any text back in them. So it asks first, whole
; rect, and leaves the old strip alone when the answer is no. Unclipped -
; every fr_kick and fr_redraw path - the test passes and nothing changes.
; -----------------------------------------------------------------------------
fr_status:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [fr_ox]                 ; the strip rect, inclusive
    mov bx, [fr_oy]
    mov cx, ax
    add cx, [fr_cw]
    dec cx
    mov dx, bx
    add dx, FR_STRIP_H-1
    call OSAPI_WM_CLIP_TEST
    jc .out
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [fr_ox]
    mov bx, [fr_oy]
    mov cx, ax
    add cx, [fr_cw]
    dec cx
    mov dx, bx
    add dx, FR_STRIP_H-1
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR

    mov ax, [fr_type]               ; the fractal's name
    mov cl, 4
    shl ax, cl
    mov si, fr_types
    add si, ax
    mov si, [si+FT_NAME]
    mov cx, [fr_ox]
    add cx, FR_X_NAME
    mov dx, [fr_oy]
    add dx, FR_TXT_Y
    call OSAPI_FONT_STR

    mov si, fr_s_zoom               ; 'Zoom' + the exponent 0..4
    mov cx, [fr_ox]
    add cx, FR_X_ZOOM
    mov dx, [fr_oy]
    add dx, FR_TXT_Y
    call OSAPI_FONT_STR
    mov al, [fr_z]
    add al, '0'
    mov cx, [fr_ox]
    add cx, FR_X_ZNUM
    mov dx, [fr_oy]
    add dx, FR_TXT_Y
    call OSAPI_FONT_CHAR

    mov di, fr_numbuf               ; the render progress, 0..100%
    mov al, [fr_pct]
    mov ah, 0
    call fr_u2s                     ; DI -> the NUL it wrote
    mov byte [di], '%'
    mov byte [di+1], 0
    mov si, fr_numbuf
    mov cx, [fr_ox]
    add cx, FR_X_PCT
    mov dx, [fr_oy]
    add dx, FR_TXT_Y
    call OSAPI_FONT_STR

    mov bx, [fr_pal]                ; the palette, named by its own menu item
    shl bx, 1
    mov si, [fr_mi_col+bx]
    mov cx, [fr_ox]
    add cx, FR_X_PAL
    mov dx, [fr_oy]
    add dx, FR_TXT_Y
    call OSAPI_FONT_STR
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fr_clear - white-fill the canvas (the content below the status strip)
; in:  [fr_ox]/[fr_oy], [fr_cw], [fr_ch]; lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
fr_clear:
    push ax
    push bx
    push cx
    push dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [fr_ox]
    mov cx, ax
    add cx, [fr_cw]
    dec cx
    mov bx, [fr_oy]
    add bx, FR_STRIP_H
    mov dx, bx
    add dx, [fr_ch]
    dec dx
    call OSAPI_GFX_FILL
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fr_u2s - unsigned AX (0..999) to decimal at [DI], NUL-terminated
; in:  AX = value, DI = buffer
; out: DI advanced to the NUL it wrote; all other registers preserved
; -----------------------------------------------------------------------------
fr_u2s:
    push ax
    push bx
    push cx
    push dx
    xor cx, cx
    mov bx, 10
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
    mov byte [di], 0
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
        OS88_MENU fr_m_frac, fr_mi_frac, 5
        OS88_MENU fr_m_col,  fr_mi_col,  4
        OS88_MENU fr_m_view, fr_mi_view, 4
    OS88_MENUSET_END fr_menus

fr_m_frac:   db 'Fractal', 0
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
fr_s_zoom:     db 'Zoom', 0
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

; band height added to the top row, per progressive pass (fr_advance)
fr_bhtab:    db 3, 1, 0

; --- palettes: escape count -> EGA colour index, 48 entries each ---------------
; The interior is CBLACK in every palette - a separate constant, not entry 0 -
; so each ramp is free to start bright. 42% of Mandelbrot pixels escape in
; under 4 iterations, which is why none of the four opens on a dark colour
; that would merge with the set.
fr_pals:     dw fr_pal_spectrum, fr_pal_fire, fr_pal_ice, fr_pal_contour

; Spectrum: a 12-hue wheel, four times round.
fr_pal_spectrum:
    db  1, 9,11, 3,10, 2,14, 6,12, 4,13, 5
    db  1, 9,11, 3,10, 2,14, 6,12, 4,13, 5
    db  1, 9,11, 3,10, 2,14, 6,12, 4,13, 5
    db  1, 9,11, 3,10, 2,14, 6,12, 4,13, 5

; Fire: dark -> red -> orange -> yellow -> white -> back, twice.
fr_pal_fire:
    db  8, 8, 4, 4, 4,12,12,12, 6, 6,14,14
    db 15,15,14,14, 6, 6,12,12, 4, 4, 8, 8
    db  8, 8, 4, 4, 4,12,12,12, 6, 6,14,14
    db 15,15,14,14, 6, 6,12,12, 4, 4, 8, 8

; Ice: dark -> blue -> cyan -> white -> back, twice.
fr_pal_ice:
    db  8, 8, 1, 1, 1, 9, 9, 9, 3, 3,11,11
    db 15,15,11,11, 3, 3, 9, 9, 1, 1, 8, 8
    db  8, 8, 1, 1, 1, 9, 9, 9, 3, 3,11,11
    db 15,15,11,11, 3, 3, 9, 9, 1, 1, 8, 8

; Contour: the 1bpp-safe one. SPEC.md 39.4 collapses 16 colours into three
; classes (0..6 black, 7/8/9/10/11/13 dither, 12/14/15 white); this ramp uses
; ONLY white and dither, in strict runs of four. Twelve evenly spaced contour
; bands whose every boundary is a white<->dither transition, and - uniquely
; among the four - no ramp entry is ever black, so on a mono screen the
; interior is the only black region and the set reads as a solid silhouette.
fr_pal_contour:
    db 15,15,14,14, 7, 7,11,11,12,12,15,15
    db  9, 9,13,13,14,14,12,12,11,11, 7, 7
    db 15,15,14,14,13,13, 9, 9,12,12,15,15
    db  7, 7,11,11,14,14,12,12, 9, 9,13,13

    OS88_BSS FR_BSS_TOTAL
    OS88_IMAGE_END

; --- loader-zeroed bss (SPEC.md 21 step 5) -------------------------------------
; All zero is type 0 (Mandelbrot), palette 0 (Spectrum), zoom 0, no worker -
; but NOT the Mandelbrot's centre, which fr_entry loads via fr_defaults.
fr_ox      equ os88_image_end + 0    ; word: content left (re-read per call)
fr_oy      equ os88_image_end + 2    ; word: content top
fr_win     equ os88_image_end + 4    ; word: our window ptr (spawn + worker)
; --- the view state: four words, and everything else derives from them
fr_type    equ os88_image_end + 6    ; word: 0..4, index into fr_types
fr_pal     equ os88_image_end + 8    ; word: 0..3, index into fr_pals
fr_z       equ os88_image_end + 10   ; word: zoom exponent 0..FR_ZMAX
fr_cenx    equ os88_image_end + 12   ; word: Q4.12 centre
fr_ceny    equ os88_image_end + 14   ; word
; --- derived once per frame by fr_setup
fr_cw      equ os88_image_end + 16   ; word: canvas width  (content width)
fr_ch      equ os88_image_end + 18   ; word: canvas height (content less strip)
fr_step0   equ os88_image_end + 20   ; word: span / cw
fr_step    equ os88_image_end + 22   ; word: step0 >> z, floored at 1
fr_x0      equ os88_image_end + 24   ; word: complex coord of canvas column 0
fr_y0      equ os88_image_end + 26   ; word: ...and of canvas row 0
fr_cx      equ os88_image_end + 28   ; word: c for the core (Julia: constant;
fr_cy      equ os88_image_end + 30   ; word:  Mandelbrot-type: the pixel)
fr_palp    equ os88_image_end + 32   ; word: the live 48-byte palette
; --- the render state machine
fr_pcx     equ os88_image_end + 34   ; word: running pixel coord, this row
fr_pcy     equ os88_image_end + 36   ; word
fr_px      equ os88_image_end + 38   ; word: column counter, this row
fr_pass    equ os88_image_end + 40   ; word: 0/1/2 progressive pass, 3 = done
fr_row     equ os88_image_end + 42   ; word: canvas row being computed
fr_prog    equ os88_image_end + 44   ; word: rows computed this frame
fr_restart equ os88_image_end + 46   ; word: UI -> worker "start over"
fr_by1     equ os88_image_end + 48   ; word: emit scratch - band top (screen)
fr_by2     equ os88_image_end + 50   ; word: emit scratch - band bottom
fr_p       equ os88_image_end + 52   ; word: emit scratch - run start column
fr_flg     equ os88_image_end + 54   ; byte: FF_* for the live type
fr_spawned equ os88_image_end + 55   ; byte: 1 = this instance owns its worker.
                                     ; Latches on SUCCESS only: while it is 0
                                     ; every kick retries the spawn, so a slot
                                     ; freed later is picked up (SPEC.md 20.6)
fr_pct     equ os88_image_end + 56   ; byte: last percentage drawn
fr_col     equ os88_image_end + 57   ; byte: emit scratch - the run's colour
fr_numbuf  equ os88_image_end + 58   ; 6 bytes: "100%" + NUL
fr_line    equ os88_image_end + 64   ; 320 bytes: one colour index per column
; --- the restore cache (see the file header). The runs themselves are a HEAP
; claim that regrows, not bss: a busy view wants 32KB and a plain one 340
; bytes, and a package that reserved the larger in its region would be
; refused a launch on a machine that can run it perfectly well with the
; smaller - or with no cache at all.
fr_cseg    equ os88_image_end + 384  ; word: the claim, or 0 = we have none
fr_cmax    equ os88_image_end + 386  ; word: its size in BYTES
fr_cn      equ os88_image_end + 388  ; word: BYTES of it in use, not runs
fr_cnrow   equ os88_image_end + 390  ; word: ROWS in it - what fr_prog becomes
                                     ; across a repaint
fr_cpass   equ os88_image_end + 392  ; word: the frontier - the (pass, row) the
fr_crow    equ os88_image_end + 394  ; word:  cache is waiting to be handed
fr_c0n     equ os88_image_end + 396  ; word: BYTES of the pass-0 prefix, which
                                     ; is the part fr_redraw replays inline
fr_c0row   equ os88_image_end + 398  ; word: the next pass-0 row not cached
                                     ; (>= ch once pass 0 is all in there)
fr_cpos    equ os88_image_end + 400  ; word: the worker's replay cursor
fr_ctlen   equ os88_image_end + 402  ; word: bytes of the row fr_take decoded
fr_ccw     equ os88_image_end + 404  ; word: the canvas the cache was built for
fr_cch     equ os88_image_end + 406  ; word:  - a mismatch invalidates it
fr_crrow   equ os88_image_end + 408  ; word: fr_replay scratch - the row being
                                     ; blitted back
fr_cfrom   equ os88_image_end + 410  ; byte: 1 = fr_line was REPLAYED, not
                                     ; computed
fr_ckb     equ os88_image_end + 412  ; word: the claim's size in KB, which is
                                     ; what doubles; total 414 = FR_BSS_TOTAL

; The percentage field's padding is written into fr_numbuf, and fr_numbuf's
; size is the gap to the next bss symbol rather than a declaration - these are
; hand-computed offsets, so nothing but this check stands between a wider field
; and FR_PCT_CELLS silently overwriting the first byte of fr_line.
%if FR_PCT_CELLS + 1 > fr_line - fr_numbuf
  %error "FR_PCT_CELLS + a NUL does not fit fr_numbuf - widen the gap to fr_line"
%endif
