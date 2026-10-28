; =============================================================================
; os8088 - toast.inc (SPEC.md 59)
;
; THE TRANSIENT ONE-LINE MESSAGE, for the whole machine: `Saved NOTES.TXT`,
; `Opened SUNSET.BMP`. One routine, one look, one lifetime - where five
; applications had each re-rolled their own and no two agreed about where it
; went, how long it lived or who erased it.
;
; It was NOT ui_note (SPEC.md 54.4.1), the notice WINDOW a failure the user
; had to acknowledge once raised - and since kernel size pass 4 there is no
; ui_note: a failed launch (the Task Manager's, the Wire zone's, a document's
; program not found) is a toast like any other verdict, says its piece and
; goes away on its own.
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
; message plus one inverted space and no wider, and over the clock it takes
; the LEFT end of the field: the clock is composed into the whole field
; before the strip is laid over it, so a short message lands in the time's
; leading spaces and a long one costs the DATE, which is the end worth
; losing. The clock is never suppressed while a message is up: it is simply
; not what those particular cells say.
;
; And the ERASE is the same composition (SPEC.md 12.9), so a toast expiring
; lets the menus' spaces and the time straight back and nothing here has to
; remember to restore anything. There is no fill in this file, no
; menu_inval, and no drawing routine at all.
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
                                ; to fit rather than left to truncate.
                                ;
                                ; **AND THAT SENTENCE WAS A CLAIM ABOUT A TREE
                                ; THAT NOTHING HELD** (SPEC.md 59.10). A 286
                                ; showed `Hibernation file is from` - the cut
                                ; landing on the word that carries the meaning
                                ; - and the sweep behind it found TWENTY-ONE
                                ; over the cap in six files, ten of them in
                                ; `HIBER.DRV`, which is a MODULE and was never
                                ; walked. `tests/unit/t_toast.py` is the gate
                                ; now: it reads this very `equ` rather than
                                ; carrying a copy, and `tests/toastlong.txt`
                                ; is the ratchet its false positives live in
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

; THE THREE FLAG BYTES ARE READ AND WRITTEN IN PAIRS, so their ORDER is part
; of the code below and the %if under them is what keeps it.  Little-endian,
; `[toast_dirty]` as a word is (dirty, on) and `[toast_on]` as a word is
; (on, want) - which is every pair any site here needs.  The order was
; (on, want, dirty); this one is worth 26 bytes where (on, dirty, want) was
; worth 12, and it is also the one that keeps `toast_want` cleared FIRST in
; toast_arm.
toast_dirty: db 0               ; the composition changed and nobody has
                                ; drawn it - set by the two edges AND by
                                ; toast_kill, which runs under the lock and
                                ; so may not draw for itself
toast_on:    db 0               ; a strip is composed into the bar
toast_want:  db 0               ; staged by toast_show, not yet put up
%if (toast_on - toast_dirty) != 1 || (toast_want - toast_dirty) != 2
  %error "toast.inc: the three flag bytes are stored and tested in PAIRS as words - dirty+on at [toast_dirty], on+want at [toast_on] - so they must stay adjacent in that order. Reorder them and every one of those sites writes or tests the wrong byte, silently"
%endif
toast_inst:  db 0FFh            ; the raiser's instance slot (0FFh = kernel)
toast_die:   dw 0               ; the tick it expires at
toast_ttl:   dw TOAST_TICKS     ; ...and how long it was asked for
toast_scell: dw 0               ; the strip's first bar cell (toast_place),
toast_scn:   dw 0               ; how many cells it takes, 0 = no strip,
toast_scov:  db 0               ; and 1 = it is over the CLOCK and hides the
                                ; speaker (SPEC.md 34.15). In .text with real
                                ; initialisers for [toast_on]'s reason:
                                ; menu_draw_bar runs during drv_boot, before
                                ; any init routine of ours

