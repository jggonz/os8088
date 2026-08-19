; =============================================================================
; os8088 - tests/trklog.inc
;
; THE TRACKER LOG. A per-tick record of what the audio chain and the drawing
; side were doing, written to TRKLOG.TXT so it can be carried off the machine
; and read somewhere with a screen bigger than 59 characters.
;
; This is BENCH CODE and it does not ship. `apps/tracker` carries a handful of
; one-line hooks, every one of them inside %ifdef TRKLOG, and everything else
; lives here; the shipped TRACKER.O88 is built without -DTRKLOG and contains
; none of it - not the records, not the claims, not the two keys. The bench
; build is TRKLOG.O88 on build/trklog.img (`make trklog`, docs/TESTING.md).
;
; WHY A LOG AND NOT A METER. The status line carried five running extremes and
; one live count, which was enough to rule things out - it retired the mixer,
; the drawing, the driver's own double buffer and the sample content in turn -
; and is exactly the wrong shape for what is left. An extreme says a bad thing
; happened; it cannot say WHEN, or what else was happening in the same tick, or
; whether the same tick was bad twice. The remaining question is a correlation
; between three clocks - the block IRQ, this worker's wakes and the drawing
; frames - and a correlation needs a time series.
;
; SO: one record per system tick (18.2065 Hz), 1,024 of them = 56 seconds, in a
; 16KB heap claim taken when the log is armed and given back when it is not.
; The rate limit matters and is not cosmetic: inside an fsx bracket
; OSAPI_TASK_SLEEP degenerates to an immediate resume (SPEC.md 53.3), so
; trk_feed is called far more often than 18 times a second and an unlimited log
; would fill in about a second of music.
;
; WHAT EACH FIELD IS FOR
;   TICK  [ticks] when the record was written. A GAP IN THIS COLUMN IS THE
;         MOST IMPORTANT THING IN THE FILE: it means neither the worker nor
;         the drawing side ran for that tick, which is the signature the
;         BLK/WAKE extremes could only report as one number.
;   CONS  the driver's free-running consumed count. It advances by one whole
;         2,048-byte half and only from sbl_isr, so the tick spacing between
;         two different values IS the block-IRQ interval - 6.8 ticks at
;         5,500 Hz - and this column shows every one of them rather than the
;         worst.
;   TOTL  the app's staged total. TOTL - CONS is the ring lead in bytes.
;   S     the stream state: 0 playing, 1 underrun-paused, 2 watchdog-ended.
;   PS PT RW  song position, pattern, row - so any row of the file can be
;         placed in the music, which is what "it hitches at the same point in
;         the song" needs to be checked rather than believed.
;   FR FD how many drawing frames and feed passes happened in that tick. FR 0
;         for a run of ticks is the text screen frozen; FD 0 is the worker not
;         running.
;   FL    1 a shadow rebuild started, 80h that rebuild CARRIED the window
;         instead of formatting a fresh one (SPEC.md 45.13.5 - the two are
;         the same bit 1 and nothing else in the file tells them apart),
;         2 a rebuild step ran, 4 a blit ran,
;         8 a stream was opened - and 10h, the one bit a MACHINE cannot set:
;         M stamps it, so the listener can put a mark in the file at the
;         moment they hear a hitch. Everything else here says what the guest
;         was doing; this says what the guest sounded like, and the first
;         field capture showed exactly why that is needed - 62 seconds in
;         which every counter is healthy and nothing in the file knows
;         whether anything was audible.
;   BPM SP  the replayer's tempo, so rows-per-second is derivable per record
;         rather than assumed to be the 125/6 default.
;   AR AP the row and pattern the SCREEN showed. Since SPEC.md 45.15 the
;         display quotes what the CARD is playing, not what the mixer is on,
;         so PS/PT/RW above are no longer what the user was looking at - and
;         "the scroll updates every two seconds" is a statement about THIS
;         column. AR frozen while RW advances is the stamp lookup; both
;         crawling together is the music itself running slow.
;   SD    stamps between the card and the mixer (mp_stw - mp_str), which is
;         the ring lead in ROWS. Pinned at MP_ST_N means the history lapped
;         and the display is being dragged forward by the writer (45.15).
;   SR    which producer opened the record: 0 the drawing side, 1 the worker.
;         It matters because AR and PLAY are set by tui_sync, which runs on
;         the DRAWING side - so a worker-opened record carries the previous
;         frame's row, and a run of them manufactures a row gap that reads
;         one tick short and then one tick long. The field capture that
;         found this had 26% of its 1-and-4 row gaps on worker records
;         against a 22% baseline, and 7% of its healthy 2-and-3 gaps.
;   WX WK the other half of the loop, added after a capture whose 18-tick
;         gap had FX = 00 and DX = 07: the frame was fine and the feed pass
;         accounts for a third of it, so the rest went somewhere neither span
;         covered. WX is the longest single OSAPI_FSX_WAIT in the tick - the
;         only other thing the drawing loop does - and WK counts trk_worker
;         loop passes, which is how a worker that was STARVED is told from a
;         worker that was ASLEEP. A gap with WX high is the app blocked in
;         the wait; a gap with WX low and WK low is both tasks starved by
;         something that is neither of them.
;   FX DX the longest SINGLE drawing frame and feed pass in that tick, in
;         ticks. 00 is the healthy answer - one frame is 9 ms and one pass is
;         a status poll - and anything else is the stall, named. Tick
;         resolution on purpose: a package cannot read the PIT without
;         fighting sch_account for the latch (SPEC.md 8.1), and the question
;         here is seconds, not microseconds. Pricing a HEALTHY frame is
;         tests/gfxbench's job, not this one.
;
; THE BUFFER IS A RING. It used to stop at the first 1,024 records, which is
; the wrong half of the session to keep: the listener arms the log, plays,
; hears the thing, and only then reaches for W. So the last TLOG_RECS ticks
; are what survives, and tlog_save emits oldest-first.
;
; TWO KEYS EXIST ONLY TO SPLIT THIS QUESTION IN HALF, and both are bench-only
; like the rest of this file. Y puts the display back on the MIXER's position
; (SPEC.md 45.15 off) and T puts the frame clock back on the tick (45.16 off).
; A field report of the form "it is smooth with Y, jerky without" is worth a
; day of arithmetic, and the FL column records which way each was set for
; every record, so a file can never be read against the wrong mode.
; =============================================================================

