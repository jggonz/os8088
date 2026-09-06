; =============================================================================
; os8088 - events.inc
;
; The system event queue (SPEC.md 10): a single ring of 16 fixed 8-byte
; records in .bss. A FULL ring discards its OLDEST INPUT rather than refusing
; the newest (SPEC.md 10.1) - see evq_push, and the one exception it makes.
; Producers may be ISRs (the mouse ISR pushes button
; events with IF=0), consumers are tasks (the UI task pops), so both push
; and pop guard their whole read-modify-write with pushf/cli ... popf -
; the caller's interrupt flag is preserved and never a bare sti.
; =============================================================================

; --- event record layout (8 bytes) -------------------------------------------
EV_TYPE     equ 0               ; word: EVT_*
EV_A        equ 2               ; word: payload a (mouse events: x)
EV_B        equ 4               ; word: payload b (mouse events: y)
EV_C        equ 6               ; word: payload c (unused so far)

EVT_NONE    equ 0
EVT_MDOWN   equ 1               ; a=x, b=y, c=birth tick
EVT_MUP     equ 2               ; a=x, b=y, c=birth tick
EVT_RDOWN   equ 3               ; a=x, b=y, c=birth tick; RIGHT button PRESS.
                                ; There is no EVT_RUP on purpose: the only
                                ; consumer is the context-menu tracker, which
                                ; polls mouse_btn bit 1 for its whole life
                                ; (SPEC.md 12.4), so a release record would
                                ; be one nobody ever pops. EV_C rides along
                                ; for record symmetry and is read by nobody -
                                ; a right double-click would need no format
                                ; change to arrive.
EVT_WAKE    equ 4               ; a=window ptr, b/c unused. A PACKAGE'S OWN
                                ; KICK (SPEC.md 74.1): posted through
                                ; OSAPI_WM_WAKE from any context, popped by
                                ; the UI task and dispatched to the window's
                                ; W_ONWAKE handler WITHOUT the gfx lock, so
                                ; the handler may call the file slots and may
                                ; take the lock itself for a stated burst.
                                ; wm_wake coalesces: one queued wake per
                                ; window at a time, so a package that kicks
                                ; from every callback cannot fill the ring.

EVQ_RECSIZE equ 8               ; bytes per record
EVQ_CAP     equ 16              ; records in the ring
EVQ_MASK    equ (EVQ_RECSIZE * EVQ_CAP) - 1     ; byte-offset wrap mask (0x7F)
%if EVQ_MASK > 0x7F
  %error "the three index sites hold the ring offset in AL and widen it with `cbw`, and the three wrap sites mask AL alone (`and al, EVQ_MASK`). BOTH need the offset's top bit CLEAR, so EVQ_RECSIZE * EVQ_CAP may not exceed 128. This guard read `> 0xFF` until size pass 3 and that bound was one bit too generous twice over: at EVQ_MASK = 0xFF a `cbw` sign-extends offsets 0x80..0xF8 and the ring indexes UP TO 128 BYTES BELOW evq_buf - which is .lowbss, i.e. the task stacks - and the older `add ax, EVQ_RECSIZE / and al, EVQ_MASK` form left AH = 1 and stored a head of 0x0100"
%endif

; -----------------------------------------------------------------------------
; THERE IS NO evq_init, AND THAT IS NOT AN OMISSION (size pass 3).
;
; It zeroed evq_head/evq_tail/evq_count under pushf/cli...popf and kmain was
; its only caller - blk_pass's second call went with SPEC.md 74.1.1, which
; replaced a reset with a DRAIN through wm_wake_eaten, a reset having wiped
; EVT_WAKE records and left wm_wkq pointing at nothing.
;
; The three indices are `.bss` and .bss ARRIVES ZEROED: NASM emits the file
; bytes between .text's end and .cold's start as padding, and stage 2's single
; contiguous read lands that run over the whole of .bss (clip.inc's own data
; block already rests on this, and it was checked on the built binary - the
; run is 0 non-zero bytes). So the boot image does the clearing.
;
; Nothing can have pushed before kmain reached the call either: the three
; producers are mou_isr, kbm_post and wm_wake, and mouse_init, the keyboard
; hook and the window manager all come LATER in kmain than the MARK the call
; sat at. The interrupts-off bracket therefore guarded a window that could not
; open - and a machine that wants the ring re-emptied mid-session must NOT use
; a reset anyway, for 74.1.1's reason above.
; -----------------------------------------------------------------------------