; -----------------------------------------------------------------------------
; toast_show - say one line in the menu bar for a few seconds (API 0x02AA)
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
    call kent_di                ; = push ax/bx/cx/dx/si/di. BX is not spent
                                ; here - it is banked to make ax..di the
                                ; shared epilogue ladder's own run, which is
                                ; what kret_di wants
    pushf
    cli
    cld                         ; the copy below is `es lodsb`, and OSAPI_SLOT
                                ; does NOT cld - a package running with `std`
                                ; (SPEC.md 20) would walk its string backwards.
                                ; Free of side effects: the pushf above and the
                                ; popf on both exits restore the caller's DF
                                ; exactly as they restore its IF
    or cx, cx
    jnz .ttl
    mov cx, TOAST_TICKS
.ttl:
    or ch, ch                   ; toast_pass measures expiry with a SIGNED
    jns .ttlok                  ; subtract (the modular-clock compare), so a
    mov cx, 0x7FFF              ; TTL past 32767 ticks would read as already
.ttlok:                         ; over and the toast would die on first look.
                                ; BIT 15 IS THE WHOLE TEST, so it is asked of
                                ; CH: same predicate as `cmp cx,0x7FFF / jbe`
                                ; at every value including both boundaries
    mov [toast_ttl], cx
    mov di, toast_buf
    mov cx, TOAST_MAX
    mov dx, TOAST_SCAN
.cp:
    es lodsb                    ; the same instruction files.inc:2050 uses in
                                ; the same role, and it is why the `cld` above
                                ; is not optional
    or al, al
    jz .end
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
    jmp short .out              ; the lock, it reaches the glass NOW rather
                                ; than on the next idle pass (SPEC.md 59.4)
.kill:
    call toast_kill             ; preserves the flags, so the popf below is
    popf                        ; still the caller's
.out:
    clc                         ; ONE `clc`: nothing here refuses today, and
                                ; `.out` is reached from those two paths and
                                ; nothing else. A failure path added later
                                ; must jump PAST this.
    jmp kret_di                 ; pops di, si, dx, cx, bx, ax - this routine's
                                ; push run. `pop` and `ret` write no flag, so
                                ; the `clc` still reaches the caller

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
    cbw                         ; a code of 0x80 or more sign-extends to
                                ; 0xFF80..0xFFFF rather than 0x0080..0x00FF -
                                ; and BOTH are refused by the `jae` below,
                                ; which is UNSIGNED and whose bound is a small
                                ; table length. Every caller passes a FERR_*
                                ; / LD_* code well under 0x80; the hostile
                                ; case is refused either way. Widen that bound
                                ; or make the compare signed and this byte has
                                ; to go back. `cbw` writes no flag, so the
                                ; `or al, al` below is unaffected.
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
    retf

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
    cmp word [gfx_lock_flag], 1 ; HELD (flag = 1) **and owned by task 0** (own
    jne .outa                   ; = 0) in ONE word read: the two bytes are
                                ; declared adjacent in vga12.inc and a 0-byte
                                ; %if there holds them to it. The flag is
                                ; strictly 0 or 1 - every writer was read, and
                                ; the comment beside the declaration says so.
                                ; Torn reads cannot lie either: gfx_lock writes
                                ; flag then owner inside its own cli window and
                                ; gfx_unlock writes owner FIRST then flag, so
                                ; the only interleaving visible is (1, 0xFF) =
                                ; 0xFF01, which this refuses exactly as the two
                                ; byte compares did; and an 8086 takes
                                ; interrupts only at instruction boundaries, so
                                ; the word read is atomic
    cmp byte [sch_cur], 0       ; ...and task 0 - ui_task (kernel.asm's kmain
    jne .outa                   ; tail) - is the one asking, not a bystander
                                ; running while a pre-empted worker owns the
                                ; glass
    mov al, [gfx_color]         ; the caller is mid-callback: its pen and its
    push ax                     ; clip region are ITS, and the bar is inside
    push bx                     ; no window's region ever - drawn through an
    call fpg_bare               ; armed one the strip's cells would be dropped
    dw toast_ad                 ; on the glass while menu_bcell records them
                                ; composed, and no later pass would repair it.
                                ; fpg_bare banks the region, clears it, runs
                                ; the painter and puts it back: the three
                                ; steps this wrote out itself (kernel size
                                ; pass 14: 6 bytes; fpg_bare spends BX).
                                ; Done now because the idle pass that would
                                ; carry it to VRAM does not come until the
                                ; operation this announces is over (SPEC.md
                                ; 32) - fpg_begin's reasoning exactly
    pop bx
    pop ax
    mov [gfx_color], al