TLOG_RECS   equ 512                 ; 28 s of RING at one record per tick -
                                    ; the claim stays 16KB, which is what a
                                    ; machine small enough to be slow can
                                    ; actually spare (SPEC.md 50.3)
TLOG_RSZ    equ 32                  ; a power of two so the index is 5 shifts
TLOG_KB     equ (TLOG_RECS * TLOG_RSZ + 1023) / 1024
TLOG_LINE   equ 76                  ; 70 columns + CR LF
TLOG_TXTKB  equ (TLOG_RECS * TLOG_LINE + 1024 + 1023) / 1024


TLOGF_REBUILD equ 1
TLOGF_STEP    equ 2
TLOGF_BLIT    equ 4
TLOGF_STREAM  equ 8
TLOGF_MARK    equ 16               ; M: the LISTENER heard something here
TLOGF_NOSYNC  equ 32               ; Y: the display is back on the MIXER
TLOGF_TICKCLK equ 64               ; T: the frame clock is back on the tick
TLOGF_CARRY   equ 128              ; the boundary MOVED the window (45.13.5)
                                   ; rather than formatting a new one - the
                                   ; two look identical in the FL rebuild bit
                                   ; and cost completely different things

; record field offsets
TLR_TICK  equ 0
TLR_CONS  equ 2
TLR_TOTL  equ 4
TLR_STATE equ 6
TLR_POS   equ 7
TLR_PAT   equ 8
TLR_ROW   equ 9
TLR_FR    equ 10
TLR_FD    equ 11
TLR_FL    equ 12
TLR_BPM   equ 13
TLR_SPD   equ 14
TLR_AROW  equ 15                    ; the row the SCREEN showed (SPEC.md 45.15)
TLR_APAT  equ 16                    ; ...and its pattern
TLR_SDEP  equ 17                    ; stamps between the card and the mixer
TLR_FRMX  equ 18                    ; longest single frame this tick, in ticks
TLR_FDMX  equ 19                    ; ...and longest single feed pass
TLR_PLAY  equ 20                    ; w: the INTERPOLATED play position
TLR_WTMX  equ 22                    ; longest single OSAPI_FSX_WAIT, in ticks
TLR_WLOOP equ 23                    ; trk_worker loop passes since the record
TLR_SRC   equ 24                    ; who WROTE the record: 0 the drawing
                                    ; side, 1 the worker (AR/PLAY stale)

; -----------------------------------------------------------------------------
; tlog_key - D: arm or disarm. Arming claims the record buffer, disarming
;            gives it back, so a log that is not running costs no memory at
;            all and the heap is not split for the rest of the session
;            (SPEC.md 50.3 - the mistake docs/FIELD-NOTES.md entry 2 was).
; -----------------------------------------------------------------------------
tlog_key:
    push ax
    push bx
    push dx
    push si
    push es                         ; the claim slots are X stubs: they put
                                    ; the caller's DS in ES and the callback
                                    ; contract wants KERNEL_SEG back
    cmp word [tlog_seg], 0
    jne .off
    mov ax, TLOG_KB                 ; AX = KB in, DX = the base segment OUT
    call OSAPI_MEM_CLAIM            ; (SPEC.md 50.3 - the answer moves
    jc .nomem                       ; register, and reading it out of AX
    mov [tlog_seg], dx              ; writes 16KB of records over the IVT)
    mov word [tlog_n], 0
    mov byte [tlog_full], 0
    mov byte [tlog_flags], 0
    mov byte [tlog_frames], 0
    mov byte [tlog_feeds], 0
    call OSAPI_GET_TICKS
    mov [tlog_lastt], ax
    jmp short .say