; -----------------------------------------------------------------------------
; evq_push - append one event record to the queue
; in:  SI -> 8-byte record (in DS)
; out: nothing; drops the event silently when the queue is full
; clobbers: nothing (flags only)
; ISR-safe: the copy + index update sit inside pushf/cli ... popf, so a
; caller running with IF=0 (the mouse ISR) stays that way throughout.
; -----------------------------------------------------------------------------
evq_push:
    call sch_wake_ui            ; an event means the UI task has work
    push ax                     ; work (docs/SCHED-IDLE-PLAN.md 6.3)
    push si                     ; ...and no CX bank: with the record copy
                                ; unrolled nothing in this body writes it. The
                                ; CONTRACT is unchanged - CX still comes back
                                ; untouched - it just costs nothing to keep.
    push di
    push es
    pushf
    cli

    cmp byte [evq_count], EVQ_CAP
    jb .room
    ; --- SPEC.md 10.1: full, so something is lost. Lose the OLDEST ----------
    mov al, [evq_head]          ; ...unless the oldest is a WAKE, which is a
    cbw                         ; PROMISE and not a sample: wm_wake coalesces
    mov di, evq_buf             ; on a per-window flag that only a DISPATCH
    add di, ax                  ; clears. THE OFFSET IS READ ONCE AND KEPT IN
                                ; AX: the ring base is an immediate, so the
                                ; index register is built from it rather than
                                ; the word being loaded a second time below
    cmp word [ss:di + EV_TYPE], EVT_WAKE ; `ss:` because the ring is in LOW_SEG
                                ; now (SPEC.md 10.1.1), which SS names for
                                ; every kernel task and every ISR on one
    je .full                    ; DISPATCH clears, so discarding one hangs
                                ; that window's handshake for the session.
                                ; A ring full of input has input at its head,
                                ; so this falls back to dropping the newest
                                ; about as often as a wake is the oldest
    add al, EVQ_RECSIZE
    and al, EVQ_MASK           ; AL alone - see the %if beside EVQ_MASK
    mov [evq_head], al
    dec byte [evq_count]        ; ...and the copy below puts it straight back
.room:
    push ss                     ; copy runs DS:SI -> ES:DI, and ES is LOW_SEG
    pop es                      ; rather than the kernel's since SPEC.md 10.1.1
                                ; - which is what SS names, set as kmain's
                                ; third instruction, before any ISR is hooked,
                                ; and never changed again
                                ; (every task stack is in LOW_SEG). The `ss:`
                                ; override above already rests on that; this
                                ; makes the COMMON path rest on it too.
                                ; **IT IS 5 TO 12 CLOCKS SLOWER PER SITE**, not
                                ; faster: a push and a pop each move a word
                                ; through memory (~13.5 clocks each here, ~34
                                ; by the operand-byte rule) against the `mov`
                                ; pair's 21.7 on the prefetch floor. evq_push
                                ; runs once per input event, so it is under
                                ; 0.01% of the machine and the 8 bytes win.
    mov al, [evq_tail]          ; put the ring there: the SOURCE is the caller's
    cbw                         ; record and stays DS-relative. The offset
    mov di, evq_buf             ; survives the copy in AX - movsw touches
    add di, ax                  ; neither half of it - so the wrap below reads
    cld                         ; no memory
    movsw                       ; EVQ_RECSIZE / 2 = 4, unrolled: four `movsw`
    movsw                       ; is 4 bytes and 72 clocks against `mov cx,4 /
    movsw                       ; rep movsw` at 5 and 77 - and it stops
    movsw                       ; spending CX. GROW EVQ_RECSIZE AND THIS COPY
                                ; TRUNCATES SILENTLY.

    add al, EVQ_RECSIZE
    and al, EVQ_MASK           ; AL alone - see the %if beside EVQ_MASK
    mov [evq_tail], al
    inc byte [evq_count]

.full:
    popf
    jmp evq_pop.done            ; THE SAME FOUR POPS AND THE SAME `ret`: this
                                ; routine and evq_pop push ax/si/di/es in that
                                ; order and unwind them in the mirror, so one
                                ; epilogue serves both. That makes the two PUSH
                                ; SETS one contract - change either without the
                                ; other and this returns to the wrong address.
                                ; `pop` writes no flag, so the FLAGS this
                                ; `popf` restores survive the shared tail
                                ; exactly as they did when it was local.

