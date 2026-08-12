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

TOAST_MAX   equ 23              ; characters, AND IT IS THE TIGHT ONE (SPEC.md
                                ; 59.8). The clock's field is ([vid_wm8] -
                                ; [vid_clk_hx]) / 8 = 200/8 = 25 cells on
                                ; every screen this runs on - 640 and 720 both
                                ; subtract the same 206 and round to the same
                                ; 8 - and the strip spends one inverted space
                                ; at each end, so 23 is what is left. It was
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
; **IT NO LONGER DEFERS TO THE FILE-ACTIVITY WIDGET** (SPEC.md 12.8). It used
; to refuse outright while [fpg_on] was set, and fpg_begin retired whatever
; was up - both of which were about PIXELS, the widget sitting at
; [vid_clk_hx] - FPG_W and the strip reaching left into the same cells. The
; strip is in the clock's field now, wholly to the RIGHT of the widget's
; right edge, so the two cannot touch and neither rule has anything left to
; protect. Dropping them is the point rather than a tidy-up: a refusal here
; loses the message for good, silently, and the widget is armed by exactly
; the mounts and reads that the messages worth saying are about - which is
; why three call sites in files.inc had to be ordered around it.
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
    mov byte [toast_dirty], 0   ; ...and this IS the draw, on the toast_now
                                ; path; toast_pass re-sets it and draws.
                                ; NOTHING is invalidated here: the field's
                                ; painter composes what it is about to show
                                ; and compares it against [menu_ckck] plus
                                ; [menu_ckstrip], so it finds the difference
                                ; by itself (SPEC.md 59.8) -
                                ; where this used to set [menu_bdirty], a flag
                                ; about the MENUS, which the strip has not
                                ; been in since it moved
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