.off:
    mov dx, [tlog_seg]              ; DX both ways
    call OSAPI_MEM_FREE
    mov word [tlog_seg], 0
    jmp short .say
.nomem:
    mov si, tlog_s_nomem
    call tui_msg
    jmp short .out
.say:
    call tlog_status
.out:
    pop es
    pop si
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tlog_stream - trk_play opened a stream: flag it into the next record
; -----------------------------------------------------------------------------
tlog_stream:
    or byte [tlog_flags], TLOGF_STREAM
    ret

; -----------------------------------------------------------------------------
; tlog_mark - M: the listener heard something. Stamped into the next record.
;
; The one input to this log that is not a measurement. Everything else says
; what the machine was doing; a hitch is a thing a person hears, and without
; a mark a file of healthy counters cannot be told from a file of healthy
; counters taken while the audio was fine.
; -----------------------------------------------------------------------------
tlog_mark:
    push si
    or byte [tlog_flags], TLOGF_MARK
    mov si, tlog_s_mark
    call tui_msg
    pop si
    ret

; -----------------------------------------------------------------------------
; tlog_flag - AL = a TLOGF_* bit the drawing side wants recorded
; -----------------------------------------------------------------------------
tlog_flag:
    or [tlog_flags], al
    ret

; -----------------------------------------------------------------------------
; tlog_feed - called from trk_feed with AX = stream state, DX = consumed.
;             Banks what only the worker can see, then offers a record.
; -----------------------------------------------------------------------------
tlog_feed:
    push ax
    mov byte [tlog_src], 1          ; if this call is the one that opens the
                                    ; record, AR and PLAY in it are the LAST
                                    ; FRAME's - tui_sync runs on the drawing
                                    ; side and the worker cannot refresh it
    mov [tlog_state], al
    mov [tlog_cons], dx
    mov ax, [trk_total]
    mov [tlog_totl], ax
    inc byte [tlog_feeds]
    jnz .ok
    dec byte [tlog_feeds]           ; saturate rather than wrap: 255 already
.ok:                                ; means "far more than one"
    call tlog_tick
    pop ax
    ret

; -----------------------------------------------------------------------------
; tlog_frame - called once per drawn frame. Bumps the counter, offers a
;              record, and refreshes the status line, which is live because
;              the record count is the only sign the log is running.
; -----------------------------------------------------------------------------
tlog_frame:
    mov byte [tlog_src], 0          ; this record's AR and PLAY are fresh: the
                                    ; frame that just ran set them
    inc byte [tlog_frames]
    jnz .ok
    dec byte [tlog_frames]
.ok:
    push ax
    push bx
    mov ax, [tlog_ft0]
    mov bx, tlog_frmx
    call tlog_span
    pop bx
    pop ax
    call tlog_tick
    call tlog_status
    ret

; -----------------------------------------------------------------------------
; tlog_fstart / tlog_wstart - a drawing frame / a feed pass began HERE
;
; The span each one ends up reporting is in TICKS, and 00 is the healthy
; answer for both. That is deliberately coarse: the question a field report
; of "it updates every two seconds" asks is which of the two took the two
; seconds, and tick resolution answers it without a package fighting
; sch_account for the PIT latch (SPEC.md 8.1).
; -----------------------------------------------------------------------------
tlog_fstart:
    push ax
    call OSAPI_GET_TICKS
    mov [tlog_ft0], ax
    pop ax
    ret

tlog_wstart:
    push ax
    call OSAPI_GET_TICKS
    mov [tlog_wt0], ax
    pop ax
    ret

; tlog_wend - close the feed pass's span. It has to be the pass's OWN end and
;             not the next pass's start: measured from the top of trk_feed to
;             four instructions later, DX read 00 through a capture in which
;             the machine plainly stalled for a second.
tlog_wend:
    push ax
    push bx
    mov ax, [tlog_wt0]
    mov bx, tlog_fdmx
    call tlog_span
    pop bx
    pop ax
    ret

; tlog_wtstart / tlog_wtend - the same pair around OSAPI_FSX_WAIT, which is
; everything the drawing loop does that tlog_fstart..tlog_frame does not.
tlog_wtstart:
    push ax
    call OSAPI_GET_TICKS
    mov [tlog_wtt0], ax
    pop ax
    ret

tlog_wtend:
    push ax
    push bx
    mov ax, [tlog_wtt0]
    mov bx, tlog_wtmx
    call tlog_span
    pop bx
    pop ax
    ret

