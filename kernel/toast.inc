; =============================================================================
; os8088 - toast.inc (SPEC.md 59)
;
; THE TRANSIENT ONE-LINE MESSAGE, for the whole machine: `Saved NOTES.TXT`,
; `Opened SUNSET.BMP`. One routine, one look, one lifetime - where five
; applications had each re-rolled their own and no two agreed about where it
; went, how long it lived or who erased it.
;
; It is NOT ui_note (SPEC.md 54.4.1). That is a WINDOW, for a failure the user
; has to acknowledge, and it stays until it is dismissed. This is for an
; operation that just finished, usually successfully, that nobody needs to
; acknowledge - so it says its piece and goes away on its own. A notice you
; can miss is not a notice; a toast you have to dismiss is a dialog.
;
; **IT LIVES IN THE MENU BAR, IN THE CLOCK'S FIELD** (SPEC.md 59.8) - and
; that placement is the whole design rather than a decoration. Two properties
; of that strip of screen do all the work:
;
;   - **the bar can never be covered.** Windows clamp to y >= MBAR_H. So
;     there is no clip region here, no occlusion test, no interaction with
;     WF_SAVEU or the raise cache (SPEC.md 11.96), and none of SPEC.md 11.3's
;     granularity rule. A toast drawn over a window's CONTENT would need all
;     four, and would need every application told about them.
;   - **the bar has ONE painter.** menu_draw_bar is the only thing in the
;     machine that draws there, so a toast on it cannot diverge between an
;     incremental path and a full repaint - there is no second path to
;     diverge from. That divergence is exactly what Note Pad's own toast
;     had: it cleared [np_msg] as it drew (or a drag re-showed
;     `Loaded README.TXT`), so a full repaint did not put it back, and the
;     only thing reconciling the two was np_sigsame refusing the fast path
;     and repainting the whole content on the next keystroke - a full
;     np_paint, tens of milliseconds, after every save and every load.
;
; **IT TAKES THE CLOCK'S FIELD AND NOT THE MENUS'** (SPEC.md 59.8), and that
; is a correction rather than the original design. The strip borrowed the
; right end of the MENUS segment, where menu_bput's clamp dropped whatever it
; covered - so a message long enough took the frontmost application's own
; menu titles off the bar with it, which reads as the application breaking
; rather than as a message arriving. The clock is the right neighbour to
; displace for a reason that is about KIND and not about room: it is the one
; thing on the bar that is not a control, it says the same thing again a
; second later, and a menu title is the user's way into the program. So the
; layering is now settled in the geometry - there is no message this can
; write that reaches a menu - rather than by keeping every string short
; enough to hope.
;
; **AND IT HIDES ONLY THE CELLS IT COVERS.** The strip is as wide as its
; message plus one inverted space at each end and no wider, it takes the LEFT
; end of the field, and menu_draw_clock stages the clock into the whole field
; before laying the strip over it - so a short message lands entirely in the
; time's leading spaces and hides nothing at all, and a long one costs the
; DATE, which is the end worth losing. The clock is never suppressed while a
; message is up: it is simply not what those particular cells say.
;
; And the ERASE is written by the same painter (SPEC.md 12.9's rule at the
; other end of the bar), so a toast expiring lets the time straight back and
; nothing here has to remember to restore anything. There is no fill in this
; file, no menu_inval, and no drawing routine at all.
;
; TWO ENTRY POINTS FOR CALLERS and they are on opposite sides of a lock:
; toast_show STAGES and never draws, toast_pass (the UI task's idle pass)
; draws. That is what makes the context rule `any` - a window callback holds
; the gfx lock, a worker holds nothing, kernel file code holds it too, and
; gfx_lock is not reentrant. Deferring gives ONE drawing site, which is also
; the ONE expiry site, and puts it where this tree already spends deferred
; flags ([fm_fchk], [cp_dirty], [desk_zdirty]). The cost is up to one pass of
; latency - a tick - on a three-second message.
;
; The string is COPIED. Not an optimisation: a toast outlives the operation
; that raised it by design and the raiser may CLOSE inside that window, so a
; retained pointer is a dangling far pointer the bar would letter from.
;
; The flags live in .text with real initialisers, NOT .bss, which -f bin
; zeroes not at all: menu_draw_bar runs during drv_boot, before any init
; routine of ours could have run, and a garbage [toast_on] would compose a
; strip out of whatever was in memory. [fpg_on]'s reasoning exactly. The
; BUFFER may be .bss, because it is read only when [toast_on] is set and
; only a completed toast_show can set it - and .bss is off the image rung.
;
; Prefix toast_.
; =============================================================================

section .text

TOAST_MAX   equ 24              ; characters, AND IT IS THE TIGHT ONE (SPEC.md
                                ; 59.8). The clock's field is ([vid_wm8] -
                                ; [vid_clk_hx]) / 8 = 200/8 = 25 cells on
                                ; every screen this runs on - 640 and 720 both
                                ; subtract the same 206 and round to the same
                                ; 8 - and the strip spends ONE of them on the
                                ; gap that keeps its bed off SPEC.md 12.8's
                                ; widget (59.8.1), so 24 is what is left. It
                                ; was 23 while both ends cost a whole cell. It was
                                ; 40 while this borrowed the menus segment's
                                ; 50, and shortening it is what made the move
                                ; safe: every message in the tree was revised
                                ; to fit rather than left to truncate
