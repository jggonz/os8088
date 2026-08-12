; =============================================================================
; os8088 - blank.inc
;
; The idle screen blanker (SPEC.md 64). After BLK_IDLE ticks with no keyboard
; and no mouse, every monitor the machine has goes dark; the next input brings
; them all back, and that input is swallowed rather than acted on.
;
; It is for the MONITOR and not for the machine. The tubes this OS runs in
; front of - a 5151, a 5153, whatever is on the second card - burn a static
; image into the phosphor if it is left lit, and a desktop is the worst
; possible thing to leave there: a menu bar, a dock strip and a drive column
; that never move, at full brightness, for as long as the machine is on.
; Nothing about it is a power feature; there is no DPMS on hardware this old
; and none is attempted.
;
; WHAT IS BLANKED IS THE VIDEO SIGNAL, NOT THE FRAMEBUFFER. vid_blank_kind
; (SPEC.md 39.18.1) gates the card's video output and leaves the CRTC running,
; so both syncs keep coming out, the monitor never re-acquires, and memory is
; untouched. Three things fall out of that and all three are load-bearing:
; the screen comes back INSTANTLY and exactly as it was, with nothing to
; repaint and no mode set; a background task may keep drawing into a blanked
; card with no more than a compare anywhere in the drawing path - which is why
; there is not one gate in gfx_*; and the blank cannot be undone by accident,
; because nothing in the kernel writes those mode registers except vid_setmode
; and the pair above.
; =============================================================================

; --- the idle period ---------------------------------------------------------
;
; [ticks] is the BIOS counter at 18.2065 Hz, so five minutes is 5,461.95 of
; them. It stays 18.2065 Hz whether or not SPEC.md 53.2.1's sub-tick is armed:
; that divides IRQ0 three ways and only every third entry bumps [ticks].
BLK_IDLE    equ 5462            ; five minutes, in ticks

; -----------------------------------------------------------------------------
; State. In .text with real initialisers rather than in .bss, and here that is
; not the usual caution - .bss does arrive zeroed (kernel.asm: the image's gap
; to OVL_START is emitted as zeros and the boot sector's one contiguous read
; lands them), and it costs the same image bytes either way, because the rung
; KIMG_PARA rounds covers .text and .bss together.
;
; It is here because [blk_t0] = 0 is not scratch, it is the ANSWER: the idle
; period starts when the machine does, so a desktop nobody ever touches blanks
; five minutes after it is drawn. Written down as an initialiser that reads as
; a decision, rather than as a zero somebody has to go and confirm.
; -----------------------------------------------------------------------------
blk_t0      dw 0                ; [ticks] when the current idle period began
blk_on      db 0                ; is the video signal gated off right now?
blk_act     db 0                ; mou_isr saw a packet; the UI task spends it

; -----------------------------------------------------------------------------
; blk_wake - user input has arrived (SPEC.md 64.2)
;
; in:       nothing; NO GFX LOCK HELD
; out:      CF = 1: the screen was dark and THIS input is what woke it, so the
;                   caller owes it to the user to throw that input away
;           CF = 0: the screen was already lit; ordinary input, deliver it
; clobbers: flags
;
; THE WAKE INPUT IS CONSUMED, and that is a safety rule rather than a
; convention borrowed from elsewhere. While the screen is dark the user cannot
; see what is under the pointer or which window has the keyboard, so acting on
; the input that arrives out of that state is acting blind: a click lands on
; whatever happens to be beneath it, a keystroke goes to whichever window was
; frontmost five minutes ago. Restoring the display IS the whole of what a wake
; event means. Both halves of it are the caller's to spend, because they arrive
; by different routes - ui.inc drops the keystroke it has just fetched, and
; blk_pass drains the event ring.
;
; It is also what makes the feature safe to have no interaction with anything
; else in the kernel: an unblank can only be reached from ui_task, so no
; drawing path, no mode set and no fsx bracket can ever run against a screen
; that is dark without the user having lit it first.
; -----------------------------------------------------------------------------
blk_wake:
    push ax
    mov ax, [ticks]
    mov [blk_t0], ax            ; the idle period restarts from here, lit or
    cmp byte [blk_on], 0        ; dark - this is the only place it is set
    pop ax                      ; outside boot (pop does not touch flags)
    jne .dark
    clc                         ; already lit: an ordinary input
    ret