.outa:
    pop ax
    ret

toast_ad:                       ; toast_now's painter: arm, then the bar
    call toast_arm
    jmp menu_draw_bar

; -----------------------------------------------------------------------------
; toast_arm - spend [toast_want]: the strip goes up
; in/out:   nothing (AX clobbered - both callers have banked it)
; -----------------------------------------------------------------------------
toast_arm:
    mov byte [toast_want], 0    ; FIRST, still: a toast_show landing mid-arm
                                ; has to survive to the next pass
    mov ax, [ticks]
    add ax, [toast_ttl]
    mov [toast_die], ax
    mov word [toast_dirty], 0x0100  ; dirty = 0, on = 1, in one store - and
                                    ; still AFTER [toast_die], so no pass can
                                    ; see on=1 beside a stale expiry
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
                                ; The `dirty = 0` half of that word store IS
                                ; the draw, on the toast_now path; toast_pass
                                ; re-sets it and draws.
                                ; NOTHING is invalidated: the composition
                                ; compares every cell, the clock's included,
                                ; against what is on the glass, so it finds
                                ; the difference by itself (SPEC.md 12.9)
    ret

; -----------------------------------------------------------------------------
; toast_place - decide where the strip goes (SPEC.md 59.9)
; in:       [menu_bend], [menu_bc0] - called from menu_bar_text, after the
;           menus are composed when they are
; out:      [toast_scn]   = the strip's cells, 0 = no strip
;           [toast_scell] = its first cell (0 = x MENU_TXT_X0)
;           [toast_scov]  = 1 when it is over the clock, which hides the
;                           speaker (SPEC.md 34.15) and is menu_ckend's cue
; clobbers: nothing (flags only)
;
; **CENTRED IN THE GAP**, between the end of the last menu and the clock's
; first GLYPH - its midpoint on the gap's, half the slack each side, `shr`
; flooring so an odd cell lands right. It covers nothing there, and it reads
; as a message belonging to the machine rather than a fifth menu or a prefix
; to the clock (SPEC.md 59.9, 59.9.1). The clock's first glyph is taken with
; the speaker SHOWING, because in the gap the speaker does show.
;
; **AND OVER THE CLOCK WHEN IT DOES NOT FIT** - from the field's left end,
; where it costs the date before the time (SPEC.md 59.8), with the speaker
; hidden so the field is the one it has with none, 25 cells, which TOAST_MAX
; is written against. Never a truncation: a message that depended on how many
; menus the application in front has would be a different message.
;
; **THE WIDGET IS NOT A NEIGHBOUR ANY MORE** (SPEC.md 12.8.7). A toast that is
; up and within its time outranks SPEC.md 12.8's file-activity widget, which
; is then not drawn and holds no cells, so where the strip goes never depends
; on whether a disk is being read - the strip stays put while a widget comes
; and goes underneath its operation, where it used to be shunted along.
; -----------------------------------------------------------------------------
toast_place:
    push ax
    push bx
    push cx
    push dx
    push si                     ; ax,bx,cx,dx,si is the shared ladder's run,
                                ; so `.out` is a rung rather than five pops
    xor ax, ax
    mov [toast_scn], ax
    mov [toast_scov], al        ; menu_ckend below asks it: the speaker SHOWS
    call toast_up               ; CF = 1: a strip is up and within its time
    jnc .out
    call menu_ckend             ; AX = the field's end, speaker showing
    call clk_fmt                ; CX = the clock's glyphs (AX it keeps)
    sub ax, cx                  ; AX = the clock's first glyph
    mov si, toast_buf           ; the strip's width in cells: the message plus
    call str_len                ; the one inverted space at its left (AX it
    jcxz .out                   ; keeps too, so the two widths need no bank:
    inc cx                      ; kernel size pass 14, 4 bytes)
    mov bx, [menu_bend]
    sub ax, bx                  ; the gap...
    jb .clk
    sub ax, cx                  ; ...less the strip: the slack
    jb .clk
    shr ax, 1
    add ax, bx
    jmp short .set
.clk:
    inc byte [toast_scov]       ; over the clock, from the field's left end
    mov ax, [menu_bc0]