; tlog_wake - one trk_worker loop pass. Counted at the TOP of the loop, before
; the sleep, so it says how often the worker got the CPU rather than how often
; it found work: a starved worker and an idle one both show FD 0.
tlog_wake:
    inc byte [tlog_wloop]
    jnz .ok
    dec byte [tlog_wloop]
.ok:
    ret

; tlog_span - AX = the tick the thing started at, BX = the max byte's address;
;             keep the larger of what is there and how long it took
tlog_span:
    push ax
    push cx
    push dx
    mov dx, ax
    call OSAPI_GET_TICKS
    sub ax, dx
    cmp ax, 255
    jbe .fit
    mov ax, 255                     ; saturate: 14 seconds is already "hung"
.fit:
    cmp al, [bx]
    jbe .out
    mov [bx], al
.out:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tlog_sync_key (Y) / tlog_clk_key (T) - the two A/B toggles (SPEC.md 45.15,
; 45.16). Both are bench-only and both are recorded on every record, so a log
; carries the mode it was taken in rather than relying on the reader's memory.
; -----------------------------------------------------------------------------
tlog_sync_key:
    push si
    xor byte [tlog_nosync], 1
    mov si, tlog_s_synco
    cmp byte [tlog_nosync], 0
    je .say
    mov si, tlog_s_syncx
.say:
    call tui_msg
    pop si
    ret

tlog_clk_key:
    push ax
    push si
    mov si, tlog_s_clkt
    cmp byte [ttx_clk], FSXW_TICK
    je .fast
    mov byte [ttx_clk], FSXW_TICK   ; back to one frame per tick, and the
    mov al, TTX_SHCHUNK             ; rebuild chunk that goes with it
    mov [ttx_chunk], al
    mov byte [ttx_fdiv], 1
    mov byte [ttx_fcnt], 1
    jmp short .say
.fast:
    mov byte [ttx_clk], FSXW_FRAME
    mov al, TTX_SHCHUNKF
    mov [ttx_chunk], al
    mov byte [ttx_fdiv], FSX_SUBTICK
    mov byte [ttx_fcnt], FSX_SUBTICK
    mov si, tlog_s_clkr
.say:
    call tui_msg
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; tlog_rate_key (K) - XT mode's SAMPLE RATE, without leaving XT mode
;                     (docs/FIELD-NOTES.md 16). Bench-only.
;
; This exists because the obvious way to change the rate - press X - changes
; the SURFACE too (SPEC.md 45.13): XT off is the graphics FT2 screen, which
; SPEC.md 45.9.1 measured at 2,567 glyph cells a second and which on a real XT
; is unusable. It also destroys the measurement, because a starved frame clock
; makes tui_playpos degenerate (SPEC.md 45.15.2's fpt = 1 case) and the phase
; being asked about is then not the phase under test.
;
; What it is FOR: separating a GUEST-side offset between the display and the
; music from HOST-side output buffering in an emulator. One DMA block is
; TRK_HALF bytes whatever the rate, so in TIME it is 512 ms at 4 kHz, 372 at
; 5.5 and 186 at 11 - against a fixed 125 ms row that is 4.1, 3.0 and 1.5
; rows. A host buffer is a fixed number of MILLISECONDS and does not move.
; Three points, one line, and the slope is the answer.
;
; The floor is the hardware's: a Sound Blaster's rate is 1000000/(256-tc) with
; tc a BYTE, so nothing below 3,906 Hz exists to ask for. 4,000 is tc = 6.
;
; A rate change needs the stream reopened, so this is windowed-only and takes
; effect at the next Play - like X, and for the same reason.
; -----------------------------------------------------------------------------
tlog_rate_key:
    push ax
    push bx
    push si
    mov al, [tlog_xr]
    inc al
    cmp al, 3
    jb .set
    xor al, al
.set:
    mov [tlog_xr], al
    mov bl, al
    xor bh, bh
    shl bx, 1
    mov ax, [tlog_xrates + bx]
    mov [tlog_xrate], ax
    mov byte [trk_xhi], 0           ; ...and the app's own byte follows, so
    cmp ax, TRK_RATE_XT2            ; File > Xt Rate never names a rate that
    jne .lab                        ; is not the one playing. 4,000 has no
    mov byte [trk_xhi], 1           ; label of its own and reads as 5.5, which
.lab:                               ; is the honest half-truth of the two
    mov si, [tlog_xrstr + bx]
    call tui_msg
    pop si
    pop bx
    pop ax
    ret

; tlog_xrate_set - move K's sweep onto whatever trk_xhi now says (SPEC.md
; 45.9.3). Called by trk_xrate_tog, so the two controls cannot disagree: this
; file's rate is the one trk_play reads, and the app's byte is the one the
; menu draws. Preserves everything.
tlog_xrate_set:
    push ax
    push bx
    mov bl, 0                       ; trk_xhi 0 -> index 0 (5,500)
    mov ax, TRK_RATE_XT
    cmp byte [trk_xhi], 0
    je .set
    mov bl, 2                       ; ...and 1 -> index 2 (11,000). 4,000 is
    mov ax, TRK_RATE_XT2            ; K's alone: the shipped build has no such