.dark:
    push ax                     ; ...and it can only have been a wake. AX is
    xor al, al                  ; banked because this routine's contract is
    call blk_set                ; flags and nothing else: ui.inc calls it
    pop ax                      ; mid-keystroke, between int 16h's peek and
    stc                         ; its fetch, and fsx_restore from inside its
    ret                         ; own saved frame

; -----------------------------------------------------------------------------
; blk_pass - the UI task's per-pass service (SPEC.md 64.1)
;
; in:       nothing; NO GFX LOCK HELD
; out:      nothing
; clobbers: flags
;
; Two compares in the common case, which is every pass of a machine somebody is
; using: is there mouse activity to spend, and has the idle period run out.
;
; IT SITS AHEAD OF evq_pop AND THAT ORDERING IS BINDING - it is the whole of
; the mouse half of SPEC.md 64.2. The press that woke the screen is still in
; the event ring when this runs, so draining it HERE is what stops it being
; dispatched a few instructions later as a click on a window the user could not
; see. Moved below evq_pop this still blanks and still wakes, and the first
; click out of a dark screen goes wherever the pointer was left.
;
; The keyboard does not come through here: ui.inc has already fetched the
; keystroke by the time it knows there was one, so it calls blk_wake itself and
; drops the key on CF = 1.
; -----------------------------------------------------------------------------
blk_pass:
    cmp byte [blk_act], 0       ; the mouse ISR saw a packet on a settled port
    jne .act
    cmp byte [blk_on], 0
    jne .out                    ; already dark: there is no clock running
    push ax
    mov ax, [ticks]
    sub ax, [blk_t0]            ; modular, and it cannot wrap on us: the test
    cmp ax, BLK_IDLE            ; fires the moment it passes five minutes, and
    jb .pop                     ; a word holds sixty
    mov al, 1
    call blk_set
.pop:
    pop ax
.out:
    ret

.act:
    mov byte [blk_act], 0       ; consumed BEFORE it is acted on: a packet
    call blk_wake               ; landing inside this window simply sets it
    jnc .out                    ; again, and the worst a lost edge can cost is
    call evq_init               ; one more pass. The press that woke the screen
    ret                         ; is not a click (SPEC.md 64.2)

; -----------------------------------------------------------------------------
; blk_set - every monitor the machine has, dark or lit (SPEC.md 64.3)
;
; in:       AL = 1 to blank, 0 to unblank
; out:      [blk_on] follows
; clobbers: flags
;
; EVERY CARD, not just [vid_kind]'s. On a two-monitor machine (SPEC.md 39.11) -
; which is the machine this project is calibrated against, a 5150 with a
; Hercules and a CGA in it - the desktop spans both and both tubes are lit, so
; blanking the primary alone would protect one monitor and leave the other
; showing a frozen desktop all night. The walk is vid_fsx_enter's, which darks
; every card but the bracket's for the same reason from the other end.
;
; No lock is taken and none is needed: this writes I/O ports and never touches
; a framebuffer, and vid_seq1_blank owns the one access that has to be atomic.
; -----------------------------------------------------------------------------
blk_set:
    mov [blk_on], al
    push ax
    push cx
    push di
%ifdef KERN_BIG
    mov cl, [vid_ndisp]
    xor ch, ch
.d:
    mov al, ch
    call vid_ctx_ptr            ; DI = that display's record; AX preserved
    mov al, [di+VID_CTX_KIND]
    call blk_one
    inc ch
    cmp ch, cl                  ; a do-while: [vid_ndisp] is never 0, and a
    jb .d                       ; kernel that made it 0 would still do one
%else
    mov al, [vid_kind]          ; kern_small is single-display by construction
    call blk_one                ; - vidsel.inc's whole multi-display half is
%endif                          ; inside one %ifdef KERN_BIG
    pop di
    pop cx
    pop ax
    ret

; in: AL = VID_* kind. [blk_on] has already been written, and says which way.
; Two near jmps and not a conditional one: an 8086 conditional branch is short
; only, and both bodies are a module away in vidsel.inc.
blk_one:
    cmp byte [blk_on], 0
    jne .dark
    jmp vid_unblank_kind
.dark:
    jmp vid_blank_kind