TOAST_TICKS equ 55              ; ~3.0 s at 18.2 Hz: long enough to read eight
                                ; words on a 4.77MHz machine, short enough
                                ; that it is gone before it becomes furniture
TOAST_SCAN  equ 256             ; bytes of the caller's string examined before
                                ; the copy gives up. Generous against any real
                                ; message (the buffer keeps 23) and the whole
                                ; of the defence: the scan runs interrupts-off
                                ; through a package-supplied pointer, and
                                ; without a byte bound a segment holding no
                                ; NUL and no printable would hang the machine

toast_on:    db 0               ; a strip is composed into the bar
toast_want:  db 0               ; staged by toast_show, not yet put up
toast_dirty: db 0               ; the composition changed and nobody has
                                ; drawn it - set by the two edges AND by
                                ; toast_kill, which runs under the lock and
                                ; so may not draw for itself
toast_inst:  db 0FFh            ; the raiser's instance slot (0FFh = kernel)
toast_die:   dw 0               ; the tick it expires at
toast_ttl:   dw TOAST_TICKS     ; ...and how long it was asked for
toast_scell: dw 0               ; the strip's first cell in the MENUS segment,
toast_scn:   dw 0               ; and how many cells it takes there - 0 meaning
                                ; the CLOCK's field has it instead (SPEC.md
                                ; 59.9). In .text with real initialisers for
                                ; [toast_on]'s reason: menu_draw_clock reads
                                ; [toast_scn] to decide whether its own field
                                ; owes a strip, and menu_draw_bar runs during
                                ; drv_boot, before any init routine of ours