; -----------------------------------------------------------------------------
; evq_pop - remove the oldest event record from the queue
; in:  DI -> 8-byte destination buffer (in DS)
; out: CF=0 and the record copied to [DI]; CF=1 if the queue was empty
;      (destination untouched)
; clobbers: nothing but CF
; The whole read-modify (copy + index update) sits inside pushf/cli ... popf
; so a concurrent ISR push can never interleave with it.
; -----------------------------------------------------------------------------
; evq_drain - empty the ring, owing every wake (SPEC.md 74.1.1)
; in:  DI -> an 8-byte record buffer IN DS, PRIVATE TO THE CALLER
; out: the ring is empty; DI preserved (evq_pop preserves it, and
;      wm_wake_eaten preserves every register)
; clobbers: flags
;
; THE BUFFER IS AN IN-PARAMETER AND THAT IS THE POINT.  Three drains had this
; loop written out - fsx_restore's, blk_pass's and osapi_snd_play's click
; abort - and the obvious consolidation gives them one shared buffer as well
; as one loop.  It must not: only the snd drain is atomic (it holds sch_lock
; across its loop), the two UI-task drains are not, and a kept worker or a
; TF_SERVICE task calling OSAPI_SND_PLAY - which the kernel cannot refuse,
; apps/os88api.inc says so in terms - would overwrite a record task 0 is one
; instruction away from reading.  That turns a documented visible stall into
; a window deaf for the rest of its life, which is the failure blank.inc's
; own comment already records.  So each site keeps the buffer it has.
;
; AND NO BUFFER LIVES HERE.  events.inc's data is in TWO sections: the ring
; in `.lowbss` (LOW_SEG, reached through SS) and the rest in `.bss`.  A
; buffer "beside the ring" would land in LOW_SEG while evq_pop writes ES:DI
; with ES = KERNEL_SEG.
;
; All three call sites are `.text` = KERNEL_SEG, like this, so the call is
; near.
; -----------------------------------------------------------------------------
evq_drain:
.dl:
    call evq_pop
    jc .dend
    call wm_wake_eaten          ; a package's wake is OWED, not dropped
    jmp short .dl
.dend:
    ret

; -----------------------------------------------------------------------------
; evq_mup - pop until an EVT_MUP arrives or the ring goes empty, owing every
;           other record's wake (SPEC.md 74.1.1)
; in:  DI -> an 8-byte record buffer IN DS, PRIVATE TO THE CALLER
; out: CF = 0 and [DI] is the EVT_MUP; CF = 1 the ring went empty
; clobbers: flags
;
; evq_drain's shape with one record kept, and it exists for the same reason:
; the loop was written out FOUR times - ui_drag's and ui_grow's track loops
; and fm_drag's .wait and .track - and each copy owed the wake bookkeeping
; independently. Two of the four were the far-called ones 1f1bd7e missed
; outright. One copy is one place to get it right.
;
; THE BUFFER IS AN IN-PARAMETER for evq_drain's reason, one paragraph up: a
; buffer here would be shared, and all four callers already own `ui_ev`.
; evq_pop preserves DI and wm_wake_eaten preserves every register, so DI is
; still the record on every pass; and `cmp word [di+EV_TYPE]` addresses DS,
; which is KERNEL_SEG at all four sites including the two in `.cold`.
; -----------------------------------------------------------------------------
evq_mup:
.dl:
    call evq_pop
    jc .out
    cmp word [di + EV_TYPE], EVT_MUP
    je .out                     ; equal, so CF is already 0 - the "found" answer
    call wm_wake_eaten
    jmp short .dl
.out:
    ret