.set:                               ; rate and SPEC.md Set 20 closed it
    mov [tlog_xrate], ax
    mov [tlog_xr], bl
    pop bx
    pop ax
    ret

tlog_xrates:    dw 5500, 4000, 11000    ; 5,500 is XT mode's own rate and the
                                        ; start, so the FIRST press is the one
                                        ; worth testing: PERFORMANCE.md Set 20
                                        ; makes the rate the only large lever
                                        ; on the mixer's 45%
tlog_xrstr:     dw tlog_s_r55, tlog_s_r4, tlog_s_r11
tlog_xrate:     dw TRK_RATE_XT      ; the live one, and the index that names it
tlog_xr:        db 0                ; - in the IMAGE and not in bss, because
                                    ; trk_stream_open reads the rate and a
                                    ; package's bss arrives zeroed, which here
                                    ; would be a request for 0 Hz

; -----------------------------------------------------------------------------
; tlog_xtrate_stamp - put the LIVE rate into the text screen's header string
;                     (SPEC.md 45.13's 'XT 5500 Hz' cell)
;
; The shipped build bakes 5,500 in because XT mode IS 5,500 Hz. Here K moves
; it, so the screen has to say which one is running - a swept rate nobody can
; read off the machine is not a measurement.
;
; Called from ttx_put's caller with SI already on the string, DI the VRAM
; offset and AH the attribute, so it preserves EVERYTHING. [mp_mixrate] and
; not [tlog_xrate], because what the header owes is the rate the stream was
; OPENED at, and K takes effect at the next Play.
; -----------------------------------------------------------------------------
tlog_xtrate_stamp:
    push ax
    push bx
    push cx
    push dx
    push di
    mov di, ttx_s_xtrate + 7        ; the last of the five digits
    mov ax, [mp_mixrate]
    mov cx, 5
    mov bx, 10
.d:
    xor dx, dx
    div bx
    add dl, '0'
    mov [di], dl
    dec di
    loop .d
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret
tlog_s_r4:      db 'XT rate 4000 Hz - block 512ms = 4.1 rows (press Enter)', 0
tlog_s_r55:     db 'XT rate 5500 Hz - block 372ms = 3.0 rows (press Enter)', 0
tlog_s_r11:     db 'XT rate 11000 Hz - block 186ms = 1.5 rows (press Enter)', 0

; -----------------------------------------------------------------------------
; tlog_tick - write one record IF the tick has moved since the last one
;
; Both producers call this, so a tick in which either side ran gets a record
; and a tick in which NEITHER ran gets none - which is what makes a gap in the
; TICK column mean something. Preserves every register.
; -----------------------------------------------------------------------------
tlog_tick:
    push ax
    push bx
    push di
    push es
    mov ax, [tlog_seg]
    or ax, ax
    jz .out
    push ax
    call OSAPI_GET_TICKS
    mov di, ax
    pop ax
    cmp di, [tlog_lastt]
    je .out                         ; same tick: fold into the counters
    mov [tlog_lastt], di
    mov bx, [tlog_w]                ; the RING's write slot, not a count
    mov es, ax
    mov ax, bx                      ; AX = w * TLOG_RSZ (32)
    shl ax, 1
    shl ax, 1
    shl ax, 1
    shl ax, 1
    shl ax, 1
    xchg ax, di                     ; DI = offset, AX = the tick again
    mov [es:di + TLR_TICK], ax
    mov ax, [tlog_cons]
    mov [es:di + TLR_CONS], ax
    mov ax, [tlog_totl]
    mov [es:di + TLR_TOTL], ax
    mov al, [tlog_state]
    mov [es:di + TLR_STATE], al
    mov al, [mp_songpos]
    mov [es:di + TLR_POS], al
    mov al, [mp_pattern]
    mov [es:di + TLR_PAT], al
    mov al, [mp_row]
    mov [es:di + TLR_ROW], al
    mov al, [tlog_frames]
    mov [es:di + TLR_FR], al
    mov al, [tlog_feeds]
    mov [es:di + TLR_FD], al
    mov al, [tlog_flags]            ; ...plus which way the two bench toggles
    cmp byte [tlog_nosync], 0       ; were set, on EVERY record, so a file can
    je .sy                          ; never be read against the wrong mode
    or al, TLOGF_NOSYNC
.sy:
    cmp byte [ttx_clk], FSXW_TICK
    jne .ck
    or al, TLOGF_TICKCLK
.ck:
    mov [es:di + TLR_FL], al
    mov al, [mp_bpm]
    mov [es:di + TLR_BPM], al
    mov al, [mp_speed]
    mov [es:di + TLR_SPD], al
    mov al, [tui_arow]              ; what the SCREEN showed (SPEC.md 45.15)
    mov [es:di + TLR_AROW], al
    mov al, [tui_apat]
    mov [es:di + TLR_APAT], al
    mov al, [mp_stw]                ; the ring lead in ROWS: pinned at
    sub al, [mp_str]                ; MP_ST_N means the history lapped
    mov [es:di + TLR_SDEP], al
    mov al, [tlog_frmx]
    mov [es:di + TLR_FRMX], al
    mov al, [tlog_fdmx]
    mov [es:di + TLR_FDMX], al
    mov ax, [tui_play]              ; what tui_playpos answered (45.15.1):
    mov [es:di + TLR_PLAY], ax      ; PL - CONS is the interpolation itself
    mov al, [tlog_wtmx]
    mov [es:di + TLR_WTMX], al
    mov al, [tlog_wloop]
    mov [es:di + TLR_WLOOP], al
    mov al, [tlog_src]
    mov [es:di + TLR_SRC], al
    mov byte [tlog_frames], 0
    mov byte [tlog_feeds], 0
    mov byte [tlog_flags], 0
    mov byte [tlog_frmx], 0
    mov byte [tlog_fdmx], 0
    mov byte [tlog_wtmx], 0
    mov byte [tlog_wloop], 0
    inc word [tlog_w]               ; the RING wraps rather than stopping:
    cmp word [tlog_w], TLOG_RECS    ; the listener reaches for W AFTER hearing
    jb .count                       ; the thing, so the LAST 28 s is the half
    mov word [tlog_w], 0            ; worth keeping
    mov byte [tlog_full], 1
.count:
    cmp word [tlog_n], TLOG_RECS    ; the count saturates; [tlog_full] is what
    jae .out                        ; says the window has started sliding
    inc word [tlog_n]
.out:
    pop es
    pop di
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tlog_status - 'LOG 0123/0512' / '...FULL' / 'LOG off' into [tui_msgp]
; -----------------------------------------------------------------------------
tlog_status:
    push ax
    push di
    push si
    cmp word [tlog_seg], 0
    je .off
    mov si, tlog_s_run
    cmp byte [tlog_full], 0
    je .have
    mov si, tlog_s_full
.have:
    mov di, tlog_s_stat
    call tui_scpy
    mov ax, [tlog_n]                ; the count, four decimal digits
    mov di, tlog_s_stat + 4
    call tlog_dec4
    mov ax, TLOG_RECS               ; ...and the denominator, so the string
    mov di, tlog_s_stat + 9         ; cannot drift from the ring's real size
    call tlog_dec4                  ; the way a baked '/1024' already had
    mov si, tlog_s_stat
    jmp short .say
.off:
    mov si, tlog_s_off
.say:
    call tui_msg
    pop si
    pop di
    pop ax
    ret

; tlog_dec4 - AX right-aligned, zero-padded, four decimal digits at [DI]
tlog_dec4:
    push ax
    push bx
    push cx
    push dx
    push di
    add di, 3
    mov cx, 4
    mov bx, 10
.d:
    xor dx, dx
    div bx
    add dl, '0'
    mov [di], dl
    dec di
    loop .d
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tlog_save - W: format every record and write TRKLOG.TXT (SPEC.md 18.4)
;
; WINDOWED ONLY, and not by choice: the file slots are UI-callback-only and a
; bracket owns the machine until it returns (SPEC.md 53.7), so Esc first. The
; text is a TRANSIENT claim held across this one call - 44KB of it, which has
; no business living in a region that caps at 60 (SPEC.md 27.6's rule, and
; Note Pad's) - and the write itself is several seconds of floppy, which is why
; the status line says so before it starts. It was estimated here at ~20 s from
; a 7,457 bytes/second read figure that was two Part 9 rounds stale (Set 17
; against Sets 22/24's 21,307); the honest figure is that a WRITE is not a read
; and this path has never been timed - PERFORMANCE.md Set 24 prices a 32KB
; create at 4.67 s and an 8KB-chunked append at 11.04 s for the same bytes, so
; the answer depends entirely on which of those shapes this is.
; -----------------------------------------------------------------------------
tlog_save:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp word [tlog_seg], 0
    jne .have
    mov si, tlog_s_none
    call tui_msg
    jmp .out
.have:
    mov si, tlog_s_writing
    call tui_msg
    call trk_repaint_done           ; the notice has to reach the glass BEFORE
                                    ; twenty seconds of floppy, not after
    mov ax, TLOG_TXTKB
    call OSAPI_MEM_CLAIM
    jc .nomem
    mov [tlog_txt], dx              ; DX, not AX (SPEC.md 50.3)
    mov es, dx
    xor di, di
    mov si, tlog_s_h1
    call tlog_emit
    mov si, tlog_s_h2
    call tlog_emit
    mov si, tlog_s_h3
    call tlog_emit
    mov si, tlog_s_h5
    call tlog_emit
    mov si, tlog_s_h6
    call tlog_emit
    mov si, tlog_s_h7
    call tlog_emit
    mov si, tlog_s_h8
    call tlog_emit
    mov si, tlog_s_h9
    call tlog_emit
    mov si, tlog_s_h10
    call tlog_emit
    call tlog_hdr_clk               ; the frame clock the probe chose, which
    mov si, tlog_line               ; is constant for the bracket and so has
    call tlog_emit                  ; no business in a per-tick column
    mov si, tlog_s_h4
    call tlog_emit
    mov word [tlog_i], 0            ; OLDEST first: once the ring has wrapped
    cmp byte [tlog_full], 0         ; the oldest record is the write slot
    je .from0
    mov ax, [tlog_w]
    mov [tlog_i], ax
.from0:
    mov word [tlog_left], 0
    mov ax, [tlog_n]
    mov [tlog_left], ax
.row:
    cmp word [tlog_left], 0
    je .done
    call tlog_fmt                   ; record [tlog_i] -> tlog_line
    mov si, tlog_line
    call tlog_emit
    dec word [tlog_left]
    inc word [tlog_i]
    cmp word [tlog_i], TLOG_RECS
    jb .row
    mov word [tlog_i], 0
    jmp short .row
.done:
    mov cx, di                      ; DX:CX = the byte count
    xor dx, dx
    mov bx, 0
    mov si, tlog_s_name
    call OSAPI_FILE_WRITE
    pushf
    push ax
    mov dx, [tlog_txt]              ; the staging claim goes back either way
    call OSAPI_MEM_FREE
    pop ax
    popf
    jc .werr
    mov si, tlog_s_wrote
    call tui_msg
    jmp short .out
.werr:
    mov si, tlog_s_werr
    call tui_msg
    jmp short .out
.nomem:
    mov si, tlog_s_nomem
    call tui_msg
.out:
    call trk_repaint_done
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; tlog_emit - copy the NUL string at DS:SI to ES:DI, then CR LF. DI advances.
tlog_emit:
    push ax
    push si
    cld
.l:
    lodsb
    or al, al
    jz .eol
    stosb
    jmp short .l
.eol:
    mov al, 13
    stosb
    mov al, 10
    stosb
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; tlog_fmt - record [tlog_i] -> tlog_line as
;            'TICK CONS TOTL S PS PT RW FR FD FL BPM SP'
;
; Hex throughout and a table lookup per nibble (tui_hexb), because the one
; thing this must not do is spend a div per field on a 4.77MHz machine while
; the user waits.
; -----------------------------------------------------------------------------
tlog_fmt:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, [tlog_seg]
    mov es, ax
    mov si, [tlog_i]
    shl si, 1
    shl si, 1
    shl si, 1
    shl si, 1
    shl si, 1                       ; * TLOG_RSZ (32)
    mov di, tlog_line
    mov ax, [es:si + TLR_TICK]
    call tlog_hex4
    mov ax, [es:si + TLR_CONS]
    call tlog_hex4
    mov ax, [es:si + TLR_TOTL]
    call tlog_hex4
    mov al, [es:si + TLR_STATE]     ; the state is one nibble; the rest are
    add al, '0'                     ; bytes
    mov [di], al
    inc di
    mov al, ' '
    mov [di], al
    inc di
    mov bx, TLR_POS
.b:
    push bx
    add bx, si
    mov al, [es:bx]
    pop bx
    call tui_hexb
    mov byte [di], ' '
    inc di
    inc bx
    cmp bx, TLR_FDMX
    jbe .b
    mov ax, [es:si + TLR_PLAY]      ; ...the interpolated position, then the
    call tlog_hex4                  ; two loop spans, so the byte columns
    mov al, [es:si + TLR_WTMX]      ; above stay where they were
    call tui_hexb
    mov byte [di], ' '
    inc di
    mov al, [es:si + TLR_WLOOP]
    call tui_hexb
    mov byte [di], ' '
    inc di
    mov al, [es:si + TLR_SRC]
    call tui_hexb
    mov byte [di], 0
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tlog_hdr_clk - 'clock: retrace  fdiv 03  chunk 01' into tlog_line. What
; ttx_clkprobe decided is constant for the bracket and explains the whole FR
; column, so it belongs in the header rather than in 512 records.
; -----------------------------------------------------------------------------
tlog_hdr_clk:
    push ax
    push si
    push di
    mov di, tlog_line
    mov si, tlog_s_clk
    call tui_scpy
    mov si, tlog_s_tick
    cmp byte [ttx_clk], FSXW_TICK
    je .name
    mov si, tlog_s_retr
.name:
    call tui_scpy
    mov si, tlog_s_fdiv
    call tui_scpy
    mov al, [ttx_fdiv]
    call tui_hexb
    mov si, tlog_s_chunk
    call tui_scpy
    mov al, [ttx_chunk]
    call tui_hexb
    mov byte [di], 0
    pop di
    pop si
    pop ax
    ret

; tlog_hex4 - AX as four hex digits plus a space at [DI]; DI advances by 5
tlog_hex4:
    push ax
    mov al, ah
    call tui_hexb
    pop ax
    call tui_hexb
    mov byte [di], ' '
    inc di
    ret

; --- strings -----------------------------------------------------------------
tlog_s_name:    db 'TRKLOG.TXT', 0
tlog_s_run:     db 'LOG     /      W saves', 0
tlog_s_full:    db 'LOG     /     FULL  W saves', 0
                                ; both templates put the count at +4 and the
                                ; ring size at +9 - tlog_status stamps both,
                                ; and 'FULL' sits clear of the second field
                                ; (it used to be at +4, so the count erased
                                ; the very word that says the ring stopped)
tlog_s_off:     db 'LOG off - D arms it', 0
tlog_s_none:    db 'No log - D arms one first', 0
tlog_s_nomem:   db 'Log: out of memory', 0
tlog_s_writing: db 'Writing TRKLOG.TXT - 20s of floppy', 0
tlog_s_wrote:   db 'Wrote TRKLOG.TXT', 0
tlog_s_werr:    db 'TRKLOG.TXT write failed', 0
tlog_s_fsw:     db 'Save is windowed: Esc first', 0
tlog_s_mark:    db 'MARK', 0
tlog_s_h1:      db 'os8088 tracker log - one record per system tick (18.2065 Hz)', 0
tlog_s_h2:      db 'CONS advances one 2048-byte half per block IRQ; TOTL-CONS is the ring lead', 0
tlog_s_h3:      db 'FL: 1 rebuild 2 step 4 blit 8 stream 10 MARK 80 rebuild CARRIED', 0
tlog_s_h4:      db 'TICK CONS TOTL S PS PT RW FR FD FL BP SP AR AP SD FX DX PLAY WX WK SR', 0
tlog_s_h5:      db 'AR/AP is what the SCREEN showed; SD is the card-to-mixer lead in ROWS', 0
tlog_s_h6:      db 'FX/DX/WX: longest frame / feed pass / FSX_WAIT that tick, TICKS (00 ok)', 0
tlog_s_h9:      db 'WK counts trk_worker loop passes: low WK in a TICK GAP is a STARVED worker', 0
tlog_s_h10:     db 'SR 1 = the WORKER opened this record, so its AR and PLAY are a frame stale', 0
tlog_s_h8:      db 'PLAY is CONS interpolated between block IRQs; PLAY-CONS <= one block', 0
tlog_s_h7:      db 'FL 20 = Y pressed (display on the mixer), 40 = T pressed (tick clock)', 0
                ; 80 = the rebuild CARRIED (SPEC.md 45.13.5) - named in h3
tlog_s_clk:     db 'clock: ', 0
tlog_s_tick:    db 'tick', 0
tlog_s_retr:    db 'frame (sub-tick)', 0
tlog_s_fdiv:    db '  fdiv ', 0
tlog_s_chunk:   db '  chunk ', 0
tlog_s_synco:   db 'Sync ON: the screen follows the card', 0
tlog_s_syncx:   db 'Sync OFF: the screen follows the mixer', 0
tlog_s_clkt:    db 'Clock: one frame per tick', 0
tlog_s_clkr:    db 'Clock: one frame per sub-tick', 0

; --- bss ---------------------------------------------------------------------
    TRKW tlog_seg                   ; the record claim, 0 = not armed
    TRKW tlog_txt                   ; the staging claim, live inside tlog_save
    TRKW tlog_n                     ; records written
    TRKW tlog_i                     ; tlog_save's cursor
    TRKW tlog_w                     ; the ring's write slot
    TRKW tlog_left                  ; tlog_save's records still to emit
    TRKW tlog_lastt                 ; the tick the last record carries
    TRKW tlog_ft0                   ; the tick the current frame began at
    TRKW tlog_wt0                   ; ...and the current feed pass
    TRKW tlog_wtt0                  ; ...and the current OSAPI_FSX_WAIT
    TRKB tlog_frmx                  ; longest frame / feed pass / wait this
    TRKB tlog_fdmx                  ; tick
    TRKB tlog_wtmx
    TRKB tlog_wloop                 ; worker loop passes since the record
    TRKB tlog_src                   ; which producer is opening this one
    TRKB tlog_nosync                ; Y: the display is back on the mixer
    TRKW tlog_cons                  ; what the worker last saw...
    TRKW tlog_totl
    TRKB tlog_state
    TRKB tlog_frames                ; ...and what has happened since the last
    TRKB tlog_feeds                 ; record was written
    TRKB tlog_flags
    TRKB tlog_full
    TRKBUF tlog_s_stat, 32          ; the live status line
    TRKBUF tlog_line, TLOG_LINE + 8