; -----------------------------------------------------------------------------
; toast_show - say one line in the menu bar for a few seconds (API 0x0380)
; in:       ES:SI = a NUL string, CX = ticks to live (0 = TOAST_TICKS)
; out:      CF = 0 taken, CF = 1 refused - and NOTHING refuses today, the
;           widget interlock below having been the only one. The flag stays in
;           the published contract because a refusal that has to be added
;           later must not be an ABI change (SPEC.md 20.8)
; clobbers: nothing but the flags
;
; ES:SI and not an X stub, for clip_put's reason (SPEC.md 55): the text an
; app wants to say is very often not in the app's own image - a file name
; taken out of a heap-claimed document is not reachable through the caller's
; DS. A far pointer the caller builds costs one `mov es` and reaches
; anything.
;
; AN EMPTY STRING IS A RETIRE and not an error, clip_put's CX = 0 exactly:
; an app that wants to take its message down early has one call to make and
; does not have to know whether one is up.
;
; **IT NO LONGER DEFERS TO THE FILE-ACTIVITY WIDGET** (SPEC.md 12.8), and
; that is still right, but NOT for the reason this paragraph used to give. It
; used to refuse outright while [fpg_on] was set, and fpg_begin retired
; whatever was up - both of which were about PIXELS, the widget sitting at
; [vid_clk_hx] - FPG_W and the strip reaching left into the same cells.
; Dropping them is the point rather than a tidy-up: a refusal here loses the
; message for good, silently, and the widget is armed by exactly the mounts
; and reads that the messages worth saying are about.
;
; What replaced them was the claim that the strip lives in the clock's field,
; wholly RIGHT of the widget's right edge, "so the two cannot touch". SPEC.md
; 59.9 then gave the strip a second home - the gap the menus leave - and that
; gap's right end is precisely where the widget is drawn, so they touched
; again and nobody re-read this. They are kept apart by GEOMETRY now
; (toast_place reserves the widget's cells, SPEC.md 59.9.2) rather than by a
; property of where the strip happens to live.
;
; The stage runs with interrupts off: a worker task and the UI task can both
; raise, and a half-copied buffer would letter as garbage. 23 bytes of
; rep-less movsb is ~475 clocks, ~100 us against a 55 ms tick.
; -----------------------------------------------------------------------------
toast_show:
    push ax
    push cx
    push dx
    push si
    push di
    pushf
    cli
    or cx, cx
    jnz .ttl
    mov cx, TOAST_TICKS
.ttl:
    cmp cx, 0x7FFF              ; toast_pass measures expiry with a SIGNED
    jbe .ttlok                  ; subtract (the modular-clock compare), so a
    mov cx, 0x7FFF              ; TTL past 32767 ticks would read as already
.ttlok:                         ; over and the toast would die on first look
    mov [toast_ttl], cx
    mov di, toast_buf
    mov cx, TOAST_MAX
    mov dx, TOAST_SCAN
.cp:
    mov al, [es:si]
    or al, al
    jz .end
    inc si
    dec dx                      ; the OTHER bound: CX counts characters KEPT,
    jz .end                     ; so a string that never offers a printable
                                ; byte or a NUL - and every byte from a
                                ; package is hostile - would spin this loop
                                ; around its segment forever, interrupts off
    cmp al, ' '                 ; a control character would letter as a glyph
    jb .cp                      ; rather than doing anything, and every byte
    cmp al, 0x7F                ; from a package is hostile, so only printable
    jae .cp                     ; ASCII is taken
    mov [di], al
    inc di
    loop .cp
.end:
    mov byte [di], 0
    cmp di, toast_buf
    je .kill                    ; nothing sayable: this is a retire
    call inst_caller            ; DH = who is asking (SPEC.md 19.2.1), so a
    mov [toast_inst], dh        ; closing app takes its own message with it
    mov byte [toast_want], 1
    popf
    call toast_now              ; ...and if the caller can be shown to hold
    clc                         ; the lock, it reaches the glass NOW rather
    jmp short .out              ; than on the next idle pass (SPEC.md 59.4)
.kill:
    call toast_kill             ; preserves the flags, so the popf below is
    popf                        ; still the caller's
    clc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; toast_say - say entry AL of a word table of NUL strings
; in:       AL = index (0 = nothing to report, which is how every one of these
;           tables spells it), BX = the table, CX = its length in entries
; out:      nothing - all registers AND the flags preserved
;
; The kernel's own verdict tables (`fm_errtab`, `fm_stattab`) are arrays of
; string pointers indexed by a FERR_*/LD_* code, and every caller of this was
; open-coding the same bound test and shift before storing the code in a
; per-window byte for a status line to find later. It says it instead.
;
; The bound test is not decoration: `LD_EBAD` walking off the end of a table
; is what once made every rung of the Disk window's status ladder go silent
; (SPEC.md 22.9), and an out-of-range code here would letter whatever the word
; after the table happens to be.
; -----------------------------------------------------------------------------
toast_say:
    pushf
    push ax
    push bx
    push cx
    push si
    push es
    xor ah, ah
    or al, al
    jz .out
    cmp ax, cx
    jae .out
    shl ax, 1
    add bx, ax
    mov si, [bx]                ; the tables are .text, so DS reaches them
    push ds                     ; from cold code as well as from here
    pop es
    xor cx, cx
    call toast_show
.out:
    pop es
    pop si
    pop cx
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; toast_now - draw a staged toast on the spot, IF the caller provably holds
;             the gfx lock (SPEC.md 59.4)
; in:       a toast staged; called from toast_show only
; out:      nothing (all registers preserved)
;
; A message put up BEFORE a long operation has to be on the glass before the
; machine goes quiet - Paint says `Encoding...` and then spends seconds on
; 125,000 pixels of LZW with nothing else on screen, and staging alone would
; show it after the thing it announces. The idle pass cannot help: that
; operation runs INSIDE a window callback on the UI task, so ui_task's loop
; does not come round again until it is over.
;
; **The test is the lock's OWNER against the running task.** [gfx_lock_flag]
; alone says the lock is held by SOMEBODY, not by the caller - and "the UI
; task is running and the flag is set" does not close that gap either, the
; way this first shipped believing: gfx_lock does `sti` after the take, so a
; WORKER holding the lock can be pre-empted mid-draw and the UI task can run
; a path that raises without the lock (the Control Panel's flush-on-reboot
; is one today). So gfx_lock stamps [gfx_lock_own] with [sch_cur] at the
; take, and this draws only when the task asking IS the task named - which
; is a fact, not an inference. Anybody else takes the staged path and is one
; tick late, which is right for a background job and costs nothing.
; -----------------------------------------------------------------------------
toast_now:
    push ax
    cmp byte [gfx_lock_flag], 0
    je .outa
    cmp byte [sch_cur], 0       ; task 0 is ui_task (kernel.asm's kmain tail)
    jne .outa
    cmp byte [gfx_lock_own], 0  ; ...and the UI task is the HOLDER, not a
    jne .outa                   ; bystander running while a pre-empted worker
                                ; owns the glass
    mov al, [gfx_color]         ; the caller is mid-callback: its pen and its
    push ax                     ; clip region are ITS, and the bar is inside
    mov ax, [wm_clip_n]         ; no window's region ever - drawn through an
    push ax                     ; armed one the strip's cells would be dropped
    mov word [wm_clip_n], 0     ; on the glass while menu_bcell records them
                                ; composed, and no later pass would repair it
    call toast_arm
    call menu_draw_bar
                                ; carry it to VRAM does not come until the
                                ; operation this announces is over (SPEC.md
                                ; 32) - fpg_begin's reasoning exactly
    pop ax
    mov [wm_clip_n], ax
    pop ax
    mov [gfx_color], al
.outa:
    pop ax
    ret

; -----------------------------------------------------------------------------
; toast_arm - spend [toast_want]: the strip goes up
; in/out:   nothing (AX clobbered - both callers have banked it)
; -----------------------------------------------------------------------------
toast_arm:
    mov byte [toast_want], 0
    mov ax, [ticks]
    add ax, [toast_ttl]
    mov [toast_die], ax
    mov byte [toast_on], 1
    mov byte [menu_bdirty], 1   ; **AND THE MENUS SEGMENT IS OWED A LOOK**
                                ; (SPEC.md 59.9): the strip's first choice of
                                ; home is the gap that segment's last menu
                                ; leaves, and menu_bar_text - the only thing
                                ; that composes there - runs on this flag
                                ; alone. It is set on every edge and not only
                                ; when the strip lands in the gap, because
                                ; which home it takes is toast_place's answer
                                ; and toast_place runs INSIDE the pass this
                                ; asks for. The cost of being wrong is one
                                ; composition that finds nothing differs and
                                ; emits no pixels, which is what this segment
                                ; answers most window operations with anyway
    mov byte [toast_dirty], 0   ; ...and this IS the draw, on the toast_now
                                ; path; toast_pass re-sets it and draws.
                                ; NOTHING is invalidated in the CLOCK's field:
                                ; its painter composes what it is about to
                                ; show and compares it against [menu_ckck]
                                ; plus [menu_ckstrip], so it finds the
                                ; difference by itself (SPEC.md 59.8)
    ret

; -----------------------------------------------------------------------------
; toast_gapend - the gap's right edge as the EYE sees it: the cell the clock's
;                first GLYPH lands in (SPEC.md 59.9.1)
; in:       nothing
; out:      AX = that cell, in the MENUS segment's coordinates - so it is
;           [menu_bn] plus however many of the clock field's cells are the
;           leading spaces menu_draw_clock right-aligns the line with
; clobbers: nothing but AX (flags)
;
; The clock's field is a fixed 25 cells and the line in it is NOT: clk_fmt
; answers 18..24 glyphs (12-hour mode drops the hour's leading zero, seconds
; are a setting), which menu_draw_clock right-aligns by leading spaces. So the
; field's left edge is where the clock's ROOM starts and up to seven cells of
; white follow it before anything is drawn - and centring a strip on the room
; puts it visibly left of centre, because the eye measures to the `J` of `Jul`.
;
; clk_fmt is cheap enough to ask twice per bar paint: clk_snapshot reads the
; kernel's own RAM shadows of the clock (IRQ0 keeps those; SPEC.md 37), so
; there is no port I/O and no chip access here at all.
; -----------------------------------------------------------------------------
toast_gapend:
    push bx
    push cx
    push dx
    push si
    mov ax, [vid_wm8]           ; the field, in whole cells - menu_draw_clock's
    sub ax, [vid_clk_hx]        ; own arithmetic, and clamped the same way
    mov cl, 3
    shr ax, cl
    cmp ax, MENU_CLKC - 1
    jbe .fw
    mov ax, MENU_CLKC - 1
.fw:
    mov dx, ax                  ; DX = the field's cells
    call clk_fmt                ; SI = the live line (all registers but SI)
    xor cx, cx
.l:
    cmp byte [si], 0
    je .ld
    inc si
    inc cx
    jmp short .l
.ld:
    cmp cx, dx                  ; a line wider than its field cannot happen,
    jbe .pad                    ; but a negative pad below would put the gap's
    mov cx, dx                  ; end LEFT of the segment
.pad:
    mov ax, dx
    sub ax, cx                  ; AX = the leading spaces = the cells of the
    add ax, [menu_bn]           ; field that are blank before the first glyph
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; toast_place - decide where the strip goes: the GAP, or the clock's field
;               (SPEC.md 59.9)
; in:       nothing. Called from menu_bar_text after the menus are composed,
;           so [menu_bend] is THIS bar's last menu and not the previous
;           owner's
; out:      [toast_scn]   = cells of strip the MENUS segment holds, 0 = none,
;                           which is menu_draw_clock's cue to take it
;           [toast_scell] = its first cell there (0 = x MENU_TXT_X0)
; clobbers: nothing (flags only)
;
; **THE GAP FIRST, BECAUSE A STRIP THERE COVERS NOTHING AT ALL.** The bar's
; menus end wherever the active application's last title ends, and everything
; from there to the clock is blank white - so a message that fits between them
; costs neither a menu title (SPEC.md 59.8's whole subject) nor a clock cell
; (what 59.8 settled for). It is the one placement with no victim, and on an
; ordinary desktop it is available: measured, Locator leaves a **24-cell** gap
; on a 640px screen and **34** on a 720px one, so `Settings Saved` (16 cells)
; and `Opened SUNSET.BMP` (19) both land in it with room either side.
;
; **CENTRED means the strip's MIDPOINT on the gap's midpoint**, not its left
; edge at the gap's start - and the gap's right end is the clock's first GLYPH
; rather than its field (toast_gapend, 59.9.1). Half the slack goes at each
; end; `shr` floors, so an odd cell lands on the right. Left-aligned it reads
; as a tail on the menus - another menu title, at the exact pen a fifth one
; would have had - and right-aligned it reads as a prefix to the clock. Centred
; it reads as neither, which is what a message that belongs to the machine
; rather than to either neighbour should look like.
;
; **AND IT FALLS BACK TO THE CLOCK, NOT TO A TRUNCATION.** A message too wide
; for the gap goes where 59.8 put it - the clock field's left end, eating the
; date before the time - because the alternative that looks natural is worse
; in both directions: shortening the message to fit whatever gap this
; application happens to leave makes the text depend on the menu bar, and
; letting it start in the gap and RUN ON into the clock straddles two
; separately composed buffers with two separate check words, which is two
; painters for one strip and exactly the divergence 59.1 exists to avoid.
;
; So the gap is a bonus and never a constraint: TOAST_MAX still measures
; against the clock's field, every message in the tree still fits there, and
; what this section adds is that the ones which fit the gap cost nothing.
; -----------------------------------------------------------------------------
toast_place:
    push ax
    push bx
    push cx
    push dx
    mov word [toast_scn], 0
    cmp byte [toast_on], 0
    je .out
    mov bx, toast_buf           ; the strip's width in cells: the message, plus
    xor cx, cx                  ; the one inverted space at each end
.l:
    cmp byte [bx], 0
    je .ld
    inc bx
    inc cx
    jmp short .l
.ld:
    jcxz .out                   ; nothing sayable. toast_show cannot stage that
    inc cx                      ; (an empty string is a retire), but reading the
                                ; buffer rather than trusting the flag is what
                                ; keeps the two decs below off a negative count
    mov dx, [menu_bn]           ; **DX = ONE PAST THE LAST CELL THE STRIP MAY
                                ; TAKE**, which is the end of the segment -
                                ; and that already accounts for SPEC.md 12.8's
                                ; widget (59.9.2), because menu_bar_text takes
                                ; FPG_CELLS off [menu_bn] itself while it is
                                ; armed (SPEC.md 12.8.3). This used to
                                ; subtract them here, which was the only
                                ; reader that needed to; the widget outliving
                                ; its operation made a whole composition able
                                ; to run underneath it, so the reservation
                                ; moved to where every reader of the segment
                                ; sees it and this line lost its second
                                ; opinion about where the widget is
    mov ax, dx                  ; the GAP: the cells between the end of the last
    sub ax, [menu_bend]         ; drawn menu and whatever is on our right
    jbe .out                    ; the menus fill the segment: no gap at all
    cmp ax, cx
    jb .out                     ; will not fit - the clock's field takes it and
                                ; costs the date, exactly as it did before this
                                ; section existed (SPEC.md 59.8). That fallback
                                ; is what makes the widget's reservation free:
                                ; the field is wholly RIGHT of [vid_clk_hx] and
                                ; the widget wholly left of it, so the message
                                ; is still said in full and still covers neither
                                ; a menu title nor the widget

    call toast_gapend           ; --- and CENTRED on what the EYE sees, which
    cmp byte [fpg_on], 0        ; is the clock's first GLYPH and not its field's
    je .eye                     ; left edge (SPEC.md 59.9.1). gapend's answer is
    cmp ax, dx                  ; [menu_bn] PLUS the leading spaces, so clamping
    jbe .eye                    ; it to DX unconditionally would discard the
    mov ax, dx                  ; whole correction on the ordinary bar and centre
.eye:                           ; on the room again - the clamp belongs to the
                                ; armed widget alone, which IS our right-hand
                                ; neighbour then and sits left of the glyph
                                ; (59.9.2)
    sub ax, [menu_bend]
    sub ax, cx                  ; AX = the slack, half of it each side
    jns .half
    xor ax, ax                  ; a clock so wide it reaches the menus: flush
.half:                          ; left, and the clamp below is what keeps it in
    shr ax, 1
    add ax, [menu_bend]
    mov bx, dx                  ; **THE ROOM IS THE SEGMENT'S EVEN THOUGH THE
    sub bx, cx                  ; CENTRING IS THE EYE'S**: the strip is composed
    cmp ax, bx                  ; into menu_bcell, where menu_bput DROPS a cell
    jbe .set                    ; at or past [menu_bn] without advancing - so a
    mov ax, bx                  ; centre computed past that boundary would
.set:                           ; silently truncate the strip's right end
    mov [toast_scell], ax
    mov [toast_scn], cx
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; toast_pass - put a toast up, or take an expired one down
; in:       UI task, NO lock held
; out:      nothing (all registers preserved)
;
; One compare per idle pass when nothing is up, which is nearly always -
; fpg_step's bargain.
; -----------------------------------------------------------------------------
toast_pass:
    push ax
    cmp byte [toast_want], 0
    jne .up
    cmp byte [toast_on], 0
    je .chk
    mov ax, [ticks]
    sub ax, [toast_die]
    js .chk                     ; MODULAR arithmetic between two free-running
                                ; words, so the SIGN of the difference
                                ; decides - sch_isr's wake scan and SPEC.md
                                ; 45.15's stamp compare. A `jg` honours OF
                                ; and would hold a toast across the wrap
    mov byte [toast_on], 0
    mov byte [toast_dirty], 1
    jmp short .chk
.up:
    call toast_arm
    mov byte [toast_dirty], 1
.chk:
    cmp byte [toast_dirty], 0
    je .out
    mov byte [toast_dirty], 0
    mov byte [menu_bdirty], 1   ; the DOWN edges' half of toast_arm's flag
                                ; (SPEC.md 59.9) - an expiry above, and
                                ; toast_kill's deferred retire, both of which
                                ; may have a strip to erase out of the menus
                                ; segment and only menu_bar_text can do it
    call gfx_lock               ; menu_draw_bar always ends in the clock's
    call menu_draw_bar          ; field, which IS the draw and IS the erase
    call gfx_unlock             ; (SPEC.md 12.9/59.8) - and going through the
.out:                           ; bar rather than straight to the field is
                                ; what keeps wm_fs_vis's guard in front of it:
                                ; a fullscreen window is over these pixels and
                                ; the message waits for it to end
    pop ax
    ret

; -----------------------------------------------------------------------------
; toast_kill - retire whatever is up, now
; in:       anything, lock held or not
; out:      nothing - all registers AND THE FLAGS preserved
;
; Does not draw. Two of its three callers hold the gfx lock (fpg_begin,
; inst_free's teardown) and one is inside toast_show's cli window; the pixels
; go back on toast_pass's next look, which is a tick away at worst.
; -----------------------------------------------------------------------------
toast_kill:
    pushf
    cli                         ; check-then-act: a task switch between the
                                ; tests and the stores could retire a toast
                                ; some OTHER task staged in the gap
    cmp byte [toast_on], 0
    jne .go
    cmp byte [toast_want], 0
    je .out
.go:
    mov byte [toast_want], 0
    mov byte [toast_on], 0
    mov byte [toast_dirty], 1
.out:
    popf
    ret

; -----------------------------------------------------------------------------
; toast_owner_gone - a closing instance takes its message with it
; in:       DI = the record being freed (snd_release_rec's contract, and it
;           is called beside it at both teardown sites so the two cannot
;           drift apart)
; out:      nothing - all registers and the flags preserved
;
; Not needed for CORRECTNESS - the string was copied, so nothing dangles -
; but `Saved NOTES.TXT` sitting over a bar that now belongs to Locator is a
; message about a window that is not there any more.
; -----------------------------------------------------------------------------
toast_owner_gone:
    pushf
    cli                         ; same check-then-act window as toast_kill's:
                                ; the instance compare must not race a fresh
                                ; toast_show from another task
    push ax
    push cx
    cmp byte [toast_on], 0
    jne .chk
    cmp byte [toast_want], 0
    je .out
.chk:
    mov ax, di
    sub ax, inst_tab
    mov cl, 5                   ; I_RECSZ = 32
    shr ax, cl
    cmp al, [toast_inst]
    jne .out
    call toast_kill
.out:
    pop cx
    pop ax
    popf
    ret

section .bss

toast_buf:   resb TOAST_MAX + 1 ; the staged message. .bss because it is read
                                ; only while [toast_on] is set, and .bss is
                                ; off the image rung

section .text