; -----------------------------------------------------------------------------
evq_pop:
    push ax
    push si                     ; ...and no CX bank, for evq_push's reason
    push di
    push es
    pushf
    cli

    cmp byte [evq_count], 0
    je .empty

    push ds                     ; copy runs DS:SI -> ES:DI: the DESTINATION is
    pop es                      ; the caller's buffer and is still the kernel's
    mov al, [evq_head]          ; the offset once, kept in AX across the copy -
    cbw                         ; push/pop ds and movsw leave it alone
    mov si, evq_buf
    add si, ax
    cld
    push ds                     ; ...and the SOURCE is the ring, in LOW_SEG
    push ss                     ; since SPEC.md 10.1.1. DS is BANKED rather than
    pop ds                      ; the copy wearing a segment prefix: the 8086
    movsw                       ; LOSES the prefix when a string op restarts
    movsw                       ; after an interrupt, and NMI reaches inside the
    movsw                       ; cli above. EVQ_RECSIZE / 2 = 4, unrolled -
    movsw                       ; GROW THE RECORD AND THIS TRUNCATES SILENTLY.
    pop ds

    add al, EVQ_RECSIZE
    and al, EVQ_MASK           ; AL alone - see the %if beside EVQ_MASK
    mov [evq_head], al
    dec byte [evq_count]

    popf                        ; restore caller's IF first, then report
    clc
    jmp .done

.empty:
    popf
    stc

.done:                          ; ...and evq_push's exit too - see there
    pop es
    pop di
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; evq_pending - how many events are still queued behind this one?
; out: AX = 0..EVQ_CAP; preserves every other register and the flags
;
; SPEC.md 13.4. A callback is dispatched one event at a time, so a handler that
; REDRAWS has no way to know it is about to be asked to do the same thing
; again - and a user holding down a scroll arrow, or spamming the track, queues
; a burst of clicks whose redraws then play out one after the other long after
; the clicking stopped. The scroll positions in that burst are superseded the
; instant the next one is read; only the last one is worth a repaint.
;
; One word, read with no lock. It is a single memory read of a word the ISR
; only ever increments, so a torn value is not possible and a stale one is
; harmless in the only direction that matters: too LOW means a redraw happens
; that need not have, which is exactly today's behaviour.
;
; It answers a question about the QUEUE and not about which window an event is
; for, so a handler that skips a redraw on the strength of it must still make
; sure the redraw eventually happens - the next event may belong to somebody
; else. Note Pad owes it to its worker (SPEC.md 27.7.8).
; -----------------------------------------------------------------------------
evq_pending:
    mov al, [evq_count]             ; a BYTE since size pass 3 - see .bss below
    cbw                             ; - and 0..EVQ_CAP is inside seven bits, so
    ret                             ; the sign extension is a zero extension.
                                    ; NEAR - the OSAPI_SLOT cell owns the retf

; -----------------------------------------------------------------------------
section .lowbss                 ; LOW_SEG (SPEC.md 2.1, 10.1.1): 128 bytes of
                                ; ring out of the 64KB segment, which is the
                                ; scarce one, for nine bytes of code and no
                                ; footprint at all - the block lands inside
                                ; `.lowbss`'s existing rung.
                                ;
                                ; It is reached through SS (the WAKE test, which
                                ; is an ordinary read) and through ES and a
                                ; banked DS (the two copies). `.lowbss` does not
                                ; arrive zeroed and does not need to: the three
                                ; indices below are in `.bss`, which does, and
                                ; no record is ever read that a push did not
                                ; write.
                                ;
                                ; events.inc is %included AFTER sched.inc, so
                                ; this cannot move sch_stacks' word-alignment
                                ; pad - which is the one thing adding `.lowbss`
                                ; can break at a distance. That pad was 256
                                ; bytes and is now a word (SPEC.md 2.1), so a
                                ; block ADDED ABOVE sch_stacks moves it by at
                                ; most one byte rather than by up to 256 - and
                                ; sched.inc's %if is what refuses the odd case.
evq_buf:    resb EVQ_RECSIZE * EVQ_CAP  ; the ring itself (128 bytes)

section .bss
; ALL THREE ARE BYTES, and the %if beside EVQ_MASK is what keeps them so: the
; two offsets index a ring of EVQ_RECSIZE * EVQ_CAP = 128 bytes and the count
; is 0..EVQ_CAP, so every value fits seven bits and every reader widens with
; `cbw`. Nothing here is stored wider than its own field - the three are only
; ever written by the byte forms above - so no neighbour covers a neighbour.
evq_head:   resb 1                      ; byte offset of the oldest record
evq_tail:   resb 1                      ; byte offset of the next free slot
evq_count:  resb 1                      ; records currently queued (0..EVQ_CAP)

section .text