.set:
    mov [toast_scell], ax
    mov [toast_scn], cx
.out:
    jmp kret_si                 ; pops si, dx, cx, bx, ax - the push run above

; -----------------------------------------------------------------------------
; toast_up - is a strip up and within its time? (SPEC.md 12.8.7, 59.9)
; out:      CF = 1 yes
; clobbers: AX
;
; Expiry is toast_pass's, and toast_pass runs on the UI task's idle pass - so
; a freeze, which is a handler that has not returned, keeps a strip on the bar
; past its time. This asks the clock instead, which is what lets the file
; widget have the bar back when a toast's seconds are spent, mid-operation,
; rather than after it. The same modular compare as toast_pass: the SIGN of
; ticks - die.
; -----------------------------------------------------------------------------
toast_up:
    test byte [toast_on], 1     ; CF = 0 either way; [toast_on] is 0 or 1 at
    jz .r                       ; every writer, so 0 is "not up" (a byte under
                                ; the compare and its cmc: kernel size pass 14)
    mov ax, [ticks]
    sub ax, [toast_die]
    rol ah, 1                   ; CF = the sign: still to come
.r:
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
    mov ax, [toast_on]          ; AL = on, AH = want - the adjacent pair the
    or ah, ah                   ; %if beside them pins, read in ONE load where
    jnz .up                     ; it was two compares against memory (kernel
    or al, al                   ; size pass 10: 3 bytes)
    jz .chk
    mov ax, [ticks]
    sub ax, [toast_die]
    js .chk                     ; MODULAR arithmetic between two free-running
                                ; words, so the SIGN of the difference
                                ; decides - sch_isr's wake scan and SPEC.md
                                ; 45.15's stamp compare. A `jg` honours OF
                                ; and would hold a toast across the wrap
    mov word [toast_dirty], 0x0001  ; dirty = 1, on = 0, in one store...
.chk:                           ; ...and FALLS INTO the test, where it used to
                                ; step over `.up` with a `jmp short`. `.up` is
                                ; at the foot of the routine now
    mov al, 0                   ; take-and-clear in ONE exchange, where it
    xchg al, [toast_dirty]      ; was a compare and a separate store - and a
    or al, al                   ; set landing between those two (toast_kill
    jz .out                     ; from a pre-empting task) is no longer lost
.draw:
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

.up:                            ; MOVED HERE from between the expiry store and
    call toast_arm              ; `.chk`, so that store falls through instead of
    jmp short .draw             ; jumping over this block. toast_arm's last act
                                ; leaves [toast_dirty] ZERO, so setting it here
                                ; only to have `.chk` clear it two instructions
                                ; later was a round trip through a byte that
                                ; had just been zeroed. IF toast_arm EVER STOPS
                                ; CLEARING IT this reads as a message appearing
                                ; one pass late.

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
    cmp word [toast_on], 0      ; "on OR want", as one word over the pair
    je .out
    mov word [toast_on], 0      ; ...and both cleared as one
    mov byte [toast_dirty], 1
.out:
    popf
    ret

; -----------------------------------------------------------------------------
; toast_owner_gone - a closing instance takes its message with it
; in:       DI = the record being freed (inst_rel_rec's contract, and it
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
    call inst_idx               ; AL = this record's instance index. NO "is
    cmp al, [toast_inst]        ; anything up?" test first: toast_kill opens
    jne .out                    ; with exactly that test (`cmp word
    call toast_kill             ; [toast_on], 0`, one word over the pair) and
.out:                           ; does nothing when it fails, so the only
                                ; thing the deleted pair bought was skipping
                                ; one inst_idx (a shift) and one compare in
                                ; the case where no toast is up. With nothing
                                ; up [toast_inst] still holds the LAST
                                ; raiser's slot - nothing resets it on expiry
                                ; - so the compare can now match and
                                ; toast_kill is called, which writes nothing.
                                ; It preserves every register and the flags,
                                ; so CX needs no banking here either
    pop ax
    popf
    ret

section .bss

toast_buf:   resb TOAST_MAX + 1 ; the staged message. .bss because it is read
                                ; only while [toast_on] is set, and .bss is
                                ; off the image rung

section .text
