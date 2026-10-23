; =============================================================================
; os8088 - apps/xtmp3/x3eng.inc - THE ENGINE (SPEC.md 121.5, 121.6, 121.9)
;
; Everything between the decoder and the user: the compressed-input ring and
; its refill, the worker, the feed into OSAPI_SND_STREAM, the states, pause,
; stop, next/previous and seek with pre-roll, calibration and the quality
; ladder, the finished-WAV player (the Sound Blaster here, the PC speaker and
; the Covox through the Audio Player's own bracket, %included below), and the
; teardown. x3conv.inc is the other half: the decode-first dialog, the
; conversion, finalisation and the cache. docs/xtmp3/ENGINE.md is the account
; of both - the rings' sizes, the handshake, the constants, the memory.
;
; THE FACE'S INTERFACE (x3face.inc reads x3_st and calls nothing but these;
; each runs on the UI task with the gfx lock held, answers CF = 0 and may
; clobber AX, CX, DX, SI, DI - BX is the caller's):
;
;   x3_e_init         the entry proc, before the window: x3_st's first state
;   x3_e_attach       the entry proc, after it (x3_win set): the wake and the
;                     close handlers
;   x3_e_tick         every X3_TICK ticks, from the window's timer
;   x3_e_play   AL    play queue entry AL from its start
;   x3_e_cue    AL    load entry AL without playing it (READY)
;   x3_e_pause / x3_e_resume / x3_e_stop
;   x3_e_seek   AX    to second AX of the current track (clamped)
;   x3_e_qual         X3S_QWANT changed
;   x3_e_vol          X3S_VOL changed
;   x3_e_cancel       Esc: a conversion's Cancel
;   x3_e_qadd   AL    entry AL joined the queue: its title and duration
;   x3_e_qdrop  AL    entry AL is about to leave the queue
;   x3_e_qclear       the queue is about to be emptied
;
; THE DECODER'S INTERFACE is apps/xtmp3/x3gran.inc's (SPEC.md 121.3): the
; front end's near procs x3p_* (x3pars.inc in the primary - or x3stubp.inc,
; its stand-in, until the STANDARD track's lands) and a backend part's far
; verbs, reached through x3_bvcall. The parts are named by X3_PART_STD and
; X3_PART_XT (xtmp3.asm's parts table); a build without a backend part has
; them FFh, and every rung is then refused by name rather than played.
;
; DIVISION OF LABOUR (SPEC.md 20.6 rule 7, the Audio Player's, 86.5):
;   - the UI task opens and closes the stream, claims and frees, fetches and
;     drops the parts, OPENs the backend, and does every disk read and write;
;   - the WORKER stages frames from the ring (x3p_next, x3p_gran), runs the
;     backend's GRANULE and feeds - the card through the any-task stream
;     verbs, or a conversion's staged half - and never touches a file or the
;     heap. When it wants the disk it sets a bit in [x3_req] and wakes the UI
;     task; x3_onwake services it and clears the bit LAST (SPEC.md 77.1).
;   - [x3_wbusy] brackets a worker pass: set FIRST, the job read after it,
;     cleared LAST. x3_park clears the job and waits for it, so no UI-side
;     change can race a pass half done.
; =============================================================================

; --- versions: the cache identity (SPEC.md 121.7) --------------------------------
X3_DECVER   equ 1                   ; the decoders' numerics, as x3sim pins them
X3_TABVER   equ 1                   ; the table packs' generator

; --- the ladder and calibration (SPEC.md 121.5) ----------------------------------
X3_SAFETY   equ 125                 ; a rung passes at 1.25 audio s a wall s
X3_SLOW     equ 110                 ; a live window under 1.10x is slow
X3_FLOOR    equ 8000                ; no auto rung below this output rate
X3_CAL_GRAN equ 24                  ; granules a probe times, at most...
X3_CAL_TICKS equ 18                 ; ...and ticks, at most (~1 s)
X3_CAL_HOPE equ 4                   ; a first granule 4x short of passing ends
                                    ; the probe there: a hopeless rung costs one
                                    ; granule, and every dearer rung of that
                                    ; backend is skipped with it
X3_CAL_SKIP equ 2                   ; frames decoded untimed after the probe's
                                    ; seek, so a short reservoir's silence is
                                    ; not what is timed
X3_PREROLL  equ 6                   ; frames decoded to nothing after a seek or
                                    ; a rung change (SPEC.md 121.9): MEASURED -
                                    ; the least pre-roll after which the output
                                    ; is an uninterrupted decode's, over 749
                                    ; seeks into 33 fixtures: 1 frame 3.2%, 2
                                    ; 15.8%, 3 60.7%, 4 92.5%, 5 99.9%, 6 100%.
                                    ; A frame's main data reaches back up to
                                    ; 511 bytes (several frames at 32 kbps),
                                    ; and short blocks chain their overlap
                                    ; (docs/xtmp3/ENGINE.md section 1)
X3_OFFMAX   equ 8                   ; offline Auto: the best rung converting in
                                    ; at most 8x the track's own length
X3_WINGR    equ 32                  ; granules a live slow window spans
X3_NCAL     equ 8                   ; the session's cached probe results

; --- the output ---------------------------------------------------------------------
X3_RINGKB   equ 16                  ; the SND grant: 16 KB, else 8 - and 32
X3_RINGSM   equ 8                   ; KB where the output's bytes a second
X3_RINGBIG  equ 32                  ; pass X3_RINGHI (a 16 KB ring is 93 ms
X3_RINGHI   equ 32000               ; of 44.1 kHz 16-bit stereo)
X3_HALF     equ 4096                ; a published half: 4,096 bytes, which is
                                    ; every regime's (SPEC.md 34.5.5) - the
                                    ; smaller 2,048 of the legacy one fits it
X3_MAXFEED  equ 8                   ; halves a worker pass publishes, at most:
                                    ; the whole ring, when it can
X3_PCMSZ    equ 576 * 2 * 2         ; a granule's PCM, at most: 576 frames,
                                    ; two channels, sixteen bits - kept just
%define X3_PCMOFF word [x3_lasz]   ; past the RING, in the same claim (the
                                    ; primary's segment has no room for it)
X3_RAMP     equ 32                  ; frames an underrun's ramp to silence takes
X3_DEVMIN   equ 4000                ; the card's lowest rate (a DSP's floor)
X3_DEVMAX   equ 22050               ; ...its highest without SND_CAP_PCM_HI
X3_CEILKB   equ 390                 ; the XT path's working set, at most: 390 KB
                                    ; = 399,360 bytes, inside SPEC.md 121.10's
                                    ; 400,000 (x3_plan budgets against it)
X3_PCMKB    equ (X3_PCMRES + 1023) / 1024   ; the PCM's room, in the claim

; --- worker jobs, and what the worker asks of the UI task --------------------------
WJ_NONE     equ 0
WJ_LIVE     equ 1                   ; decode into the card
WJ_FILE     equ 2                   ; a finished WAV into the card
WJ_CONV     equ 3                   ; decode into the conversion's stage
WJ_CAL      equ 4                   ; a calibration probe

X3R_READ    equ 0x01                ; the ring wants a gulp
X3R_WRITE   equ 0x02                ; a conversion half is staged
X3R_END     equ 0x04                ; the job ended (the stream, the source)
X3R_STEP    equ 0x08                ; step DOWN a rung (live)
X3R_CAL     equ 0x10                ; a probe finished
X3R_FAIL    equ 0x20                ; the decoder refused (x3_wfail says why)
X3R_UIOP    equ 0x40                ; a dialog's action, [x3_uiop] (x3conv.inc):
                                    ; its window is UNBOUND - billed to no
                                    ; instance - so a file call from its own
                                    ; callbacks would move the MACHINE and not
                                    ; us; the main window's wake does it

; --- the engine's own mode (x3_md): what x3_st's state is the face of ------------
MD_IDLE     equ 0
MD_CHECK    equ 1                   ; calibrating
MD_LIVE     equ 2                   ; MP3 into the card
MD_FILE     equ 3                   ; a finished WAV into the card
MD_SPK      equ 4                   ; a finished WAV on the speaker / Covox
MD_CHOOSE   equ 5                   ; the decode-first dialogs are up
MD_CONV     equ 6                   ; converting
MD_STEP     equ 7                   ; live, draining the ring for a rung change

; --- a rung's row (x3_rungs): backend, profile, divisor ---------------------------
RG_BE       equ 0                   ; X3K_STD / X3K_XT
RG_PROF     equ 1                   ; the backend's profile
RG_DIV      equ 2                   ; 1, 2, 4 - or 0: the XT Speech rule
RG_SIZE     equ 4

; =============================================================================
; THE AUDIO PLAYER'S SHARED SOURCE, ASSEMBLED HERE
;
; apps/audio/aplook.inc (the look-ahead ring: one gulp read straight into a
; heap claim) and apps/audio/apspk.inc (the PC speaker's and the Covox's
; bracket, SPEC.md 86.21) are the Audio Player's own source, included as it
; is - the way WEAVE and LOOM share theirs (WEAVE-SPEC 1.2). They name their
; state with the Audio Player's names; this block defines those names over
; XTMP3's own state, and nothing in either file was changed for it but the
; split of aplook.inc out of apengine.inc, which assembles AUDIO.O88 byte for
; byte as before.
;
; THE RING IS ONE: x3_ring is x3gran.inc's compressed-input record (X3R_*),
; and its SEG, RD, WR and EOF ARE aplook.inc's ap_la_seg, ap_la_rd, ap_la_wr
; and ap_src_done - the same words, so the Audio Player's refill is the MP3
; parser's producer with nothing in between, and the finished WAV's after it.
; =============================================================================
; THE RING'S SIZE IS THE BACKEND'S (x3_lasize): 32 KB, a power of two and a
; whole number of clusters to 16 KB (SPEC.md 86.5) - and 16 KB under the XT
; backend, whose machine has 400,000 bytes for all of it (SPEC.md 121.10) and
; whose decode is slow enough that 16 KB of 128 kbps is a second ahead. A
; refill is asked for under half of it. aplook.inc reads all three as words
X3_LABIG    equ 0x8000
X3_LAXT     equ 0x4000
%define AP_LA_SZ   word [x3_lasz]
%define AP_LA_MASK word [x3_lamask]
%define AP_LA_LOW  word [x3_lalw]
X3_PCMRES   equ 2560                ; the PCM's room, X3_PCMSZ to 512 bytes, so
                                    ; a conversion's stage after it is aligned
AP_RD_CHUNK equ 32768               ; the most a gulp reads: a cluster of 32 KB
                                    ; (FAT16's largest) is one gulp, so a
                                    ; finished WAV plays from any volume; an
                                    ; MP3 wants two in its ring (x3_plan)
AP_DRAW_TICKS equ 8                 ; the speaker's redraw throttle
AP_FMT_PCM8 equ 0                   ; apspk's file formats: a finished WAV is
AP_FMT_ADPCM equ 1                  ; always PCM, so its ADPCM arm never runs
AP_ST_STOP  equ 0                   ; apspk's states, the bracket's answer
AP_ST_PLAY  equ 1
AP_ST_PAUSE equ 2

; the shared sources' bss: their APB/APW/APD/APBUF land in ONE block of ours,
; x3_apbss, laid out by xtmp3.asm's X3B list (X3_ENGBSS below) once this
; counter has its final value
%assign X3A_AT 0
%macro APB 1
%1 equ x3_apbss + X3A_AT
%assign X3A_AT X3A_AT + 1
%endmacro
%macro APW 1
%1 equ x3_apbss + X3A_AT
%assign X3A_AT X3A_AT + 2
%endmacro
%macro APD 1
%1 equ x3_apbss + X3A_AT
%assign X3A_AT X3A_AT + 4
%endmacro
%macro APBUF 2
%1 equ x3_apbss + X3A_AT
%assign X3A_AT X3A_AT + (%2)
%endmacro
%macro APCNT 1                      ; the Audio Player's diagnostic counters:
%endmacro                           ; none here
%macro APADD 2
%endmacro

ap_la_seg   equ x3_ring + X3R_SEG   ; THE ring (see above)
ap_la_rd    equ x3_ring + X3R_RD
ap_la_wr    equ x3_ring + X3R_WR
ap_src_done equ x3_ring + X3R_EOF
ap_win      equ x3_win
    APW ap_lg_got                   ; aplook.inc's state
    APW ap_clu
    APW ap_rd_len
    APW ap_skip
    APW ap_trk_dir
    APB ap_trk_vol
    APD ap_rd_pos
    APD ap_got
    APD ap_data_off
    APD ap_data_end
    APBUF ap_rd_cur, FSEQ_SIZE
    APW apw_name                    ; -> the file's NUL name (x3_fname)
    APW ap_msg                      ; the sentence a refusal leaves
    APW ap_rate                     ; the WAV's rate (the speaker's S)
    APB apw_fmt                     ; AP_FMT_PCM8, always
    APB apw_spk                     ; 0: no 'o8sp' (SPEC.md 86.21.1)
    APB ap_state                    ; AP_ST_*: the bracket's answer
AP_TRK0 equ x3_apbss + X3A_AT       ; apspk's per-track run, zeroed by
    APD ap_played                   ; ap_prep_track (as the Audio Player's is)
    APW ap_elapsed_s
    APB aps_eot
    APB aps_lag
    APW aps_crem
AP_TRKLEN equ x3_apbss + X3A_AT - AP_TRK0
    APW ap_last_s                   ; apspk's redraw keys
    APW ap_last_bar
apd_out     equ x3_work             ; apspk's span of inputs (APD_SRCSZ, 2,368
                                    ; bytes): the face's band, 2,720 - free
                                    ; while a span is made, which is never
                                    ; across a draw (aps_span1 makes one and
                                    ; puts it in the ring before aps_draw)
ap_cinv     equ aps_dbuf            ; (the Audio Player's own alias)

; the speaker's sentences: two bytes before each say where the device is
; named and how long, for aps_say's Covox (audio.asm's APSAY, the same layout)
%macro APSAY 4
%strlen %%b %2
%strlen %%d %3
              db %%b, %%d
%1:           db %2, %3, %4, 0
%endmacro
    APSAY ap_s_spk, '', 'PC speaker', ' - click or Space to pause'
    APSAY ap_s_spkpaused, 'Paused - Space plays on the ', 'speaker', ''
    APSAY ap_s_spkbusy, 'The ', 'PC speaker', ' is busy'
ap_s_spkfast: db 'Made for a faster machine', 0
    APSAY ap_s_spkslow, 'Too slow for this file on the ', 'speaker', ''
ap_s_nomem:   db 'Out of memory', 0
ap_s_nosb:    db 'This kernel has no speaker play', 0
ap_s_stopped: db 'Stopped', 0
ap_s_endlist: db 'Finished', 0
apw_e_io:     db 'Cannot read the file', 0

%define APLOOK_GET
%define APLOOK_REFILL
%include "aplook.inc"
%undef APLOOK_GET
%undef APLOOK_REFILL
%include "os88rseq.inc"
%include "os88pit.inc"
%include "sndpkg.inc"               ; SNDV_MIX*: the knob's mixer verbs

;
; X3LOG code, value: a byte and a word appended to x3_dlog (X3STUB builds
; only - tests/xtmp3eng.py reads it to say which way a probe or a job went)
%macro X3LOG 2
%ifdef X3STUB
    cmp word [x3_st+X3S_UNDER], 0   ; (frozen at the first underrun)
    jne %%done
    push ax
    push bx
    push word %2                    ; (the value first: %2 may be AX or BX)
    mov bl, [x3_dlogn]
    xor bh, bh
    pop ax
    cmp bl, 3 * 16
    jb %%in                         ; (a ring: the last 16 are kept)
    xor bl, bl
%%in:
    mov [x3_dlog+bx+1], ax
    mov al, %1
    mov [x3_dlog+bx], al
    add bl, 3
    mov [x3_dlogn], bl
    pop bx
    pop ax
%%done:
%endif
%endmacro

; ap_ret6 - THE SHARED EPILOGUE aplook.inc ends on: pop DI SI DX CX BX AX, ret
ap_ret6:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE FACE'S PROCS
; =============================================================================

; -----------------------------------------------------------------------------
; x3_e_init - the entry proc, before the window exists
; -----------------------------------------------------------------------------
x3_e_init:
    mov byte [x3_st+X3S_STATE], X3ST_IDLE
    mov byte [x3_st+X3S_CUR], 0xFF
    mov byte [x3_st+X3S_VOL], 7
    mov byte [x3_md], MD_IDLE
    mov byte [x3_lastq], 0xFF
    mov byte [x3_bpart], 0xFF       ; no backend part fetched
    mov byte [x3_bkind], 0xFF
    mov ax, X3_LABIG
    call x3_lasize
    push bx
    push dx
    call OSAPI_FILE_HERE            ; where we were launched from: the index's
    mov [x3_homed], dx              ; volume (SPEC.md 19.9 - the program's own,
    mov [x3_homev], bl              ; wherever a play has since moved us)
    pop dx
    pop bx
    call x3_volsense                ; the knob: a mixer's master, or the gain
%ifdef X3STUB
    call x3_stubcfg                 ; the test hook (x3hook.inc)
%endif
    push ax                         ; THE PEAK'S ZERO (SPEC.md 121.10): the
    push bx                         ; heap's free total now, with the region
    call OSAPI_MEM_AVAIL            ; and the eager parts already out of it
    mov [x3_free0], bx
    pop bx
    pop ax
    call x3_memck
    clc
    ret

; x3_e_attach - the window exists: the wake (the worker's handshake, the
; launch document's handover) and the close negotiator
x3_e_attach:
    push ax
    push bx
    mov bx, [x3_win]
    mov ax, x3_onwake
    call OSAPI_WM_ONWAKE
    mov ax, x3_onclose
    call OSAPI_WM_ONCLOSE
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; x3_e_tick - the window's timer: the clock, the meters, the gauge, a
; conversion's progress, and the engine's own transitions
; -----------------------------------------------------------------------------
x3_e_tick:
%ifdef X3REPORT
    call x3r_tick                   ; FIELD (x3rep.inc): an error's report
%endif
    cmp byte [x3l_arm], 0           ; A PROBE ARMED: it runs once a whole
    je .na                          ; pass's face update has followed the arm
    dec byte [x3l_arm]              ; (x3l_launch) - an armed document's
    jnz .na                         ; play arms it in this pass, before here
    cmp byte [x3_md], MD_CHECK
    jne .na
    mov byte [x3_job], WJ_CAL
.na:
    call x3_needles                  ; the worker's peaks into the needles
    mov al, [x3_md]
    cmp al, MD_LIVE
    je .play
    cmp al, MD_FILE
    je .play
    cmp al, MD_STEP
    je .step
    cmp al, MD_CONV
    jne .out
    jmp x3c_tick                    ; (x3conv.inc) the progress, throttled
.step:
    call x3_steptick                ; the ring drained? then the new rung
    jmp short .out
.play:
    call x3_clock                   ; elapsed from the bytes PLAYED
    call x3_gauge
.out:
    clc
    ret

; -----------------------------------------------------------------------------
; x3_e_play - entry AL, from its start: a cache that matches plays with no
; decoding; otherwise the ladder decides - live, or the decode-first dialog
; -----------------------------------------------------------------------------
x3_e_play:
    cmp al, [x3_qn]
    jae .out
    push ax
    call x3_stopall                 ; whatever was under way
    pop ax
    call x3_cue1                    ; the LCD: title, total, READY
    xor ax, ax
    mov [x3_startms], ax            ; from the start
    mov [x3_startms+2], ax
    mov byte [x3_wantdf], 0
    call x3_go                      ; CF = 1: x3_st says why
.out:
    clc
    ret

; x3_go - the current entry from [x3_startms]: open it, then a cache, the
; ladder, or the dialog. [x3_wantdf] = 1 skips straight to decode first
x3_go:
    call x3_trk_open                ; the stream's info, its fingerprint
    jc .err
    call x3_snddev                  ; what the device is NOW (the tier moves)
    mov al, [x3_card]               ; A CACHE FIRST - with a card, only one in
    cmp byte [x3_wantdf], 0         ; exactly the format this device would be
    je .s                           ; given for its rung (else live may do
    xor al, al                      ; better: the ladder decides, and x3l_done
.s:                                 ; takes any cache before it asks); with no
    mov [x3c_strict], al            ; card, or for D, any it can play
    call x3c_cachefind              ; (x3conv.inc)
    jc .ladder
    jmp x3_file_play                ; ...played, nothing decoded
.ladder:
    jmp x3l_start                   ; CHECK: the ladder (decode first, too,
                                    ; wants its profile and its speed)
.err:
    stc
    ret

; x3_e_cue - entry AL into the state, not playing: READY
x3_e_cue:
    cmp al, [x3_qn]
    jae .out
    push ax
    call x3_stopall
    pop ax
    call x3_cue1
.out:
    clc
    ret

; x3_cue1 - AL into x3_st: its title and duration, READY. keeps nothing
x3_cue1:
    mov [x3_st+X3S_CUR], al
    call x3_qent                    ; SI = its record
    mov ax, [si+QE_DUR]
    mov [x3_st+X3S_TOTAL], ax
    and byte [x3_st+X3S_FLAGS], ~(X3SF_EST | X3SF_NOTOT | X3SF_DEVLIM)
    cmp ax, 0xFFFF
    jne .tot
    or byte [x3_st+X3S_FLAGS], X3SF_NOTOT
.tot:
    mov al, [si+QE_FLAGS]
    and al, X3SF_EST
    or [x3_st+X3S_FLAGS], al
    xor ax, ax
    mov [x3_st+X3S_ELAP], ax
    mov [x3_st+X3S_UNDER], ax
    mov [x3_st+X3S_SPEED], ax
    mov [x3_st+X3S_ERR], ax
    mov byte [x3_st+X3S_CHANS], 0
    mov byte [x3_st+X3S_PCT], 0
    mov byte [x3_st+X3S_QUAL], X3Q_AUTO
    add si, QE_TITLE
    mov di, x3_st+X3S_TITLE
    mov cx, 40
    call x3_putn
    mov byte [di], 0
    mov byte [x3_st+X3S_STATE], X3ST_READY
    mov byte [x3_md], MD_IDLE
    ret

; -----------------------------------------------------------------------------
; x3_e_pause / x3_e_resume - a pause CLOSES the stream where the card is and
; keeps that position; a resume is a seek to it. Crisp on every path: the
; grant ring's tail is never left playing behind a Pause, and the MP3 path's
; resume rebuilds its histories with the same pre-roll a seek does
; -----------------------------------------------------------------------------
x3_e_pause:
    mov al, [x3_md]
    cmp al, MD_LIVE
    je .snd
    cmp al, MD_FILE
    jne .out
.snd:
    call x3_pos                     ; DX:AX = the sample PLAYED
    mov [x3_ppos], ax
    mov [x3_ppos+2], dx
    mov al, [x3_md]
    mov [x3_pmd], al
    call x3_snd_close               ; parks the worker, closes, frees the grant
    mov byte [x3_md], MD_IDLE
    mov byte [x3_st+X3S_STATE], X3ST_PAUSED
    mov byte [x3_paused], 1
.out:
    clc
    ret

x3_e_resume:
    cmp byte [x3_paused], 0
    je .out
    mov byte [x3_paused], 0
    cmp byte [aps_on], 0            ; a speaker session, paused on the desktop:
    je .snd                         ; its bracket again, from the very sample
    mov byte [x3_md], MD_SPK        ; (os88spk_go plays from CONS)
    mov byte [x3_st+X3S_STATE], X3ST_FILE
    call aps_resume
    jmp x3_spk_back
.snd:
    mov ax, [x3_ppos]
    mov dx, [x3_ppos+2]
    cmp byte [x3_pmd], MD_FILE
    je .file
    call x3_live_at                 ; the MP3 path: a seek to the sample
    jmp short .out
.file:
    call x3_file_at
.out:
    clc
    ret

; -----------------------------------------------------------------------------
; x3_e_stop - everything under way stops; the track stays cued (READY)
; -----------------------------------------------------------------------------
x3_e_stop:
    call x3_stopall
    mov al, X3ST_READY
    cmp byte [x3_st+X3S_CUR], 0xFF
    jne .s
    mov al, X3ST_IDLE
.s:
    mov [x3_st+X3S_STATE], al
    clc
    ret

; x3_stopall - whatever runs, stopped and released: the stream, a conversion
; (cancelled: its temporary file deleted), a speaker session, the dialogs, the
; claims. The decoder's parts stay (a play that follows wants them). Keeps BX
x3_stopall:
    push bx
    cmp byte [x3_md], MD_CONV
    jne .nconv
    call x3c_abort                  ; (x3conv.inc) the temporary file deleted
.nconv:
    cmp byte [x3_md], MD_CHOOSE
    jne .nch
    call x3c_dlgclose
.nch:
    call x3_snd_close
    call aps_close                  ; a paused speaker session ends
    call x3_park
    call x3_la_free
    xor ax, ax
    mov [x3_st+X3S_ELAP], ax
    mov [x3_st+X3S_METL], al
    mov [x3_st+X3S_METR], al
    mov [x3_st+X3S_LEVEL], al
    mov byte [x3_st+X3S_CHANS], 0
    mov byte [x3_st+X3S_QUAL], X3Q_AUTO
    mov byte [x3_md], MD_IDLE
    mov byte [x3_paused], 0
    mov byte [x3l_arm], 0
    mov byte [x3_rampout], 0
    mov byte [x3_rampin], 0
    pop bx
    ret

; -----------------------------------------------------------------------------
; x3_e_seek - AX = the second to go to. LIVE: re-synced at the target frame
; less X3_PREROLL frames decoded to nothing (SPEC.md 121.9); a finished WAV
; seeks directly; a paused play moves its resume point; a conversion is not
; playback and does not seek
; -----------------------------------------------------------------------------
x3_e_seek:
    mov cx, [x3_st+X3S_TOTAL]
    cmp cx, 0xFFFF
    je .clamp0
    cmp ax, cx
    jb .in
    mov ax, cx
    or ax, ax
    jz .in
    dec ax
    jmp short .in
.clamp0:
.in:
    mov [x3_st+X3S_ELAP], ax
    call x3_s2samp                  ; DX:AX = that second, in output samples
    cmp byte [x3_paused], 0
    je .live
    cmp byte [aps_on], 0
    jne .spk
    mov [x3_ppos], ax               ; paused: the resume point moves
    mov [x3_ppos+2], dx
    jmp short .out
.spk:
    call x3_spk_seek                ; a paused speaker session: from there
    jmp short .out
.live:
    cmp byte [x3_md], MD_LIVE
    jne .nlive
    call x3_live_at
    jmp short .out
.nlive:
    cmp byte [x3_md], MD_FILE
    jne .out
    call x3_file_at
.out:
    clc
    ret

; x3_s2samp - AX seconds -> DX:AX output samples at [x3_orate]. keeps CX
x3_s2samp:
    push cx
    mov cx, [x3_orate]
    mul cx
    pop cx
    ret

; -----------------------------------------------------------------------------
; x3_e_qual - the user's quality changed. Playing live: a new check at the
; position, as the ladder's own override would be (SPEC.md 121.5)
; -----------------------------------------------------------------------------
x3_e_qual:
    cmp byte [x3_md], MD_LIVE
    jne .out
    call x3_pos                     ; the sample played
    call x3_samp2ms                 ; DX:AX = its millisecond
    mov [x3_startms], ax
    mov [x3_startms+2], dx
    call x3_snd_close
    mov byte [x3_wantdf], 0
    call x3l_start
.out:
    clc
    ret

; -----------------------------------------------------------------------------
; x3_e_vol - the knob moved: the mixer's master where there is one, else the
; decoder's own gain (X3O_GAIN) at the next OPEN or granule boundary
; -----------------------------------------------------------------------------
x3_e_vol:
    test byte [x3_st+X3S_FLAGS], X3SF_VOLOK
    jz .out
    cmp byte [x3_mixer], 0
    je .gain
    mov al, [x3_st+X3S_VOL]         ; 0..10 -> 0..100%
    mov ah, 10
    mul ah
    mov cl, al
    mov ch, al
    xor al, al                      ; channel 0, the master
    push bx
    mov bh, DRVC_SOUND
    mov bl, SNDV_MIXSET
    call OSAPI_DRV_CALL
    pop bx
    jmp short .out
.gain:
    call x3_gainset                 ; the Q8 the next granule is made with
.out:
    clc
    ret

; x3_volsense - SND_CAP_MIXER: the knob is the master, read back now; else
; the decoder's gain, which works only while MP3 is decoded (x3_flagsvol)
x3_volsense:
    push ax
    push bx
    push cx
    push dx
    mov byte [x3_mixer], 0
    call OSAPI_SND_CAPS
    test ax, SND_CAP_MIXER
    jz .gain
    xor al, al
    mov bh, DRVC_SOUND
    mov bl, SNDV_MIXGET
    call OSAPI_DRV_CALL             ; CL = the master's left, percent
    jc .gain
    mov byte [x3_mixer], 1
    mov al, cl
    xor ah, ah
    add ax, 5
    mov cl, 10
    div cl
    cmp al, X3_VMAX
    jbe .v
    mov al, X3_VMAX
.v:
    mov [x3_st+X3S_VOL], al
.gain:
    call x3_gainset
    call x3_flagsvol
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; x3_flagsvol - X3SF_VOLOK: a mixer, or MP3 being decoded into the card - a
; finished file played with no mixer has nothing to turn (SPEC.md 121.8).
; keeps everything
x3_flagsvol:
    and byte [x3_st+X3S_FLAGS], ~X3SF_VOLOK
    cmp byte [x3_mixer], 0
    jne .ok
    cmp byte [x3_md], MD_FILE
    je .out
    cmp byte [x3_md], MD_SPK
    je .out
.ok:
    or byte [x3_st+X3S_FLAGS], X3SF_VOLOK
.out:
    ret

; x3_gainset - [x3_gain] = the knob's Q8 gain (detent 7 is unity; X3O_GAIN 0
; is the bit-exact model, so unity is sent as 0). keeps everything
x3_gainset:
    push ax
    push bx
    mov bl, [x3_st+X3S_VOL]
    xor bh, bh
    shl bx, 1
    mov ax, [x3_gaintab+bx]
    cmp byte [x3_mixer], 0
    je .g
    xor ax, ax                      ; the mixer turns it: the model stays exact
.g:
    mov [x3_gain], ax
    pop bx
    pop ax
    ret

x3_gaintab: dw 1, 16, 32, 64, 96, 144, 200, 0, 320, 400, 512   ; 0..10, Q8

; x3_e_cancel - Esc: a conversion's Cancel (x3_bok has said it is one)
x3_e_cancel:
    cmp byte [x3_md], MD_CONV
    jne .out
    call x3c_cancel
.out:
    clc
    ret

; -----------------------------------------------------------------------------
; x3_e_qadd - entry AL joined the queue: the front end's probe gives it its
; title and duration (and whether the duration is a count or a guess)
; -----------------------------------------------------------------------------
x3_e_qadd:
    push bx
    mov bl, al
    xor bh, bh
    mov byte [x3_qfpok+bx], 0       ; its fingerprint, when first asked for
    call x3_qent                    ; SI = the record
    push si
    mov di, x3p_qinfo
    mov bl, [si+QE_VOL]
    mov dx, [si+QE_DIR]
    add si, QE_NAME
    call x3p_probe                  ; CF = 0: the record complete
    pop si
    jc .out
    cmp byte [x3p_qinfo+X3I_REF], 0
    jne .out                        ; refused: Play says why, by name
    mov ax, [x3p_qinfo+X3I_DUR]
    mov [si+QE_DUR], ax
    mov byte [si+QE_FLAGS], 0
    test byte [x3p_qinfo+X3I_FLAGS], X3IF_EXACT
    jnz .exact
    mov byte [si+QE_FLAGS], X3SF_EST
.exact:
    cmp byte [x3p_qinfo+X3I_TITLE], 0
    je .out                         ; no tag: the name's stem stays
    push si
    lea di, [si+QE_TITLE]
    mov si, x3p_qinfo+X3I_TITLE
    mov cx, 40
    call x3_putn
    mov byte [di], 0
    pop si
.out:
    pop bx
    clc
    ret

; -----------------------------------------------------------------------------
; x3_e_qdrop - entry AL is leaving: the one shown stops and goes; the
; per-entry fingerprints move up with the list
; -----------------------------------------------------------------------------
x3_e_qdrop:
    push bx
    mov ah, [x3_st+X3S_CUR]
    cmp ah, 0xFF
    je .fp
    cmp al, ah
    ja .fp
    jb .up
    push ax
    call x3_stopall
    pop ax
    mov byte [x3_st+X3S_CUR], 0xFF
    mov byte [x3_st+X3S_STATE], X3ST_IDLE
    jmp short .fp
.up:
    dec byte [x3_st+X3S_CUR]
.fp:
    call x3_qfpdel                  ; AL's fingerprint row out, the rest up
    pop bx
    clc
    ret

x3_e_qclear:
    call x3_stopall
    mov byte [x3_st+X3S_CUR], 0xFF
    mov byte [x3_st+X3S_STATE], X3ST_IDLE
    mov byte [x3_c_batch], 0        ; a batch is the queue's: it ends with it
    push di
    push cx
    mov di, x3_qfpok
    mov cx, X3_QMAX
    xor al, al
    call x3_fillb
    pop cx
    pop di
    clc
    ret

; x3_qfpdel - AL's per-entry rows (x3_qfpok, x3_qfp) out, the later ones up
x3_qfpdel:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    cld
    xor ah, ah
    mov bx, X3_QMAX - 1
    sub bx, ax                      ; BX = the rows after it
    jbe .done
    mov di, ax
    add di, x3_qfpok
    lea si, [di+1]
    mov cx, bx
    rep movsb
    mov cx, X3_FPSZ
    mul cx
    add ax, x3_qfp
    mov di, ax
    lea si, [di+X3_FPSZ]
    mov ax, bx
    mul cx
    mov cx, ax
    rep movsb
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE WAKE AND THE CLOSE
; =============================================================================

; -----------------------------------------------------------------------------
; x3_onwake - OSAPI_WM_ONWAKE: the worker's requests, serviced in order. No
; lock arrives with it; the file I/O needs none, and what draws takes it
; in:  SI = the window
; -----------------------------------------------------------------------------
x3_onwake:
    push si
    test byte [x3_req], X3R_READ    ; 1. A GULP - time-critical, first
    jz .nrd
    call x3_refill                  ; (timed: the disk's measured share)
    and byte [x3_req], ~X3R_READ    ; cleared LAST (SPEC.md 77.1)
    call ap_la_avail
    cmp ax, AP_LA_LOW
    jae .nrd
    cmp byte [ap_src_done], 0
    jne .nrd
    mov bx, [x3_win]
    call OSAPI_WM_WAKE              ; still low: come back next pass
.nrd:
    test byte [x3_req], X3R_WRITE   ; 2. A CONVERSION HALF: written
    jz .nwr
    call x3c_write                  ; (x3conv.inc) clears the bit itself
.nwr:
    test byte [x3_req], X3R_END     ; 2b. A CONVERSION'S END: its finishing
    jz .nfin                        ; I/O, lock-free (x3conv.inc)
    cmp byte [x3_md], MD_CONV
    jne .nfin
    call x3c_final
.nfin:
    test byte [x3_req], X3R_END | X3R_STEP | X3R_CAL | X3R_FAIL | X3R_UIOP
    jz .out
    call OSAPI_GFX_LOCK             ; 3. what changes the state, and draws
    test byte [x3_req], X3R_UIOP
    jz .nu
    and byte [x3_req], ~X3R_UIOP
    call x3c_uiop                   ; a dialog's action, billed to us
.nu:
    test byte [x3_req], X3R_FAIL
    jz .nf
    and byte [x3_req], ~X3R_FAIL
    call x3_wfailed
.nf:
    test byte [x3_req], X3R_CAL
    jz .nc
    and byte [x3_req], ~X3R_CAL
    call x3l_probed                 ; the ladder's next step
.nc:
    test byte [x3_req], X3R_STEP
    jz .ns
    and byte [x3_req], ~X3R_STEP
    call x3_stepbegin               ; drain, then the rung below
.ns:
    test byte [x3_req], X3R_END
    jz .ne
    and byte [x3_req], ~X3R_END
    call x3_ended
.ne:
    mov bx, [x3_win]
    call x3f_update
    call OSAPI_GFX_UNLOCK
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; x3_onclose - the close negotiator (SPEC.md 75.1): everything under way is
; stopped and released FIRST - a conversion's temporary file deleted, the
; stream closed, the claims and the parts given back - then the close goes on
; in:  SI = the window, gfx lock held. out: CF = 0, the close proceeds
; -----------------------------------------------------------------------------
x3_onclose:
    push bx
    call x3_stopall
    call x3c_dlgclose
    call x3c_dfclose
    call aps_free
    call x3_be_drop
    pop bx
    clc
    ret

; =============================================================================
; THE TRACK: its stream, the ring, the fingerprint
; =============================================================================

; -----------------------------------------------------------------------------
; x3_trk_open - the current entry's stream: x3p_open over reads of the file
; (the front end asks, this reads), the ring claimed, the fingerprint made.
; out: CF = 1 refused, x3_st says why (ERROR)
; -----------------------------------------------------------------------------
x3_trk_open:
    mov al, [x3_st+X3S_CUR]
    call x3_qent                    ; SI = its record
    mov [x3_trkent], si
    mov dx, [si+QE_DIR]
    mov bl, [si+QE_VOL]
    mov [ap_trk_dir], dx
    mov [ap_trk_vol], bl
    push si
    add si, QE_NAME                 ; the name, ours to keep while it plays
    mov di, x3_fname
    mov cx, 13
    call x3_putn
    mov byte [di], 0
    pop si
    mov word [apw_name], x3_fname
    call OSAPI_FILE_GOTO_QM         ; stand in its folder
    jc .noread
    mov ax, X3_LABIG
    call x3_lasize
    call x3_la_claim                ; the ring: the open's reads land in it
    jc .nomem
    call x3_cluget                  ; [ap_clu] for this volume
    jc .noread
    cmp word [ap_clu], X3_LABIG / 2 ; an MP3 wants two clusters in its ring
    ja .clus                        ; (x3_plan): 32 KB clusters, by name
    mov si, x3_fname
    call x3_findsz                  ; DX:AX = its bytes (FIND: the flags too)
    jc .noread
    test byte [x3_find+22], OSAPI_FIND_CZ
    jnz .packed                     ; STORED PACKED: READ_SEQ hands over the
                                    ; wrapper, not the stream (SPEC.md
                                    ; 20.14.3) - said, never decoded as noise
    mov [x3_flen], ax
    mov [x3_flen+2], dx
    mov di, x3_info
    call x3p_inew
    xor ax, ax                      ; THE OPEN: the first read at offset 0,
    xor dx, dx                      ; then whatever the machine asks for
.rd:
    call x3_readat                  ; DX:AX = an offset -> ES:BX, CX bytes
    jc .noread
    mov di, x3_info                 ; (ES:BX: x3_readat's)
    call x3p_open
    jc .noread
    cmp ax, X3P_NEED
    jne .done
    mov ax, [x3_info+X3I_WANT]
    mov dx, [x3_info+X3I_WANT+2]
    jmp short .rd
.done:
    mov al, [x3_info+X3I_REF]
    or al, al
    jz .ok
    call x3p_reftext                ; SI = 'FREE FORMAT', 'LAYER II'...
    mov [x3_st+X3S_ERR], si
    jmp short .fail
.ok:
    call x3_trkinfo                 ; the LCD: duration, its '~'
    call x3c_fprint                 ; (x3conv.inc) the fingerprint, once
    jc .noread
    clc
    ret
.clus:
    mov word [x3_st+X3S_ERR], x3_s_clus
    jmp short .fail
.packed:
    mov word [x3_st+X3S_ERR], x3_s_packed
    jmp short .fail
.nomem:
    mov word [x3_st+X3S_ERR], ap_s_nomem
    jmp short .fail
.noread:
    mov word [x3_st+X3S_ERR], x3_s_noread
.fail:
    mov byte [x3_st+X3S_STATE], X3ST_ERROR
    stc
    ret

; x3_trkinfo - the opened stream's duration into x3_st and the queue entry
x3_trkinfo:
    push si
    mov ax, [x3_info+X3I_DUR]
    mov [x3_st+X3S_TOTAL], ax
    and byte [x3_st+X3S_FLAGS], ~(X3SF_EST | X3SF_NOTOT)
    mov si, [x3_trkent]
    push word [si+QE_DUR]           ; (what the queue's row shows now)
    push word [si+QE_FLAGS]
    mov [si+QE_DUR], ax
    mov byte [si+QE_FLAGS], 0
    test byte [x3_info+X3I_FLAGS], X3IF_EXACT
    jnz .x
    or byte [x3_st+X3S_FLAGS], X3SF_EST
    mov byte [si+QE_FLAGS], X3SF_EST
.x:
    pop ax                          ; the stream's own figures differ from the
    cmp al, [si+QE_FLAGS]           ; probe's: the row is news (its key is the
    pop ax                          ; queue's generation)
    jne .gen
    cmp ax, [si+QE_DUR]
    je .same
.gen:
    inc byte [x3_qgen]
.same:
    mov al, [x3_info+X3I_NCH]
    mov [x3_snch], al
    mov ax, [x3_info+X3I_RATE]
    mov [x3_srate], ax
    pop si
    ret

; x3_readat - DX:AX = a file offset: up to 32 KB of x3_fname from the cluster
; it lies in, into the ring's claim. out: ES:BX = the offset's byte there, CX
; = bytes from it, DX:AX as given; CF = 1 nothing could be read
x3_readat:
    push si
    push di
    mov [x3_rdo], ax
    mov [x3_rdo+2], dx
    mov cx, [ap_clu]
    dec cx
    mov di, ax
    and di, cx                      ; DI = into the cluster
    not cx
    and ax, cx                      ; DX:AX = the cluster's start
    mov es, [ap_la_seg]
    xor bx, bx
    mov cx, AP_LA_SZ
    mov si, x3_fname
    push di
    mov di, ap_rd_cur
    call os88_rseq                  ; CF, DX:AX = bytes
    pop di
    jc .no
    or dx, dx
    jnz .full
    sub ax, di
    jbe .no                         ; nothing at the offset: past the end
    mov cx, ax
    jmp short .ok
.full:
    mov cx, AP_LA_SZ
    sub cx, di
.ok:
    mov bx, di
    mov ax, [x3_rdo]
    mov dx, [x3_rdo+2]
    clc
    jmp short .out
.no:
    stc
.out:
    pop di
    pop si
    ret

; x3_fsize - SI = a NUL name in the folder we stand in -> DX:AX its bytes
; (OSAPI_FILE_READ's size probe: no data I/O). CF = 1 not there
x3_fsize:
    push bx
    push cx
    push es
    push ds
    pop es
    xor bx, bx
    xor cx, cx
    xor dx, dx
    call OSAPI_FILE_READ            ; FERR_BIG: DX = KB, or CF = 0, DX:AX = 0
    jnc .got                        ; (an empty file)
    cmp ax, FERR_BIG
    jne .no
    mov si, x3_fname                ; the exact figure: FIND it
    call x3_findsz
    jc .no
.got:
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop cx
    pop bx
    ret

; x3_findsz - SI = a NUL name -> DX:AX its bytes, from OSAPI_FILE_FIND. CF = 1
; not in the folder we stand in. keeps SI
x3_findsz:
    push bx
    push cx
    push di
    push es
    push ds
    pop es
    xor cx, cx
.f:
    mov di, x3_find
    call OSAPI_FILE_FIND
    jc .no
    push si
    push cx
    mov di, x3_find
    call x3_nameeq
    pop cx
    pop si
    jne .f
    mov ax, [x3_find+18]
    mov dx, [x3_find+20]
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop di
    pop cx
    pop bx
    ret

; x3_nameeq - NUL strings at SI and DI equal, any case? ZF = 1. keeps all
x3_nameeq:
    push ax
    push si
    push di
.l:
    mov al, [si]
    mov ah, [di]
    cmp al, 'a'
    jb .a
    cmp al, 'z'
    ja .a
    sub al, 0x20
.a:
    cmp ah, 'a'
    jb .b
    cmp ah, 'z'
    ja .b
    sub ah, 0x20
.b:
    cmp al, ah
    jne .out
    inc si
    inc di
    or al, al
    jnz .l
.out:
    pop di
    pop si
    pop ax
    ret

; x3_cluget - [ap_clu] = the bytes a cluster of the volume we stand in, from
; a per-volume byte of sectors a cluster (OSAPI_FILE_DFREE counts the whole
; FAT, ~105 ms on an XT's 20 MB disk, so it is asked once a volume). CF = 1:
; a cluster no gulp holds (past FAT16's 32 KB)
x3_cluget:
    push ax
    push bx
    push cx
    push dx
    call OSAPI_FILE_HERE            ; BL = the volume
    mov bh, 0
    mov al, [x3_spc+bx]
    or al, al
    jnz .have
    push bx
    call OSAPI_FILE_DFREE           ; BX = sectors a cluster
    mov ax, bx
    pop bx
    jc .no
    mov [x3_spc+bx], al
.have:
    xor ah, ah
    mov cl, 9
    shl ax, cl                      ; x 512
    cmp ax, AP_RD_CHUNK
    ja .no
    mov [ap_clu], ax
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
; THE RING'S CLAIM: 32 KB and a granule's PCM after it, taken when a track is opened and given back at stop
; (the Audio Player holds its for life; XTMP3's working set is counted against
; 400,000 bytes on an XT, SPEC.md 121.10)
; -----------------------------------------------------------------------------
x3_la_claim:
    cmp word [ap_la_seg], 0
    jne .ok
    push ax
    push dx
    mov ax, [x3_lasz]               ; the ring, the PCM (X3_PCMRES) and a
    mov dl, 10                      ; conversion's stage (x3_laxkb, x3conv
    xchg cx, dx                     ; .inc), ONE claim: 8 per owner is the
    shr ax, cl                      ; heap's limit (SPEC.md 50), and the XT
    xchg cx, dx                     ; backend's work alone is 5
    add ax, (X3_PCMRES + 1023) / 1024
    add ax, [x3_laxkb]
    call OSAPI_MEM_CLAIM            ; 512-aligned by construction (guard 6b)
    jc .no
    mov [ap_la_seg], dx
    mov dx, [x3_lamask]
    mov [x3_ring+X3R_MASK], dx
    call x3_memck
    pop dx
    pop ax
.ok:
    clc
    ret
.no:
    pop dx
    pop ax
    stc
    ret

; x3_lasize - AX = the ring's size (a power of two): the claim given back
; first when it was another size. keeps everything
x3_lasize:
    cmp ax, [x3_lasz]
    je .o
    call x3_la_free
    mov [x3_lasz], ax
    push ax
    dec ax
    mov [x3_lamask], ax
    inc ax
    shr ax, 1
    mov [x3_lalw], ax
    pop ax
.o:
    ret

x3_la_free:
    push dx
    mov dx, [ap_la_seg]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
    mov word [ap_la_seg], 0
.out:
    pop dx
    ret

; -----------------------------------------------------------------------------
; x3_la_at - aplook.inc's producer aimed at byte DX:AX of the file, with the
; data range [ap_data_off, ap_data_end) already set: the cursor new, the ring
; empty at the head skip, X3R_POS the offset. keeps everything
; -----------------------------------------------------------------------------
x3_la_at:
    push ax
    push bx
    push cx
    push dx
    push di
    mov [x3_ring+X3R_POS], ax
    mov [x3_ring+X3R_POS+2], dx
    mov bx, ax                      ; ap_got = offset - ap_data_off: the bytes
    mov cx, dx                      ; of data behind it (the tail clamp's)
    sub bx, [ap_data_off]
    sbb cx, [ap_data_off+2]
    mov [ap_got], bx
    mov [ap_got+2], cx
    mov cx, [ap_clu]
    dec cx
    mov bx, ax
    and bx, cx                      ; BX = the head skip
    not cx
    and ax, cx
    mov [ap_rd_pos], ax
    mov [ap_rd_pos+2], dx
    mov [ap_skip], bx
    mov [ap_la_wr], bx              ; empty, at the skip: the first gulp lands
    mov [ap_la_rd], bx              ; at index 0 (aplook.inc)
    mov byte [ap_src_done], 0
    mov di, ap_rd_cur
    call os88_rseq_new
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; x3_la_mp3 - the producer over the MP3's audio, from byte DX:AX of it
x3_la_mp3:
    push ax
    push dx
    mov ax, [x3_info+X3I_AUDIO]
    mov dx, [x3_info+X3I_AUDIO+2]
    mov [ap_data_off], ax
    mov [ap_data_off+2], dx
    mov ax, [x3_info+X3I_AEND]
    mov dx, [x3_info+X3I_AEND+2]
    mov [ap_data_end], ax
    mov [ap_data_end+2], dx
    mov word [apw_name], x3_fname
    pop dx
    pop ax
    jmp x3_la_at

; x3_prime - the ring filled before a play or a probe, on the UI task (the
; worker then tops it up through the handshake): gulps until AX bytes are
; queued or the file is in. Each one timed (x3_refill)
x3_prime:
    push cx
    mov cx, X3_LABIG / AP_RD_CHUNK + 1
.l:
    cmp byte [ap_src_done], 0
    jne .out
    push ax
    call ap_la_avail
    mov dx, ax
    pop ax
    cmp dx, ax
    jae .out
    call x3_refill
    loop .l
.out:
    pop cx
    ret

; x3_refill - one gulp, timed by pit_now: [x3_dkpit] and [x3_dkby] are the
; disk's measured cost (calibration charges it per compressed byte). A read
; error ends the data and is remembered: the conversion stops on it
x3_refill:
    push ax
    push bx
    push cx
    push dx
    call pit_now
    mov [x3_rft0], ax
    mov [x3_rft0+2], dx
    push word [ap_la_wr]
    mov word [ap_msg], 0
    call ap_refill_chunk
    pop dx                          ; DX = WR before
    mov ax, [ap_la_wr]
    sub ax, dx                      ; AX = bytes this gulp published
    add [x3_dkby], ax
    adc word [x3_dkby+2], 0
    mov bx, x3_rft0
    call x3_pitd
    add [x3_dkpit], ax
    adc [x3_dkpit+2], dx
    cmp word [ap_msg], apw_e_io
    jne .out
    mov byte [x3_rderr], 1          ; aplook.inc's own verdict: a bad read
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE DEVICE (SPEC.md 121.6)
; =============================================================================

; x3_snddev - what plays sound NOW (the Control Panel's tier moves): [x3_card]
; 1 a stream-capable card, else the speaker or a Covox; [x3_caps] its caps
x3_snddev:
    push ax
    call OSAPI_SND_CAPS
    mov [x3_caps], ax
    mov byte [x3_card], 0
    test ax, SND_CAP_PCM_BG
    jz .out
    mov byte [x3_card], 1
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; x3_ofmt - a rung's OUTPUT: [x3_orate], [x3_och], [x3_obits] and DEVLIM, for
; rung AL on this source and device. The rung decides the rate (the divisor)
; and, for an XT rung, mono and eight bits; the DEVICE decides channels and
; bits below that, and says so (DEVLIM - the LCD's DEVICE, SPEC.md 121.5).
; out: CF = 1 the device cannot play the rate (the rung is skipped)
; -----------------------------------------------------------------------------
x3_ofmt:
    push ax
    push bx
    push cx
    push dx
    call x3_rungrow                 ; BX -> the rung's row
    and byte [x3_st+X3S_FLAGS], ~X3SF_DEVLIM
    call x3_rungdiv                 ; CL = the divisor
    mov [x3_odiv], cl
    mov ax, [x3_srate]
    xor dx, dx
    xor ch, ch
    div cx
    mov [x3_orate], ax
    mov byte [x3_och], 1            ; an XT rung: mono, eight bits, the
    mov byte [x3_obits], 8          ; backend's own (not the device's)
    cmp byte [bx+RG_BE], X3K_XT
    je .rate
    mov al, [x3_snch]               ; STANDARD: the source's channels and
    mov [x3_och], al                ; sixteen bits, where the device has them
    mov byte [x3_obits], 16
    cmp byte [x3_card], 0
    je .spk
    test word [x3_caps], SND_CAP_PCM16
    jnz .b16
    mov byte [x3_obits], 8
    or byte [x3_st+X3S_FLAGS], X3SF_DEVLIM
.b16:
    cmp byte [x3_och], 2
    jne .rate
    test word [x3_caps], SND_CAP_STEREO
    jnz .rate
    mov byte [x3_och], 1
    or byte [x3_st+X3S_FLAGS], X3SF_DEVLIM
    jmp short .rate
.spk:
    mov byte [x3_och], 1            ; the speaker and a Covox: eight bits,
    mov byte [x3_obits], 8          ; mono - the DEVICE's, and said
    or byte [x3_st+X3S_FLAGS], X3SF_DEVLIM
    clc                             ; (a finished file: any rate it resamples)
    jmp short .out
.rate:
    cmp byte [x3_card], 0
    je .ok
    mov ax, [x3_orate]
    cmp ax, X3_DEVMIN
    jb .no
    mov cx, X3_DEVMAX
    test word [x3_caps], SND_CAP_PCM_HI
    jz .top
    mov cx, 44100
    cmp byte [x3_och], 2            ; an SB Pro's stereo: 22,050 a channel
    jne .top                        ; (SPEC.md 34.5.5). A DSP 4.xx takes
    test word [x3_caps], SND_CAP_PCM16 | SND_CAP_ADPCM4Q    ; 44,100 a
    jnz .top                        ; channel with or without its 16-bit DMA
    mov cx, X3_DEVMAX               ; channel - ADPCM4Q is the SDK's "the DSP
                                    ; answers 4.xx", which an SB Pro never is
.top:
    cmp ax, cx
    ja .no
.ok:
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

; x3_fbytes - AX = bytes a sample FRAME of the output: 1, 2 or 4. keeps all
x3_fbytes:
    mov al, [x3_och]
    cmp byte [x3_obits], 16
    jne .b
    shl al, 1
.b:
    xor ah, ah
    ret

; =============================================================================
; THE SND STREAM (UI side): open with a pre-roll, close
; =============================================================================

; -----------------------------------------------------------------------------
; x3_snd_open - the grant, the pre-roll (the worker's own producer, run here),
; the format (verb 11, right before verb 0), the stream, the worker.
; in:  [x3_sjob] says which producer (WJ_LIVE / WJ_FILE)
; out: CF = 1 refused, x3_st says why
; -----------------------------------------------------------------------------
x3_snd_open:
    mov byte [x3_job], WJ_NONE      ; (the worker sits it out until the open)
    call x3_fbytes
    mov [x3_fby], ax
%ifdef X3STUB
    mov al, [x3h_cfg+X3SB_FAILR]    ; (the test hook: this rung's live start
    or al, al                       ; out of memory - tests/xtmp3eng.py's
    jz .nf                          ; rungfail leg)
    cmp byte [x3_sjob], WJ_LIVE
    jne .nf
    cmp al, [x3_rung]
    je .nomem
.nf:
%endif
    call x3_ringget                 ; (sized by the bytes a second)
    jc .nomem
    mov ax, [x3_orate]              ; THE SLEEP'S FLOOR: a sleep of one tick
    mul word [x3_fby]               ; is up to two (110 ms, 1/9 of a second),
    mov cx, 9                       ; the card's block IRQ wants the NEXT
    div cx                          ; block fed (34.5.2), and the first half
    add ax, 3 * X3_HALF             ; after a wake takes a rung near 1.00x a
    mov [x3_slp], ax                ; half's time to make - so the worker
                                    ; sleeps on that much more than three
                                    ; blocks, and on 176 KB/s's 32 KB ring
                                    ; never: a half-ring rule ran it dry at 1.07x
    xor ax, ax
    mov [x3_total], ax
    mov [x3_staged], ax
    mov [x3_cons], ax
    mov [x3_played], ax
    mov [x3_played+2], ax
    mov [x3_lasthalf], al
    mov [x3_sended], al
    mov [x3_drvund], al
    mov [x3_rampst], al             ; (a step's ramp out is the old stream's)
    mov [x3_wslow], al              ; a new rung: a new window
    mov [x3_wg], ax
    mov [x3_wacc], ax
    mov [x3_wacc+2], ax
    mov byte [x3_fill], 1           ; the producer, on the UI task: whole
    mov cx, [x3_ring_n]             ; halves, the ring less one
.pre:
    push cx
    call x3_feedhalf                ; CF = 1 nothing more (the end, a refusal)
    pop cx
    jc .preend
    loop .pre
.preend:
    mov byte [x3_fill], 0
    cmp word [x3_total], 0
    je .empty
    xor ah, ah                      ; THE FORMAT, right before verb 0
    cmp byte [x3_och], 2
    jne .m
    or ah, SND_FMT_STEREO
.m:
    cmp byte [x3_obits], 16
    jne .f
    or ah, SND_FMT_16
.f:
    or ah, ah
    jz .open                        ; 8-bit mono: nothing held, nothing asked
    mov al, SND_V_FMT
    call OSAPI_SND_STREAM
    or ax, ax
    jnz .fmterr
.open:
    xor al, al
    mov ah, SND_OPENF_RING
    mov si, [x3_grnt]
    mov cx, [x3_total]
    mov dx, [x3_orate]
    cmp byte [x3_ext], 0
    je .op
    mov ah, SND_OPENF_RING + SND_OPENF_EXT + (3 << SND_OPENF_RLSH)
    mov di, [x3_xseg]               ; THE EXTERNAL RING: ours, at DI:0, its
    push es                         ; total in its control word
    mov es, di
    mov [es:X3_XRL+SND_EXT_TOTAL], cx
    mov word [es:X3_XRL+SND_EXT_CONS], 0
    pop es
.op:
    call OSAPI_SND_STREAM           ; AL = 0, AH = the handle
    or al, al
    jnz .snderr
    mov [x3_hand], ah
    mov byte [x3_sopen], 1
    call x3_memck                   ; (the ring, and the driver's for it)
    call x3_wspawn
    jc .notask
    mov al, [x3_sjob]
    mov [x3_job], al                ; THE WORKER'S, from here
    clc
    ret
.empty:
    mov word [x3_st+X3S_ERR], x3_s_empty
    jmp short .fail
.fmterr:
.snderr:
    mov word [x3_st+X3S_ERR], x3_s_card
    jmp short .fail
.notask:
    mov word [x3_st+X3S_ERR], x3_s_notask
    jmp short .fail
.nomem:
    mov word [x3_st+X3S_ERR], ap_s_nomem
.fail:
    call x3_snd_close
    mov byte [x3_st+X3S_STATE], X3ST_ERROR
    stc
    ret

; -----------------------------------------------------------------------------
; x3_ringget - THE CARD'S RING. A stream of X3_RINGHI bytes a second or more
; plays from an EXTERNAL ring (SPEC.md 34.5.3): 32 KB of our own, page-safe
; and never moved (OSAPI_MEM_CLAIM_DMA_HI), written directly - the driver's
; grant pool tops out at 16 KB, which is 93 ms of 44.1 kHz 16-bit stereo and,
; less the granule a feed must have room for, 62 ms queued at most. Only a
; DSP 3.00 or later plays such a stream (PCM_HI, STEREO, PCM16), so the
; external ring's auto-init is there whenever it is asked for. Everything
; slower takes the grant, as the Audio Player does. out: CF = 1 no room
; -----------------------------------------------------------------------------
X3_XRL      equ 32768

x3_ringget:
    mov byte [x3_ext], 0
    cmp byte [x3_bkind], X3K_XT     ; the XT backend: the small grant, always
    je x3_grant                     ; (x3_grant says why)
    mov ax, [x3_orate]
    mul word [x3_fby]
    or dx, dx
    jnz .ext
    cmp ax, X3_RINGHI
    jb x3_grant
.ext:
    call x3_gfree                   ; (no grant beside it)
    push cx
    push dx
    mov ax, X3_XRL / 1024 + 1       ; the ring and its control words, all in
    mov cx, ax                      ; the page-safe head
    call OSAPI_MEM_CLAIM_DMA_HI
    jc .no
    mov [x3_xseg], dx
    pop dx
    pop cx
    mov byte [x3_ext], 1
    mov word [x3_grnt], 0
    mov word [x3_rsz], X3_XRL
    mov word [x3_rmask], X3_XRL - 1
    mov word [x3_ring_n], X3_XRL / X3_HALF - 1
    clc
    ret
.no:
    pop dx
    pop cx
    stc
    ret

; x3_grant - the SND ring: verb 7, 16 KB else 8. [x3_grnt], [x3_ring],
; [x3_rmask], [x3_ring_n] (halves to pre-roll: the ring's less one)
x3_grant:
    mov cx, X3_RINGKB * 1024
    mov ax, [x3_orate]              ; a fast stream: 32 KB
    mul word [x3_fby]
    or dx, dx
    jnz .big
    cmp ax, X3_RINGHI
    jb .xt
.big:
    mov cx, X3_RINGBIG * 1024
.xt:
    cmp byte [x3_bkind], X3K_XT     ; UNDER THE XT BACKEND the grant is what
    jne .want                       ; x3_plan budgeted at any rate (16 KB, or
    mov cx, [x3_xtgr]               ; 8): its machine has 400,000 bytes for
                                    ; everything (SPEC.md 121.10), never the
                                    ; external ring
.want:
    cmp byte [x3_ghave], 0
    je .try
    cmp cx, [x3_rsz]                ; one held already: the size this stream
    je .have                        ; wants, or given back and taken again
    push cx
    call x3_gfree
    pop cx
    cmp byte [x3_ghave], 0
    jne .have                       ; (refused: the one held it is)
.try:
    mov ax, 7                       ; verb 7, sub-op 0: alloc
    call OSAPI_SND_STREAM           ; SI = the grant
    or ax, ax
    jz .got
    cmp cx, X3_RINGSM * 1024
    jbe .no
    shr cx, 1                       ; (32 -> 16 -> 8)
    jmp short .try
.got:
    mov [x3_grnt], si
    mov [x3_rsz], cx
    mov ax, cx
    dec ax
    mov [x3_rmask], ax
    mov ax, cx
    mov cl, 12
    shr ax, cl                      ; / X3_HALF
    dec ax
    mov [x3_ring_n], ax
    mov byte [x3_ghave], 1
.have:
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; x3_snd_close - the worker parked, the stream closed (verb 2, UI only), the
; grant handed back (SPEC.md 34.6). Harmless when nothing is open. keeps BX
; -----------------------------------------------------------------------------
x3_snd_close:
    push ax
    push si
    call x3_park
    cmp byte [x3_sopen], 0
    je .grant
    mov al, 2
    mov ah, [x3_hand]
    call OSAPI_SND_STREAM
    mov byte [x3_sopen], 0
.grant:
    push dx                         ; the external ring: the stream closed
    mov dx, [x3_xseg]               ; first, then the memory (the card is
    or dx, dx                       ; not reading it now)
    jz .nx
    call OSAPI_MEM_FREE
    mov word [x3_xseg], 0
.nx:
    pop dx
    mov byte [x3_ext], 0
    call x3_gfree
.out:
    pop si
    pop ax
    ret

; x3_gfree - the grant handed back (verb 7, sub-op 1). A refusal keeps it,
; and it is used again. keeps all but AX
x3_gfree:
    cmp byte [x3_ghave], 0
    je .out
    push si
    mov si, [x3_grnt]
    mov ax, 0x0107
    call OSAPI_SND_STREAM
    pop si
    or ax, ax
    jnz .out
    mov byte [x3_ghave], 0
.out:
    ret

; x3_park - the worker's job taken away and its pass waited out. keeps all
x3_park:
    mov byte [x3_job], WJ_NONE
.w:
    cmp byte [x3_wbusy], 0          ; (a granule at most - ~0.3 s on an
    jne .w                          ; XT - and the scheduler moves the worker
                                    ; on: it takes no lock)
.out:
    ret

; x3_wspawn - the worker, once (it may be restarted: SPEC.md 66.6.2).
; out: CF = 1 no task free (the FTPD sentence says so)
x3_wspawn:
    cmp byte [x3_hasw], 0
    jne .ok
    push ax
    push bx
    mov ax, x3_worker
    mov bx, [x3_win]
    call OSAPI_TASK_SPAWN
    jc .no
    mov byte [x3_hasw], 1
    OS88_WORKER_RESTARTABLE_NOKEEP x3_worker
    pop bx
    pop ax
.ok:
    clc
    ret
.no:
    pop bx
    pop ax
    stc
    ret

; =============================================================================
; THE WORKER
; =============================================================================
x3_worker:
.loop:
    mov bx, [x3_win]
    call OSAPI_TASK_ALIVE           ; (dies here when the window closes)
    mov ax, 2                       ; nothing to do: two ticks - no longer,
    cmp byte [x3_job], WJ_NONE      ; since a stream that opens meanwhile
                                    ; has only its pre-roll until this wakes
    je .sleep
    cmp byte [x3_job], WJ_CONV      ; a conversion and a probe run flat out:
    jae .yield                      ; the machine's whole speed is the point
    mov ax, [x3_total]              ; the card: a tick - or none while it
    sub ax, [x3_cons]               ; holds less than a sleep can drain
    cmp ax, [x3_slp]
    jb .yield
    mov ax, 1
.sleep:
    call OSAPI_TASK_SLEEP
    jmp short .pass
.yield:
    call OSAPI_TASK_YIELD
.pass:
    mov byte [x3_wbusy], 1          ; FIRST - the job is read after it
    mov al, [x3_job]
    cmp al, WJ_LIVE
    je .feed
    cmp al, WJ_FILE
    je .feed
    cmp al, WJ_CONV
    je .conv
    cmp al, WJ_CAL
    jne .done
    call x3l_wprobe
    jmp short .done
.conv:
    call x3c_wpass                  ; (x3conv.inc)
    jmp short .done
.feed:
    call x3_feed
.done:
    mov byte [x3_wbusy], 0          ; LAST
    jmp short .loop

; x3_lalow - the look-ahead under AP_LA_LOW and the file not all in: a gulp
; asked for now, while there is a gulp's worth of audio still queued - the
; Audio Player's rule (SPEC.md 86.5). Asking only when the parser starves
; leaves the read's own time uncovered. Worker-safe; keeps all but AX
x3_lalow:
    cmp byte [ap_src_done], 0
    jne .out
    test byte [x3_req], X3R_READ
    jnz .out
    call ap_la_avail
    cmp ax, AP_LA_LOW
    jae .out
    mov al, X3R_READ
    jmp x3_kick
.out:
    ret

; x3_kick - a request bit (AL) set and the UI task woken. Worker-safe
x3_kick:
    or [x3_req], al
    push bx
    mov bx, [x3_win]
    call OSAPI_WM_WAKE
    pop bx
    ret

; -----------------------------------------------------------------------------
; x3_feed - THE FEED PASS (worker, lock-free; SPEC.md 86.5's shape). The card's
; position read (verb 3: the clock's source), the stream's end found, and up to
; X3_MAXFEED halves made and published while the ring has room
; -----------------------------------------------------------------------------
x3_feed:
    cmp byte [x3_sopen], 0
    je .out
    cmp byte [x3_sended], 0
    jne .out
    mov al, 3
    mov ah, [x3_hand]
    call OSAPI_SND_STREAM           ; AX = the state, DX = consumed
    mov cx, dx
    sub cx, [x3_cons]
    mov [x3_cons], dx
    add [x3_played], cx
    adc word [x3_played+2], 0
    cmp ax, SND_ST_ENDED
    je .dead
    cmp ax, SND_ST_STALE
    je .dead
    cmp ax, SND_ST_UNDER            ; the card ran dry: counted once a time
    jne .nund
    cmp byte [x3_lasthalf], 0       ; (after the last half it is the end of
    jne .feed                       ; the stream, not an underrun)
    cmp byte [x3_drvund], 0
    jne .feed
    mov byte [x3_drvund], 1
    X3LOG 'U', [x3_played+2]
    call x3_under
    jmp short .feed
.nund:
    mov byte [x3_drvund], 0
.feed:
    cmp byte [x3_lasthalf], 0
    je .fill
    mov ax, [x3_total]              ; the last half is in: when it is played,
    cmp ax, [x3_cons]               ; the stream is over
    jne .out
.dead:
    mov byte [x3_sended], 1
    mov al, X3R_END
    jmp x3_kick
.fill:
    cmp byte [x3_rampout], 0        ; A STEP'S RAMP OUT (x3_stepbegin): what is
    je .nramp                       ; staged and a ramp to silence, published
    mov ax, [x3_total]              ; as the LAST half - and the bytes of real
    sub ax, [x3_cons]               ; audio it ends on banked for the position.
    mov bx, [x3_rsz]                ; (A ring with no room for a half: the
    sub bx, X3_HALF                 ; next pass)
    cmp ax, bx
    jae .out
    mov ax, [x3_staged]
    sub ax, [x3_cons]               ; audio it ends on banked for the position
    xor dx, dx
    add ax, [x3_played]
    adc dx, [x3_played+2]
    mov [x3_stepby], ax
    mov [x3_stepby+2], dx
    call x3_ramp
    add word [x3_total], X3_HALF
    mov byte [x3_lasthalf], 1
    mov byte [x3_rampst], 1
    call x3_pub
    mov byte [x3_job], WJ_NONE      ; nothing more of this rung...
    mov byte [x3_rampout], 0        ; ...and the step goes on (LAST)
    ret
.nramp:
    call x3_lalow                   ; the look-ahead asked for BEFORE it runs
    mov byte [x3_halves], 0         ; dry (SPEC.md 86.5's low-water mark)
.more:
    mov ax, [x3_total]              ; comfortably full: nothing this pass
    sub ax, [x3_cons]
    mov bx, [x3_rsz]
    sub bx, X3_HALF
    cmp ax, bx
    jae .out
    cmp byte [x3_job], WJ_NONE      ; parked meanwhile
    je .out
    call x3_feedhalf                ; CF = 1 nothing published
    jc .out
    call x3_pub
    cmp byte [x3_lasthalf], 0
    jne .out
    inc byte [x3_halves]
    cmp byte [x3_halves], X3_MAXFEED
    jb .more
.out:
    ret

; x3_pub - [x3_total] to the card: an external ring's control word (what the
; card reads) and verb 1 (which alone resumes an underrun). The worker's
x3_pub:
    mov cx, [x3_total]
    cmp byte [x3_ext], 0
    je .v1
    push es
    mov es, [x3_xseg]
    mov [es:X3_XRL+SND_EXT_TOTAL], cx
    pop es
.v1:
    mov al, 1                       ; verb 1: the new total
    mov ah, [x3_hand]
    call OSAPI_SND_STREAM           ; (a far slot: never a tail jump)
    ret

; -----------------------------------------------------------------------------
; x3_feedhalf - make and publish ONE half: what is staged past the total is
; brought up to a whole half from the producer (decoded granules, or a
; finished file's bytes), and the total moves by X3_HALF. The worker's, and
; the UI task's for the pre-roll ([x3_fill] = 1, which never ramps).
; out: CF = 0 a half more in [x3_total]; CF = 1 none (the producer is dry -
;      or done, when what was staged was padded out and published as the last)
; -----------------------------------------------------------------------------
x3_feedhalf:
.top:
    mov ax, [x3_staged]
    sub ax, [x3_total]
    cmp ax, X3_HALF
    jae .publish
    cmp byte [x3_sjob], WJ_FILE
    je .file
    ; --- a granule, if the ring has room for it ahead of what the card holds
    mov ax, [x3_staged]
    sub ax, [x3_cons]
    add ax, X3_PCMSZ
    cmp ax, [x3_rsz]
    ja .none
    call pit_now                    ; (the slow window's clock)
    mov [x3_wt0], ax
    mov [x3_wt0+2], dx
    call x3_gran1                   ; CF = 0: CX bytes of PCM
    jc .dry
    cmp byte [x3_rampin], 0         ; a step's new rung: its first frames
    je .st                          ; rise from silence
    call x3_rampup
.st:
    call x3_stage                   ; ...into the grant at [x3_staged]
    call x3_window                  ; is the machine keeping up?
    jmp short .top
.file:
    call x3_filebytes               ; CF = 0: CX bytes staged from the ring
    jnc .top
.dry:                               ; AL = 1 starved, 2 the end, 3 refused
    cmp al, 2
    je .last
    cmp al, 3
    je .none
    cmp byte [x3_fill], 0
    jne .none                       ; (the pre-roll waits for nothing)
    mov ax, [x3_total]              ; STARVED: if the card is about to run dry,
    sub ax, [x3_cons]               ; a ramp to silence and a half of it - an
    cmp ax, X3_HALF                 ; underrun, counted, with no DC step
    jae .none
    call x3_under
    call x3_ramp
    jmp short .publish
.last:
    call x3_ramp                    ; the end: padded with silence, the last
    mov byte [x3_lasthalf], 1
.publish:
    add word [x3_total], X3_HALF
    clc
    ret
.none:
    stc
    ret

; -----------------------------------------------------------------------------
; x3_window - THE SLOW WINDOW (SPEC.md 121.5): each live granule's wall time
; (the front end, GRANULE and the feed's copy, from [x3_wt0]) summed over
; X3_WINGR granules; a window under X3_SLOW = 1.10 audio seconds a wall
; second counts, two in a row ask for a step DOWN (and the count starts
; again), a window at or over it clears the count - the hysteresis. Never a
; step UP: nothing here promotes mid-track. The disk's time is not in it: a
; read stalls the worker while the ring plays on, and a disk that cannot keep
; up shows as an underrun, which steps down by itself. The worker's (not the
; pre-roll's); keeps all but AX
; -----------------------------------------------------------------------------
x3_window:
    cmp byte [x3_fill], 0
    jne .out
    cmp byte [x3_sjob], WJ_LIVE
    jne .out
    push bx
    push cx
    push dx
    mov bx, x3_wt0
    call x3_pitd
    add [x3_wacc], ax
    adc [x3_wacc+2], dx
    inc word [x3_wg]
    cmp word [x3_wg], X3_WINGR
    jb .pop
    mov cx, [x3_wg]                 ; THE WINDOW'S SPEED
    call x3_audpit                  ; DX:AX = its audio, in pit counts
    mov bx, [x3_wacc]
    mov cx, [x3_wacc+2]
    call x3_ratio                   ; AX = x 100
    mov [x3_st+X3S_SPEED], ax
    X3LOG 'W', ax
    xor bx, bx
    mov [x3_wg], bx
    mov [x3_wacc], bx
    mov [x3_wacc+2], bx
    cmp ax, X3_SLOW
    jae .fine
    inc byte [x3_wslow]
    cmp byte [x3_wslow], 2
    jb .pop
    mov byte [x3_wslow], 0          ; two slow windows: a step down
    mov al, X3R_STEP
    call x3_kick
    jmp short .pop
.fine:
    mov byte [x3_wslow], 0
.pop:
    pop dx
    pop cx
    pop bx
.out:
    ret

; x3_audpit - CX granules of 576 source samples at [x3_srate] -> DX:AX =
; their duration in pit counts. keeps the rest
x3_audpit:
    push bx
    push cx
    mov ax, 576
    mul cx
    mov cx, ax
    mov ax, 0x34DE                  ; x 1,193,182 / rate
    mov dx, 0x0012
    mov bx, [x3_srate]
    call x3_mdiv
    pop cx
    pop bx
    ret

; x3_under - an underrun, counted (SPEC.md 121.6), and on a live stream the
; ladder told: one underrun is a step DOWN (121.5). On a finished file it is
; the disk or the device, counted and shown, and never a reason to decode
; with more loss. Worker-safe
x3_under:
    inc word [x3_st+X3S_UNDER]
    cmp byte [x3_sjob], WJ_LIVE
    jne .out
    cmp byte [x3_fill], 0
    jne .out
    mov al, X3R_STEP
    call x3_kick
.out:
    ret

; -----------------------------------------------------------------------------
; x3_ramp - [x3_staged] brought up to [x3_total] + X3_HALF with SILENCE: the
; first X3_RAMP frames a straight line from the last sample made to the
; silence level, so an underrun or the end leaves no DC step
; -----------------------------------------------------------------------------
x3_ramp:
    push ax
    push bx
    push cx
    push dx
    push di
    mov cx, [x3_total]
    add cx, X3_HALF
    sub cx, [x3_staged]             ; CX = bytes to the half's end
    jcxz .out
    cmp cx, X3_PCMSZ
    jbe .n
    mov cx, X3_PCMSZ                ; (a piece at a time, through the PCM)
.n:
    push cx
    push es
    mov es, [ap_la_seg]
    mov di, X3_PCMOFF
    call x3_rampfill                ; CX bytes of ramp-then-silence at ES:DI
    pop es
    pop cx
    call x3_stage
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    jmp short x3_ramp
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; x3_rampfill - CX bytes at ES:DI of the ramp from [x3_last] (the last
; frame made, as samples) to silence, X3_RAMP frames, then silence; the ramp's
; step carried in [x3_rampn] so a ramp spanning two pieces stays one line
x3_rampfill:
    push ax
    push bx
    push cx
    push si
    cmp byte [x3_obits], 16
    je .w16
.b8:
    or cx, cx
    jnz .b8go
    jmp .out
.b8go:
    mov al, 0x80
    cmp byte [x3_rampn], X3_RAMP
    jae .s8
    mov al, [x3_last]               ; s = last + (80h - last) x n / RAMP
    xor ah, ah
    mov bx, 0x80
    sub bx, ax                      ; BX = the distance, signed
    mov ax, bx
    mov bl, [x3_rampn]
    xor bh, bh
    imul bx
    mov bx, X3_RAMP
    idiv bx
    add al, [x3_last]
.s8:
    mov [es:di], al
    inc di
    dec cx
    cmp byte [x3_och], 2
    jne .n8
    jcxz .out
    mov [es:di], al
    inc di
    dec cx
.n8:
    inc byte [x3_rampn]
    jmp short .b8
.w16:
    cmp cx, 2
    jb .out
    xor ax, ax
    cmp byte [x3_rampn], X3_RAMP
    jae .s16
    mov ax, [x3_last]               ; s = last x (RAMP - n) / RAMP
    mov bl, X3_RAMP
    sub bl, [x3_rampn]
    xor bh, bh
    imul bx
    mov bx, X3_RAMP
    idiv bx
.s16:
    mov [es:di], ax
    add di, 2
    sub cx, 2
    cmp byte [x3_och], 2
    jne .n16
    cmp cx, 2
    jb .out
    mov [es:di], ax
    add di, 2
    sub cx, 2
.n16:
    inc byte [x3_rampn]
    jmp short .w16
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; x3_rampup - CX bytes of fresh PCM (X3_PCMOFF): while [x3_rampin] frames
; are owed, each frame scaled by (X3_RAMP - owed) / X3_RAMP - a step's new
; rung starts from silence as the old one ended on it. keeps all
x3_rampup:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov es, [ap_la_seg]
    mov si, X3_PCMOFF
.f:
    cmp byte [x3_rampin], 0
    je .out
    jcxz .out
    mov bl, X3_RAMP
    sub bl, [x3_rampin]             ; BL = k, 0 .. X3_RAMP - 1
    mov bh, [x3_och]                ; BH = the frame's samples
.s:
    cmp byte [x3_obits], 16
    je .w
    mov al, [es:si]                 ; 8-bit: (s - 80h) k / 32 + 80h
    sub al, 0x80
    imul bl
    sar ax, 1
    sar ax, 1
    sar ax, 1
    sar ax, 1
    sar ax, 1
    add al, 0x80
    mov [es:si], al
    inc si
    dec cx
    jmp short .n
.w:
    cmp cx, 2
    jb .out
    mov ax, [es:si]                 ; 16-bit: s k / 32 - the low word of
    push bx                         ; DX:AX >> 5, which is (AX >> 5) with
    push cx                         ; DX's low bits above it (|s k| < 2^20)
    xor bh, bh
    imul bx
    mov cl, 5
    shr ax, cl
    mov cl, 11
    shl dx, cl
    or ax, dx
    pop cx
    pop bx
    mov [es:si], ax
    add si, 2
    sub cx, 2
.n:
    dec bh
    jz .fr
    jcxz .out
    jmp short .s
.fr:
    dec byte [x3_rampin]
    jmp short .f
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; x3_stage - CX bytes of the PCM (X3_PCMOFF in the ring's claim) into the grant at [x3_staged], split at the
; ring's end (verb 6, any task). [x3_staged] moves by CX. keeps all
; -----------------------------------------------------------------------------
x3_stage:
    push ax
    push bx
    push cx
    push si
    push di
    mov si, X3_PCMOFF
    mov bx, cx                      ; BX = what is left
.piece:
    or bx, bx
    jz .out
    mov di, [x3_staged]
    and di, [x3_rmask]
    mov cx, [x3_rsz]
    sub cx, di                      ; the run to the ring's end
    cmp cx, bx
    jbe .c
    mov cx, bx
.c:
    add di, [x3_grnt]
    push cx
    cmp byte [x3_ext], 0
    jne .x
    mov al, 6                       ; verb 6: the ring claim's SI -> the grant
    push ds
    mov ds, [ap_la_seg]
    call OSAPI_SND_STREAM
    pop ds
    jmp short .y
.x:
    push si                         ; the external ring: written directly
    push es
    push ds
    mov es, [x3_xseg]
    mov ds, [ap_la_seg]
    cld
    rep movsb
    pop ds
    pop es
    pop si
.y:
    pop cx
    add si, cx
    add [x3_staged], cx
    sub bx, cx
    jmp short .piece
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; x3_gran1 - ONE GRANULE, decoded into the PCM area in the output format (worker,
; and the UI task for a pre-roll or a seek's discard): a frame staged from the
; ring when the last one's granules are spent, the backend's GRANULE, then the
; pre-roll's discard, the gapless trim and the tail limit.
; out: CF = 0, CX = output bytes at X3_PCMOFF (0: all of it discarded - call
;      again); CF = 1 AL = 1 starved (a gulp is asked for), 2 the stream's
;      end, 3 the decoder refused ([x3_wfail])
; -----------------------------------------------------------------------------
x3_gran1:
    push bx
    push dx
    push si
    push di
.again:
    mov al, [x3_grn]                ; granules left of the staged frame
    cmp al, [x3_grk]
    ja .have
    mov si, x3_ring
    call x3p_next                   ; CF = 0: AL = its granules
    jnc .staged
    cmp ax, X3P_END
    je .end
    cmp byte [ap_src_done], 0       ; MORE: the ring wants a gulp
    jne .end
    test byte [x3_req], X3R_READ
    jnz .starve
    mov al, X3R_READ
    call x3_kick
.starve:
    mov al, 1
    jmp .no
.staged:
    mov [x3_grn], al
    mov byte [x3_grk], 0
    inc word [x3_frames]
    jnz .have
    inc word [x3_frames+2]
.have:
    mov al, [x3_grk]
    inc byte [x3_grk]
    call x3p_gran                   ; DX:AX = the record
    mov es, dx
    mov si, ax
    mov dx, [ap_la_seg]             ; the PCM: past the ring, in its claim
    mov di, X3_PCMOFF
    push ds
    mov ds, [x3_bseg]               ; DS = the backend's (x3gran.inc 2)
    call far [cs:x3_bgran]          ; GRANULE: CX = frames, AX = clips
    pop ds
    push ds
    pop es
    jc .refused
    add [x3_clips], ax
    inc word [x3_grans]
    call x3_trim                    ; CX frames -> CX bytes kept
    jcxz .again
    cmp byte [x3_job], WJ_CONV      ; the meters (converting they rest: 3% of
    je .np                          ; an XT's conversion, FIELD.md)
    call x3_peak
.np:
    clc
    jmp short .out
.refused:
    mov [x3_wfail], ax
    mov al, X3R_FAIL
    call x3_kick
    mov al, 3
    jmp short .no
.end:
    mov al, 2
.no:
    stc
.out:
    pop di
    pop si
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; x3_trim - CX frames of PCM, just made: what of them is kept. [x3_disc]
; frames are dropped from the front (a seek's pre-roll, the gapless head trim
; - x3sim's trim rule, SPEC.md 121.7), and [x3_limit] caps what is kept (the
; tail trim: the stream's exact output less its padding; FFFFFFFFh = none).
; out: CX = the BYTES kept, moved to the PCM's front. keeps the rest
; -----------------------------------------------------------------------------
x3_trim:
    push ax
    push bx
    push dx
    push si
    push di
    xor bx, bx                      ; BX = frames dropped from the front
    mov ax, [x3_disc]
    or ax, [x3_disc+2]
    jz .limit
    cmp word [x3_disc+2], 0
    jne .all
    cmp [x3_disc], cx
    jb .part
.all:
    sub [x3_disc], cx               ; every one of them
    sbb word [x3_disc+2], 0
    xor cx, cx
    jmp short .bytes
.part:
    mov bx, [x3_disc]
    mov word [x3_disc], 0
    sub cx, bx
.limit:
    cmp word [x3_limit+2], 0xFFFF
    je .move                        ; (no limit)
    cmp word [x3_limit+2], 0
    jne .lsub
    cmp [x3_limit], cx
    jae .lsub
    mov cx, [x3_limit]
.lsub:
    sub [x3_limit], cx
    sbb word [x3_limit+2], 0
.move:
    or bx, bx
    jz .bytes
    call x3_fbytes                  ; the kept frames to the front
    mov si, ax
    mul bx
    xchg ax, si                     ; SI = bytes dropped, AX = a frame's
    mul cx
    push cx
    mov cx, ax
    add si, X3_PCMOFF
    mov di, X3_PCMOFF
    push es
    push ds
    mov ax, [ap_la_seg]
    mov es, ax
    mov ds, ax
    cld
    rep movsb
    pop ds
    pop es
    pop cx
.bytes:
    call x3_fbytes
    mul cx
    mov cx, ax
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; x3_peak - CX bytes of the PCM: the peak of each channel, folded into
; [x3_pkl] / [x3_pkr] (0..255) for the face's needles; the last frame banked
; for an underrun's ramp. keeps all
; -----------------------------------------------------------------------------
x3_peak:
    push ax
    push bx
    push cx
    push dx
    push si
    or cx, cx
    jnz .go
    jmp .out
.go:
    push es
    mov es, [ap_la_seg]
    mov si, X3_PCMOFF
    xor bx, bx                      ; BL = left, BH = right, in 0..127
    cmp byte [x3_obits], 16
    je .w
.b:
    es lodsb
    sub al, 0x80
    jns .bp
    neg al
    jns .bp
    mov al, 127
.bp:
    cmp al, bl
    jbe .bn
    mov bl, al
.bn:
    dec cx
    cmp byte [x3_och], 2
    jne .bm
    jcxz .done
    es lodsb
    sub al, 0x80
    jns .bq
    neg al
    jns .bq
    mov al, 127
.bq:
    cmp al, bh
    jbe .br
    mov bh, al
.br:
    dec cx
.bm:
    jcxz .done
    jmp short .b
.w:
    cmp cx, 2
    jb .done
    es lodsw
    call x3_pk16
    cmp al, bl
    jbe .wn
    mov bl, al
.wn:
    sub cx, 2
    cmp byte [x3_och], 2
    jne .w
    cmp cx, 2
    jb .done
    es lodsw
    call x3_pk16
    cmp al, bh
    jbe .wr
    mov bh, al
.wr:
    sub cx, 2
    jmp short .w
.done:
    shl bl, 1
    shl bh, 1
    cmp byte [x3_och], 2
    je .st
    mov bh, bl
.st:
    cmp bl, [x3_pkl]
    jbe .l
    mov [x3_pkl], bl
.l:
    cmp bh, [x3_pkr]
    jbe .r
    mov [x3_pkr], bh
.r:
    mov ax, [es:si-2]               ; the last frame, for a ramp
    cmp byte [x3_obits], 16
    je .lw
    mov al, [es:si-1]
    mov [x3_last], al
    jmp short .rz
.lw:
    mov [x3_last], ax
.rz:
    mov byte [x3_rampn], 0
    pop es
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; x3_pk16 - AX = a signed sample -> AL = |AX| >> 8 (0..127). keeps the rest
x3_pk16:
    or ax, ax
    jns .p
    neg ax
    jns .p
    mov ax, 0x7FFF
.p:
    mov al, ah
    ret

; -----------------------------------------------------------------------------
; x3_filebytes - a FINISHED WAV's bytes, from the ring into the grant: up to
; what makes the half whole and is queued. Whole frames only; the end of the
; data is the stream's end.
; out: CF = 0, CX = bytes staged; CF = 1 AL = 1 starved, 2 the end
; -----------------------------------------------------------------------------
x3_filebytes:
    push bx
    push dx
    push si
    push di
    mov cx, [x3_total]
    add cx, X3_HALF
    sub cx, [x3_staged]             ; CX = to the half's end
    call ap_la_avail
    cmp ax, cx
    jae .take
    cmp byte [ap_src_done], 0
    jne .tail
    test byte [x3_req], X3R_READ
    jnz .st
    push ax
    mov al, X3R_READ
    call x3_kick
    pop ax
.st:
    cmp ax, [x3_fby]                ; take what whole frames there are
    jb .starve
    mov cx, ax
    jmp short .frames
.tail:
    mov cx, ax
    jcxz .end
.frames:
    push dx
    mov ax, cx
    xor dx, dx
    div word [x3_fby]
    mul word [x3_fby]
    pop dx
    mov cx, ax
    jcxz .starve
.take:
    ; ring -> grant, split at either seam: the ring's (AP_LA_SZ) and the
    ; grant's - verb 6 from DS = the ring's claim
    mov bx, cx                      ; BX = what is left
.piece:
    or bx, bx
    jz .done
    mov si, [ap_la_rd]
    and si, AP_LA_MASK
    mov cx, AP_LA_SZ
    sub cx, si
    cmp cx, bx
    jbe .c1
    mov cx, bx
.c1:
    mov di, [x3_staged]
    and di, [x3_rmask]
    mov ax, [x3_rsz]
    sub ax, di
    cmp ax, cx
    jae .c2
    mov cx, ax
.c2:
    add di, [x3_grnt]
    push ds
    push cx
    cmp byte [x3_ext], 0
    jne .xc
    mov al, 6
    mov ds, [ap_la_seg]             ; verb 6's source is DS's (SPEC.md 34.5)
    call OSAPI_SND_STREAM
    jmp short .yc
.xc:
    push si
    push es
    mov es, [x3_xseg]               ; the external ring: written directly
    mov ds, [ap_la_seg]
    cld
    rep movsb
    pop es
    pop si
.yc:
    pop cx
    pop ds
    add [ap_la_rd], cx
    add [x3_staged], cx
    sub bx, cx
    jmp short .piece
.done:
    call x3_filepeak
    clc
    jmp short .out
.end:
    mov al, 2
    stc
    jmp short .out
.starve:
    mov al, 1
    stc
.out:
    pop di
    pop si
    pop dx
    pop bx
    ret

; x3_filepeak - a finished file's meters: a frame's samples read back out of
; the ring just behind its read position - one sample a half, which is what
; four needle moves a second can show
x3_filepeak:
    push ax
    push cx
    push si
    push es
    mov es, [ap_la_seg]
    mov si, [ap_la_rd]
    sub si, 4
    and si, AP_LA_MASK
    mov cx, [es:si]
    pop es
    cmp byte [x3_obits], 16
    je .w
    mov al, cl
    sub al, 0x80
    jns .p
    neg al
.p:
    shl al, 1
    mov [x3_pkl], al
    mov [x3_pkr], al
    jmp short .out
.w:
    mov ax, cx
    call x3_pk16
    shl al, 1
    mov [x3_pkl], al
    mov [x3_pkr], al
.out:
    pop si
    pop cx
    pop ax
    ret

; =============================================================================
; THE UI TASK'S SIDE OF A PLAY: the clock, the meters, the gauge, the end
; =============================================================================

; x3_pos - DX:AX = the output sample the card has PLAYED: the open's base
; plus the bytes consumed, in frames
x3_pos:
    mov ax, [x3_played]
    mov dx, [x3_played+2]
; x3_by2pos - DX:AX = bytes of the stream since its open -> DX:AX = the
; output sample they reach
x3_by2pos:
    push bx
    push cx
    mov cx, [x3_fby]
    cmp cx, 1
    je .one
    shr dx, 1
    rcr ax, 1
    cmp cx, 2
    je .one
    shr dx, 1
    rcr ax, 1
.one:
    add ax, [x3_pbase]
    adc dx, [x3_pbase+2]
    pop cx
    pop bx
    ret

; x3_clock - x3_st's elapsed seconds from the samples played
x3_clock:
    call x3_pos
    mov cx, [x3_orate]
    or cx, cx
    jz .out
    cmp dx, cx
    jae .out
    div cx
    mov [x3_st+X3S_ELAP], ax
.out:
    ret

; x3_samp2ms - DX:AX output samples -> DX:AX milliseconds (overflow-safe:
; whole seconds and the remainder apart)
x3_samp2ms:
    push bx
    push cx
    mov cx, [x3_orate]
    or cx, cx
    jz .z
    cmp dx, cx
    jae .z
    div cx                          ; AX = seconds, DX = the remainder
    push dx
    mov bx, 1000
    mul bx                          ; DX:AX = seconds x 1000
    pop bx
    push dx
    push ax
    mov ax, bx                      ; + remainder x 1000 / rate
    mov dx, 1000
    mul dx
    div cx
    pop bx
    pop dx
    add ax, bx
    adc dx, 0
    jmp short .out
.z:
    xor ax, ax
    xor dx, dx
.out:
    pop cx
    pop bx
    ret

; x3_gauge - the BUFFER gauge: the card's ring fill, 0..255
x3_gauge:
    mov ax, [x3_total]
    sub ax, [x3_cons]
    mov cx, [x3_rsz]
    or cx, cx
    jz .z
    cmp ax, cx
    jb .d
    mov ax, cx
    dec ax
.d:
    mov bl, 0                       ; AX x 256 / ring, as AH
    xor dx, dx
    mov dl, ah                      ; (DX:AX = AX x 256)
    mov ah, al
    xor al, al
    div cx
    cmp ax, 255
    jbe .s
    mov al, 255
.s:
    mov [x3_st+X3S_LEVEL], al
    ret
.z:
    mov byte [x3_st+X3S_LEVEL], 0
    ret

; x3_needles - the worker's peaks since the last pass into x3_st, eased; the
; peaks then let go. Nothing while nothing plays (the needles rest)
x3_needles:
    mov al, [x3_md]
    cmp al, MD_LIVE
    je .go
    cmp al, MD_FILE
    je .go
    cmp al, MD_SPK
    je .go
    xor ax, ax
    mov [x3_st+X3S_METL], al
    mov [x3_st+X3S_METR], al
    ret
.go:
    mov al, [x3_pkl]
    mov ah, [x3_st+X3S_METL]
    shr al, 1
    shr ah, 1
    add al, ah
    mov [x3_st+X3S_METL], al
    mov al, [x3_pkr]
    mov ah, [x3_st+X3S_METR]
    shr al, 1
    shr ah, 1
    add al, ah
    mov [x3_st+X3S_METR], al
    mov byte [x3_pkl], 0
    mov byte [x3_pkr], 0
    ret

; -----------------------------------------------------------------------------
; x3_ended - X3R_END: a stream played out, or the worker's job ended. The
; next entry plays (its own cache, or the ladder); after the last, Finished
; -----------------------------------------------------------------------------
x3_ended:
    mov al, [x3_md]
    cmp al, MD_CONV
    jne .snd
    jmp x3c_ended                   ; (x3conv.inc) finalise, then play
.snd:
    cmp al, MD_LIVE
    je .next
    cmp al, MD_FILE
    jne .out
.next:
    cmp byte [x3_sended], 0
    je .out
    call x3_snd_close
%ifdef X3REPORT
    call x3r_we                     ; FIELD (x3rep.inc): the report, EVENT E
%endif
    mov byte [x3_md], MD_IDLE
    jmp x3_advance
.out:
    ret

; x3_advance - the track is over: the next entry, from its start - played
; when it can be without a question (a cache, a live rung, an approved batch),
; else READY on it; after the last, Finished
x3_advance:
    mov al, [x3_st+X3S_CUR]
    inc al
    cmp al, [x3_qn]
    jae .done
    mov [x3_qsel], al
    jmp x3_e_play
.done:
    mov ax, [x3_st+X3S_TOTAL]
    cmp ax, 0xFFFF
    je .d
    mov [x3_st+X3S_ELAP], ax
.d:
    mov byte [x3_st+X3S_STATE], X3ST_DONE
    mov byte [x3_c_batch], 0
    ret

; x3_wfailed - X3R_FAIL: the decoder refused a granule mid-stream
x3_wfailed:
    call x3_stopall
    mov word [x3_st+X3S_ERR], x3_s_decode
    mov byte [x3_st+X3S_STATE], X3ST_ERROR
    ret

; =============================================================================
; LIVE (SPEC.md 121.5): a rung's backend opened, the stream from a position
; =============================================================================

; -----------------------------------------------------------------------------
; x3_live_at - DX:AX = an output sample: the stream from there, live on the
; rung chosen ([x3_rung]). The ring re-aimed at the frame X3_PREROLL before
; it, the histories reset, those frames decoded to nothing, the card opened
; -----------------------------------------------------------------------------
x3_live_at:
    call x3_snd_close
    call x3_samp2ms                 ; DX:AX = its millisecond
    call x3_resync                  ; the ring, the front, the backend there
    jc .err
    mov byte [x3_sjob], WJ_LIVE
    call x3_snd_open
    jc .err
    mov byte [x3_md], MD_LIVE
    mov byte [x3_st+X3S_STATE], X3ST_LIVE
    call x3_flagsvol
.err:
    ret

; -----------------------------------------------------------------------------
; x3_resync - DX:AX = a millisecond: the front end and the backend made ready
; to decode from there (SPEC.md 121.9). The frame index x3p_seekpos finds is
; the TARGET; the ring is aimed X3_PREROLL frames before it (none at the
; stream's start, whose reservoir is honestly empty), the reservoir reset,
; the backend RESET, and [x3_disc] set to the pre-roll's output frames, which
; x3_trim drops. The base sample is the target frame's. On the UI task.
; out: CF = 1 nothing to decode from (x3_st says why)
; -----------------------------------------------------------------------------
x3_resync:
    push bx
    push cx
    mov bx, ax
    or bx, dx
    jz .start
    mov di, x3_info
    call x3p_seekpos                ; DX:AX = the offset, CX = the frame
    mov [x3_tframe], cx
    push ax
    push dx
    mov bx, cx                      ; back X3_PREROLL frames, at most to 0
    sub bx, X3_PREROLL
    jae .back
    xor bx, bx
.back:
    sub cx, bx                      ; CX = the frames to discard
    pop dx
    pop ax
    or cx, cx
    jz .aim                         ; (already at the first)
    or bx, bx                       ; a pre-roll from the very first frame: the
    jz .from0                       ; audio's own start, never an estimate (and
                                    ; never a Xing frame a TOC points at)
    push cx
    mov cx, bx
    call x3_fr2ms                   ; DX:AX = the pre-roll's frame's ms
    mov di, x3_info
    call x3p_seekpos                ; ...its offset
    pop cx
    jmp short .aim
.from0:
    mov ax, [x3_info+X3I_AUDIO]
    mov dx, [x3_info+X3I_AUDIO+2]
    jmp short .aim
.start:
    mov word [x3_tframe], 0
    xor cx, cx
    mov ax, [x3_info+X3I_AUDIO]
    mov dx, [x3_info+X3I_AUDIO+2]
.aim:
    push cx
    mov [x3_rsoff], ax              ; (where the pre-roll's scan begins: what
    mov [x3_rsoff+2], dx            ; tests/xtmp3live.py's seek decodes from)
    call x3_la_mp3                  ; the producer at DX:AX
    mov ax, [x3_lasz]               ; half the ring before the front scans
    shr ax, 1
    call x3_prime
    mov si, x3_ring
    cmp word [x3_tframe], 0
    jne .reset
    mov di, x3_info
    call x3p_start                  ; the stream's very start: a fresh start
    jmp short .be
.reset:
    call x3p_reset                  ; the reservoir emptied, a scan from here
.be:
    mov bl, X3V_RESET
    call x3_bvcall
    pop cx                          ; CX = frames to discard
    mov byte [x3_grn], 0
    mov byte [x3_grk], 0
    mov ax, cx                      ; [x3_disc] = CX x the frame's output
    call x3_frout                   ; frames
    mov [x3_disc], ax
    mov [x3_disc+2], dx
    mov ax, [x3_tframe]             ; the base: the target frame's first
    call x3_frout                   ; output sample
    mov [x3_pbase], ax
    mov [x3_pbase+2], dx
    call x3_limitset                ; the tail trim's limit
    clc
    pop cx
    pop bx
    ret

; x3_fr2ms - CX = a frame index -> DX:AX = its millisecond (frames x samples a
; frame x 1000 / rate, in two pieces so nothing overflows)
x3_fr2ms:
    push bx
    push cx
    call x3_spf                     ; AX = source samples a frame
    mul cx                          ; DX:AX = samples
    mov bx, [x3_srate]
    div bx                          ; AX = seconds, DX = the rest
    push dx
    mov cx, 1000
    mul cx
    pop cx
    push dx
    push ax
    mov ax, cx
    mov cx, 1000
    mul cx
    div bx
    pop bx
    pop dx
    add ax, bx
    adc dx, 0
    pop cx
    pop bx
    ret

; x3_spf - AX = source samples a frame: 1,152 on MPEG-1, 576 else. keeps all
x3_spf:
    mov ax, 1152
    cmp byte [x3_info+X3I_VER], 1
    je .o
    mov ax, 576
.o:
    ret

; x3_frout - AX = frames -> DX:AX = OUTPUT frames: x samples a frame /
; the divisor. keeps the rest
x3_frout:
    push cx
    mov cx, ax
    call x3_spf
    mul cx
    mov cl, [x3_odiv]
    cmp cl, 2
    jb .o
    shr dx, 1
    rcr ax, 1
    cmp cl, 4
    jb .o
    shr dx, 1
    rcr ax, 1
.o:
    pop cx
    ret

; x3_limitset - [x3_limit] = output frames still to keep from the base: the
; exact count less the base and the tail trim (x3sim's: a LAME tag's padding
; less the decoder's 529), when the stream's frames are counted; FFFFFFFFh
; (none) when they are not. And the HEAD trim at the stream's start: the
; decoder's 529 and a LAME tag's delay, dropped as a discard
x3_limitset:
    push ax
    push bx
    push cx
    push dx
    mov word [x3_limit], 0xFFFF
    mov word [x3_limit+2], 0xFFFF
    cmp word [x3_tframe], 0         ; THE HEAD: only from the very start
    jne .tail
    mov ax, X3_DECDELAY
    test byte [x3_info+X3I_FLAGS], X3IF_LAME
    jz .h
    add ax, [x3_info+X3I_DELAY]
.h:
    call x3_bydiv                   ; / the divisor
    add [x3_disc], ax
    adc word [x3_disc+2], 0
.tail:
    test byte [x3_info+X3I_FLAGS], X3IF_EXACT
    jz .out
    mov ax, [x3_info+X3I_FRAMES]    ; frames x spf / div: the stream's output
    mov dx, [x3_info+X3I_FRAMES+2]
    or dx, dx
    jnz .out                        ; (65,536 frames: 25 minutes - no limit)
    call x3_frout
    sub ax, [x3_pbase]
    sbb dx, [x3_pbase+2]
    jb .out
    mov bx, ax
    mov cx, dx
    test byte [x3_info+X3I_FLAGS], X3IF_LAME
    jz .lim
    mov ax, [x3_info+X3I_PAD]       ; padding - 529, at least 0
    sub ax, X3_DECDELAY
    jbe .lim
    call x3_bydiv
    sub bx, ax
    sbb cx, 0
    jb .out
.lim:
    cmp word [x3_tframe], 0         ; from the start, the head comes off too
    jne .set
    mov ax, X3_DECDELAY
    test byte [x3_info+X3I_FLAGS], X3IF_LAME
    jz .h2
    add ax, [x3_info+X3I_DELAY]
.h2:
    call x3_bydiv
    sub bx, ax
    sbb cx, 0
    jb .out
.set:
    mov [x3_limit], bx
    mov [x3_limit+2], cx
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

X3_DECDELAY equ 529                 ; the hybrid filterbank's own delay

; x3_bydiv - AX / [x3_odiv]. keeps the rest
x3_bydiv:
    cmp byte [x3_odiv], 2
    jb .o
    shr ax, 1
    cmp byte [x3_odiv], 4
    jb .o
    shr ax, 1
.o:
    ret

; =============================================================================
; A LIVE STEP DOWN (SPEC.md 121.5): two slow windows or one underrun, the
; worker asks; the ring drains (no new half, the card plays what it has, so
; the position is kept to the sample); then the rung below opens at the
; position the card reached, with X3_PREROLL frames of pre-roll
; =============================================================================
x3_stepbegin:
    cmp byte [x3_md], MD_LIVE
    jne .out
    cmp byte [x3_st+X3S_QWANT], X3Q_AUTO ; an override is the user's: it is shown
    jne .out                        ; as set and is not moved (the underruns
                                    ; are counted)
    call x3l_below                  ; AL = the next live rung down, CF none
    jc .out
    mov [x3_steprung], al
    mov byte [x3_md], MD_STEP
    mov byte [x3_st+X3S_STATE], X3ST_BUFFER
    cmp byte [x3_lasthalf], 0       ; the stream's last half is in already:
    jne .cut                        ; nothing to ramp, it plays out
    mov byte [x3_rampout], 1        ; THE RAMP OUT: the worker's next pass ends
    ret                             ; the stream on a ramp to silence (x3_feed)
.cut:
    mov byte [x3_job], WJ_NONE      ; no new halves: the ring plays out
.out:
    ret

; x3_steptick - MD_STEP: the card played everything it had? the new rung
x3_steptick:
    cmp byte [x3_rampout], 0        ; the worker has not ramped out yet
    je .rd
    cmp byte [x3_sended], 0         ; (unless the stream ended under it)
    je .out
    mov byte [x3_rampout], 0
.rd:
    cmp byte [x3_sopen], 0
    je .go
    mov al, 3
    mov ah, [x3_hand]
    call OSAPI_SND_STREAM           ; DX = consumed
    mov cx, dx
    sub cx, [x3_cons]
    mov [x3_cons], dx
    add [x3_played], cx
    adc word [x3_played+2], 0
    cmp dx, [x3_total]
    jne .out
.go:
    call x3_pos                     ; the sample reached, at the OLD rate -
    cmp byte [x3_rampst], 0         ; the last REAL one where the stream was
    je .p                           ; ramped out (its ramp and silence are not
    mov ax, [x3_stepby]             ; the track's)
    mov dx, [x3_stepby+2]
    call x3_by2pos
.p:
    call x3_samp2ms
    push ax
    push dx
    call x3_snd_close
    mov [x3_startms], ax            ; (the position, for a rung that cannot)
    mov [x3_startms+2], dx
.try:
    mov al, [x3_steprung]
    call x3_rungset                 ; the backend opened at the rung below
    jc .next
    mov ax, [x3_startms]
    mov dx, [x3_startms+2]
    call x3_resync
    jc .next
    mov byte [x3_sjob], WJ_LIVE
    mov byte [x3_rampin], X3_RAMP   ; THE RAMP IN: the new rung's first frames
    call x3_snd_open                ; rise from silence (x3_feedhalf)
    jc .next
    mov byte [x3_md], MD_LIVE
    mov byte [x3_st+X3S_STATE], X3ST_LIVE
    pop dx
    pop ax
.out:
    ret
.next:                              ; THAT RUNG CANNOT START (its memory, the
    call x3_snd_close               ; card): the one below it; and below the
    mov word [x3_st+X3S_ERR], 0     ; last live rung, Decode first from the
    mov al, [x3_steprung]           ; track's start - never an error
    mov [x3_rung], al
    call x3l_below
    jc .df
    mov [x3_steprung], al
    jmp short .try
.df:
    pop dx
    pop ax
    mov byte [x3_md], MD_IDLE
    xor ax, ax
    mov [x3_startms], ax
    mov [x3_startms+2], ax
    mov byte [x3_wantdf], 1
    jmp x3l_start

; =============================================================================
; THE BACKEND: the part fetched, its work segments claimed, OPEN
; =============================================================================

; -----------------------------------------------------------------------------
; x3_rungset - AL = a rung (X3Q_*): its backend made current and OPENed with
; this source and the rung's output (x3_ofmt). Switching backend drops the one
; part before fetching the other (SPEC.md 121.3); its work segments are
; claimed as CAPS asks and freed with it. On the UI task.
; out: CF = 1 refused, x3_st says why
; -----------------------------------------------------------------------------
x3_rungset:
    mov [x3_rung], al
    mov [x3_st+X3S_QUAL], al
    call x3_ofmt
    jc .rate
    cmp byte [x3_sopen], 0          ; THE CARD'S GRANT, given back first: a
    jne .g                          ; probe's is held until the next probe
    call x3_gfree                   ; takes one, and through an XT OPEN that
.g:                                 ; was 16 KB over the ceiling (402 KB on
                                    ; MartyPC); every caller has no stream open
    call x3_la_free                 ; THE CLAIM ORDER (SPEC.md 121.10): the
                                    ; part, its work and OPEN before the ring
                                    ; - every caller re-aims and refills it
    call x3_rungrow                 ; BX -> the rung's row
    mov ax, X3_LABIG                ; ...and the ring the backend's size
    cmp byte [bx+RG_BE], X3K_XT
    jne .lsz
    mov ax, X3_LAXT
.lsz:
    call x3_lasize
    call x3_onew                    ; THIS rung's OPEN record, in x3_onrec
    call x3_reuse                   ; the part already OPEN with exactly it (a
    jnc .ring                       ; probe's, then its job): RESET will do -
                                    ; x3_resync's - and nothing is fetched
    call x3_rungrow
    mov al, [bx+RG_BE]
    call x3_be_get                  ; the part HERE, its vectors banked
    jc .nofit
    call x3_rungrow
    call x3_rungdiv                 ; CL = the divisor
    mov ah, cl
    mov al, [bx+RG_PROF]
    mov cl, [x3_och]
    mov di, x3_caps1
    push ds
    pop es
    mov bl, X3V_CAPS
    call x3_bvcall
    jc .offer
    call x3_wsfree                  ; the last OPEN's work segments...
    call x3_wsclaim                 ; ...and this one's
    jc .mem
    push si                         ; the record into OPEN's (its segments
    push di                         ; are x3_wsclaim's)
    push es
    push ds
    pop es
    mov si, x3_onrec
    mov di, x3_open
    mov cx, X3O_NSEG
    cld
    rep movsb
    pop es
    pop di
    pop si
    call x3_memck                   ; (the peak DURING OPEN: part and work)
    mov di, x3_open
    push ds
    pop es
    mov bl, X3V_OPEN
    call x3_bvcall
    jc .openerr
    mov byte [x3_bopen], 1          ; OPEN, and in effect
    call x3_shrink                  ; the XT part's OPEN half given back
.ring:
    call x3_rungrow
    mov al, [bx+RG_BE]
    mov [x3_bkind], al
    call x3_plan                    ; the ring (and a conversion's stage) for
    jc .plan                        ; the clusters, under the ceiling...
    call x3_la_claim                ; ...and only then claimed
    jc .mem
    mov al, [x3_och]
    mov [x3_st+X3S_CHANS], al
    mov al, [x3_obits]
    mov [x3_st+X3S_BITS], al
    mov ax, [x3_orate]
    mov [x3_st+X3S_RATE], ax
    clc
    ret
.plan:
    mov [x3_st+X3S_ERR], ax         ; (x3_plan's sentence)
    jmp short .fail
.rate:
    mov word [x3_st+X3S_ERR], x3_s_rate
    jmp short .fail
.nofit:                             ; a part this build carries that could
    call x3_rungrow                 ; not be fetched is memory: said with
    mov al, X3_PART_STD             ; the figures
    cmp byte [bx+RG_BE], X3K_XT
    jne .np
    mov al, X3_PART_XT
.np:
    cmp al, 0xFF
    je .nobe
    mov al, [x3_rung]               ; ...but ONLY when the machine is short:
    call x3_fits                    ; a part that would fit and still was not
    jc .mem                         ; had is the disk's (its package's folder
    mov word [x3_st+X3S_ERR], x3_s_nopart ; gone, a bad read), and saying
    jmp short .fail                 ; 'Needs 360 KB, has 571 KB' was a lie
.nobe:
    mov word [x3_st+X3S_ERR], x3_s_nobe
    jmp short .fail
.offer:
    mov word [x3_st+X3S_ERR], x3_s_offer
    jmp short .fail
.mem:
    call x3_needmsg                 ; 'Needs 390 KB, has 300 KB' (the rung's)
    jmp short .fail
.openerr:
    cmp ax, X3E_TABLE
    jne .oe
    mov word [x3_st+X3S_ERR], x3_s_table
    jmp short .fail
.oe:
    mov word [x3_st+X3S_ERR], x3_s_decode
.fail:
    mov byte [x3_st+X3S_STATE], X3ST_ERROR
    stc
    ret

; x3_onew - x3_onrec = rung [x3_rung]'s OPEN record up to its segments:
; profile, divisor, channels, format, the source, the gain. keeps everything
x3_onew:
    push ax
    push bx
    push cx
    call x3_rungrow
    mov al, [bx+RG_PROF]
    mov [x3_onrec+X3O_PROF], al
    mov al, [x3_odiv]
    mov [x3_onrec+X3O_DIV], al
    mov al, [x3_och]
    mov [x3_onrec+X3O_OCH], al
    mov al, X3_FMT_S16
    cmp byte [x3_obits], 16
    je .f
    mov al, X3_FMT_U8
.f:
    mov [x3_onrec+X3O_FMT], al
    mov al, [x3_snch]
    mov [x3_onrec+X3O_SCH], al
    mov al, [x3_info+X3I_VER]
    mov [x3_onrec+X3O_VER], al
    mov al, [x3_info+X3I_SRI]
    mov [x3_onrec+X3O_SRI], al
    mov byte [x3_onrec+X3O_SRI+1], 0
    mov ax, [x3_gain]
    mov [x3_onrec+X3O_GAIN], ax
    pop cx
    pop bx
    pop ax
    ret

; x3_reuse - is the backend this rung wants resident and OPEN with exactly
; x3_onrec (its work still claimed)? CF = 0 yes: the open stands and a RESET
; is all it needs - which is what saves an XT the second fetch of a part a
; probe shrank (24 KB packed off the disk) when the job's rung is the probe's
x3_reuse:
    push ax
    push bx
    push cx
    push si
    push di
    cmp byte [x3_bopen], 0
    je .no
    call x3_rungrow
    mov al, X3_PART_STD
    cmp byte [bx+RG_BE], X3K_XT
    jne .p
    mov al, X3_PART_XT
.p:
    cmp al, [x3_bpart]
    jne .no
    mov si, x3_onrec
    mov di, x3_open
    mov cx, X3O_NSEG
    call x3_memeq
    jne .no
    clc
    jmp short .out
.no:
    stc
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; x3_plan - THE BUFFERS FOR THE CLUSTERS, under the ceiling (SPEC.md 121.10).
; The look-ahead's size and, for a conversion, its stage's, chosen once the
; backend's part, work and OPEN are in, so what is left is MEASURED:
;   - the look-ahead holds two of the SOURCE's clusters ([ap_clu]) at least:
;     a gulp is whole clusters and the parser consumes whole frames, so a ring
;     of one cluster would wait for a gulp it has no room for
;   - a conversion's stage ([x3c_planc] = the DESTINATION's cluster) is two
;     halves of max(XC_STAGEKB, or XC_STAGEXT under XT, the cluster); where
;     that does not fit it is ONE (the worker waits out each write - on an XT
;     the decode is far the slower), and then the look-ahead is halved as far
;     as its two clusters allow
;   - under the XT backend all of it - and the card's grant, which a probe or
;     a live play takes after - inside X3_CEILKB less what is held now.
;     STANDARD has no ceiling (its path is measured on its own)
; out: x3_lasz (x3_lasize), x3_laxkb, x3c_half, x3c_one; CF = 1 nothing
;      fits, AX = the sentence
; -----------------------------------------------------------------------------
x3_plan:
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, x3_s_clus
    mov si, [ap_clu]                ; SI = the least ring: two source clusters,
    cmp si, X3_LABIG / 2            ; 4 KB at the least (a cluster past 16 KB
    ja .no                          ; no ring of 32 KB holds twice: refused)
    shl si, 1
    cmp si, 4096
    jae .m
    mov si, 4096
.m:
    mov dx, [x3_lasz]               ; DX = the ring: the backend's, or the least
    cmp dx, si
    jae .r
    mov dx, si
.r:
    mov word [x3_xtgr], X3_RINGKB * 1024    ; the card's grant: 16 KB first
.again:
    mov di, 0xFFFF                  ; DI = the budget in KB: none on STANDARD
    cmp byte [x3_bkind], X3K_XT
    jne .bud
    call x3_curkb                   ; AX = KB held now
    mov di, X3_CEILKB
    sub di, ax
    jae .card
    xor di, di
.card:
    cmp byte [x3_card], 0           ; the card's grant to come (none while
    je .bud                         ; converting: the stream is closed)
    cmp word [x3c_planc], 0
    jne .bud
    mov ax, [x3_xtgr]
    push cx
    mov cl, 10
    shr ax, cl
    pop cx
    sub di, ax
    jae .bud
    xor di, di
.bud:
    xor cx, cx                      ; CX = the stage's KB (none)
    mov byte [x3c_one], 0
    mov bx, [x3c_planc]
    or bx, bx
    jz .try
    mov ax, XC_STAGEKB * 1024       ; a half: max(8 KB, or 4 under XT, the
    cmp byte [x3_bkind], X3K_XT     ; destination's cluster)
    jne .h
    mov ax, XC_STAGEXT * 1024
.h:
    cmp ax, bx
    jae .h2
    mov ax, bx
.h2:
    mov [x3c_half], ax
    mov cl, 9                       ; both halves' KB: 2H / 1024 = H >> 9
    shr ax, cl
    mov cx, ax
.try:
    mov ax, dx                      ; the ring, the PCM and the stage, in KB
    push cx
    mov cl, 10
    shr ax, cl
    pop cx
    add ax, X3_PCMKB
    add ax, cx
    cmp ax, di
    jbe .fits
    or bx, bx                       ; too much: a double stage goes single...
    jz .less
    cmp byte [x3c_one], 0
    jne .less
    mov byte [x3c_one], 1
    shr cx, 1
    jmp short .try
.less:
    shr dx, 1                       ; ...then the ring halves, while it still
    cmp dx, si                      ; holds two clusters
    jae .try
    cmp word [x3_xtgr], X3_RINGSM * 1024    ; LIVE (or a probe) under XT: the
    jbe .none                       ; card's 16 KB grant with a smaller look-
    cmp byte [x3_bkind], X3K_XT     ; ahead came first - a floppy's gulp is
    jne .none                       ; shorter, and 16 KB is 1.5 s of 11 kHz
    cmp byte [x3_card], 0           ; where 8 is 0.74 (86Box's 386 underran
    je .none                        ; 11 times on 8: FIELD.md); then the
    cmp word [x3c_planc], 0         ; 8 KB grant, the ring from the top
    jne .none
    mov word [x3_xtgr], X3_RINGSM * 1024
    mov dx, [x3_lasz]
    cmp dx, si
    jae .again
    mov dx, si
    jmp .again
.none:
    mov ax, x3_s_ceil
.no:
    stc
    jmp short .out
.fits:
    mov ax, dx
    call x3_lasize
    mov [x3_laxkb], cx
    clc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; x3_rungrow - BX -> rung [x3_rung]'s row. keeps the rest
x3_rungrow:
    push ax
    mov al, [x3_rung]
    xor ah, ah
    shl ax, 1
    shl ax, 1
    mov bx, ax
    add bx, x3_rungs
    pop ax
    ret

; x3_rungdiv - CL = the row BX's divisor: XT Speech's rule (2 on an 8 to 12
; kHz source, 4 on a 16 to 24 kHz one, SPEC.md 121.4) where the row says 0.
; keeps the rest
x3_rungdiv:
    mov cl, [bx+RG_DIV]
    or cl, cl
    jnz .o
    mov cl, 2
    cmp word [x3_srate], 12000
    jbe .o
    mov cl, 4
.o:
    ret

; THE LADDER'S ROWS, by X3Q_* (SPEC.md 121.5): backend, profile, divisor
x3_rungs:
    db X3K_STD, 0, 1, 0             ; AUTO (never a row: a rung is chosen)
    db X3K_STD, 0, 1, 0             ; ORIGINAL
    db X3K_STD, 0, 2, 0             ; HALF
    db X3K_STD, 0, 4, 0             ; QUARTER
    db X3K_XT, 0, 1, 0              ; XT STANDARD
    db X3K_XT, 1, 1, 0              ; XT FAST
    db X3K_XT, 2, 1, 0              ; XT TURBO
    db X3K_XT, 3, 0, 0              ; XT SPEECH (the divisor by the source)

%ifdef X3_PARTS
; x3_fetchhome - op_fetch of part AL, STANDING IN THE FOLDER XTMP3 WAS
; LAUNCHED FROM. op_fetch reads the package's file by NAME, and a name
; resolves where the instance stands (SPEC.md 19.2.1) - which after a track
; opens is the TRACK's folder, so a song on C: with the package on B: looked
; for C:\XTMP3.O88 and was told it was out of memory. apps/weave/wfetch.inc's
; bracket, on x3_homed/x3_homev (x3_e_init's). QM both ways: nothing is
; listed. out: CF = op_fetch's (or 1 when home is gone). clobbers AX
x3_fetchhome:
    push bx
    push cx
    push dx
    push si
    push di
    push ax
    call OSAPI_FILE_HERE            ; DX, BL = where we stand: banked
    mov [x3_fhd], dx
    mov [x3_fhv], bl
    mov byte [x3_fhmv], 0
    cmp dx, [x3_homed]
    jne .go
    cmp bl, [x3_homev]
    je .home
.go:
    mov dx, [x3_homed]
    mov bl, [x3_homev]
    call OSAPI_FILE_GOTO_QM         ; clobbers AX (and CX/SI/DI redirected)
    jc .gone
    mov byte [x3_fhmv], 1
.home:
    pop ax
    push ax
    call op_fetch                   ; CF = 1: a toast has said why
    pushf
    cmp byte [x3_fhmv], 0
    je .stay
    mov dx, [x3_fhd]
    mov bl, [x3_fhv]
    call OSAPI_FILE_GOTO_QM         ; back where the track stands
.stay:
    popf
    jmp short .out
.gone:
    stc
.out:
    pop ax
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
%endif

; -----------------------------------------------------------------------------
; x3_be_get - AL = a backend kind (X3K_*): its part fetched (the other one
; dropped first), its header checked, the far vectors banked
; out: CF = 1 this build or this machine has no such part
; -----------------------------------------------------------------------------
x3_be_get:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov bl, X3_PART_STD
    cmp al, X3K_XT
    jne .p
    mov bl, X3_PART_XT
.p:
    cmp bl, 0xFF
    jne .part
    jmp .no                         ; (a build with no backend part)
.part:
%ifdef X3_PARTS
    cmp byte [x3_bshr], 0           ; shrunk to its resident half: it takes no
    jne .drop                       ; second OPEN (docs/xtmp3/XT.md 6)
    cmp bl, [x3_bpart]
    je .here
.drop:
    call x3_be_drop                 ; ONE backend resident (SPEC.md 121.3)
    mov al, bl
    push bx
    call x3_fetchhome               ; CF = 1: a toast has said why
    pop bx
    jc .no
    mov [x3_bpart], bl
    xor bh, bh                      ; THE FETCH COUNT, a part (x3_nfetch):
    inc byte [x3_nfetch+bx]         ; what proves Original never fetched the
                                    ; XT part, and a probe's RESET no refetch
    call x3_memck                   ; (the fetch's claim)
.here:
    mov al, [x3_bpart]
    call op_seg                     ; AX = its segment
    or ax, ax
    jz .no
%else
    jmp .no
%endif
    mov es, ax
    cmp word [es:X3K_SIG], X3K_SIGVAL
    jne .bad
    cmp byte [es:X3K_ABI], X3G_ABI
    jne .bad
    cmp byte [es:X3K_NV], X3V_COUNT
    jb .bad
    mov [x3_bseg], ax
    mov al, [x3_bpart]              ; the CODE selector (op_cseg): the data
    call op_cseg                    ; segment's own word in real mode, its
    mov [x3_bvec+2], ax             ; alias on kern_mega
    mov [x3_bgran+2], ax
    mov ax, [es:X3K_VEC + 2 * X3V_GRANULE]
    mov [x3_bgran], ax
%ifdef X3STUB
    call x3_stubpoke                ; the test hook's figures, into the part
%endif
    clc
    jmp short .out
.bad:
    call x3_be_drop
.no:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; x3_be_drop - the backend part and its work segments given back (a finished
; file plays with no decoder resident, SPEC.md 121.7). keeps everything
x3_be_drop:
    push ax
    call x3_wsfree
%ifdef X3_PARTS
    mov al, [x3_bpart]
    cmp al, 0xFF
    je .out
    call op_drop                    ; (a part not here: a no-op)
    mov byte [x3_bpart], 0xFF
    mov byte [x3_bshr], 0
%endif
.out:
    mov byte [x3_bkind], 0xFF       ; no backend: a finished file's ring is
    pop ax                          ; its own size again (x3_grant)
    ret

; -----------------------------------------------------------------------------
; x3_bvcall - a backend verb, BL = X3V_*: DS = the part's own for the call
; (x3gran.inc section 2), every other register the verb's. The worker never
; calls this - GRANULE goes through [x3_bgran] - so its scratch vector is the
; UI task's alone
; -----------------------------------------------------------------------------
x3_bvcall:
    push bx
    push ds
    xor bh, bh
    shl bx, 1
    mov ds, [cs:x3_bseg]
    mov bx, [bx + X3K_VEC]
    mov [cs:x3_bvec], bx
    call far [cs:x3_bvec]
    pop ds
    pop bx
    ret

; x3_wsclaim - CAPS' work segments, each its own claim, into the open record
x3_wsclaim:
    push ax
    push cx
    push dx
    push si
    mov cl, [x3_caps1+X3C_NSEG]
    xor ch, ch
    mov [x3_open+X3O_NSEG], cl
    mov si, 0
.c:
    jcxz .ok
    mov ax, [x3_caps1+X3C_PARA+si]  ; paragraphs -> KB, rounded up
    add ax, 63
    push cx
    mov cl, 6
    shr ax, cl
    pop cx
    call OSAPI_MEM_CLAIM
    jc .no
    mov [x3_open+X3O_SEG+si], dx
    mov [x3_ws+si], dx
    inc byte [x3_nws]
    add si, 2
    dec cx
    jmp short .c
.ok:
    clc
    jmp short .out
.no:
    call x3_wsfree
    stc
.out:
    pop si
    pop dx
    pop cx
    pop ax
    ret

; x3_wsfree - every work segment freed. keeps everything
x3_wsfree:
    push cx
    push dx
    push si
    mov cl, [x3_nws]
    xor ch, ch
    xor si, si
.f:
    jcxz .out
    mov dx, [x3_ws+si]
    call OSAPI_MEM_FREE
    add si, 2
    dec cx
    jmp short .f
.out:
    mov byte [x3_nws], 0
    mov byte [x3_bopen], 0          ; (an OPEN without its work is not one)
    pop si
    pop dx
    pop cx
    ret

; x3_shrink - after a successful OPEN of the XT part, its claim cut to the
; resident half CAPS names (X3C_PARTKB): OPEN's body, the expansion and
; XTGEN.BIN are behind it and nothing reaches them again until the part is
; fetched whole (docs/xtmp3/XT.md section 6; x3_be_get re-fetches). A shrink
; always succeeds in place. A calibration probe shrinks too: its part, work
; and ring held at once unshrunk are 404 KB, over 121.10's 400,000 bytes, so
; the ladder's next OPEN of the same part fetches it again - a disk read of
; its packed 24 KB on an XT, the price of staying inside the ceiling.
; keeps everything
x3_shrink:
%ifdef X3_PARTS
    push ax
    push bx
    push dx
    call x3_rungrow
    cmp byte [bx+RG_BE], X3K_XT
    jne .out
    mov ax, [x3_caps1+X3C_PARTKB]
    or ax, ax
    jz .out
    push ax
    mov al, [x3_bpart]
    call op_seg                     ; a fetched part sits at its claim's base
    mov dx, ax
    pop ax
    or dx, dx
    jz .out
    call OSAPI_MEM_REGROW
    jc .out
    mov byte [x3_bshr], 1
.out:
    pop dx
    pop bx
    pop ax
%endif
    ret

; x3_memck - X3S_PEAK, MEASURED (SPEC.md 121.10): the region and the eager
; parts (the package's, before the zero was taken) plus how far the heap's
; free total has fallen since launch - every claim this instance has made,
; SOUND.DRV's per stream on its behalf with them - kept at its highest. Run
; after each claim. keeps everything
x3_memck:
    push ax
    call x3_curkb
    cmp ax, [x3_st+X3S_PEAK]
    jbe .o
    mov [x3_st+X3S_PEAK], ax
.o:
    pop ax
    ret

; x3_curkb - AX = the working set NOW, in KB: the region, the eager carve and
; the heap's fall since launch (x3_memck's sample). keeps the rest
x3_curkb:
    push bx
    call OSAPI_MEM_AVAIL            ; BX = total free KB
    mov ax, [x3_free0]
    sub ax, bx
    jnc .p
    xor ax, ax                      ; (something else gave memory back)
.p:
    add ax, X3_REGIONKB
%ifdef X3_PARTS
    add ax, [op_kb]                 ; the eager carve: FRONT and ART
%endif
    pop bx
    ret

; =============================================================================
; CALIBRATION AND THE LADDER (SPEC.md 121.5)
;
; Auto takes the first rung that calibrates real time HERE, with THIS device:
; Original, Half, Quarter (STANDARD at divisors 1, 2, 4), then XT Standard and
; XT Fast; then Decode first. A rung whose output rate the device cannot play
; is skipped, and so is one whose output falls under X3_FLOOR while the source
; is above it (x3l_ok).
;
; THE ORDER OF THE PROBES IS NOT THE ORDER OF THE LADDER, and that is what
; bounds the wall time. Reduced-rate STANDARD does strictly less work than the
; full rate (SPEC.md 121.4: the subbands above 32/div are not reconstructed
; at all), so its cost falls with the divisor. The CHEAPEST valid STANDARD
; rung is probed first: when it fails, every dearer one would, and they are
; skipped unprobed. When it passes, Original is tried, then Half if Original
; failed - the ladder's own preference, decided by at most three probes. Then
; XT Standard and XT Fast, the same way down. An 8088 therefore probes three
; rungs and never Original or Half.
;
; A PROBE (x3l_launch, then the worker's x3l_wprobe) decodes the actual track
; from its start - no pre-roll to pay, the reservoir honestly empty there - at
; most X3_CAL_GRAN granules and X3_CAL_TICKS ticks, timed by pit_now around
; the front end, GRANULE and the feed's copy into the card's grant (verb 6);
; the disk's share is the priming reads' measured cost per byte times the
; bytes the probe consumed. A first granule X3_CAL_HOPE times short of passing
; ends it there. A rung passes at X3_SAFETY = 1.25 audio seconds a wall second.
; Results are cached for the session per (fingerprint, rung, device): the rung
; IS the backend, its profile and its divisor.
;
; WITHOUT A CARD there is nothing live to choose (SPEC.md 121.6: MP3 is never
; decoded into the speaker), and the same probes are the conversion's
; estimate. The OFFLINE choice (x3l_offpick) is the highest-quality rung
; measured at 1 / X3_OFFMAX or better - so a quality conversion stays available
; where the machine can afford it, and an 8088 is not given Original at 40
; minutes a song (SPEC.md 121.7, docs/xtmp3/ENGINE.md).
; =============================================================================
PH_LOW      equ 0                   ; the cheapest valid STANDARD rung
PH_ORIG     equ 1                   ; ...it passed: Original
PH_HALF     equ 2                   ; ...Original failed: Half
PH_XT       equ 3                   ; XT Standard
PH_XTF      equ 4                   ; XT Fast
PH_ONE      equ 5                   ; the user's override, alone
PH_DONE     equ 6
PH_HINT     equ 7                   ; an 8086: XT Standard first, alone
X3L_NONE    equ 0xFF                ; no rung chosen

; -----------------------------------------------------------------------------
; x3l_start - CHECK: the ladder for the current track from [x3_startms]
; -----------------------------------------------------------------------------
x3l_start:
%ifdef X3REPORT
    call x3r_cal0                   ; FIELD (x3rep.inc): the calibration's start
%endif
    mov byte [x3_md], MD_CHECK
    mov byte [x3_st+X3S_STATE], X3ST_CHECK
    mov byte [x3l_choice], X3L_NONE
    mov byte [x3l_low], X3L_NONE
    mov di, x3l_meas                ; nothing measured yet this run
    mov cx, X3Q_N * 2
    mov al, 0xFF
    call x3_fillb
    mov byte [x3l_ph], PH_LOW
    cmp byte [x3_st+X3S_QWANT], X3Q_AUTO
    jne .one
    push ax                         ; AN 8086 (OSAPI_CPU_INFO's tier 0 - a HINT,
    call OSAPI_CPU_INFO             ; never the verdict): XT Standard is probed
    cmp al, CPU_8086                ; FIRST, and alone if it is hopeless (see
    pop ax                          ; x3l_result) - the rung its conversion
    jne x3l_run                     ; will use, so the probe's OPEN is reused
    mov byte [x3l_ph], PH_HINT
    jmp short x3l_run
.one:
    mov byte [x3l_ph], PH_ONE
    mov byte [x3l_one], 0
    ; fall into x3l_run

; x3l_run - the machine, until a probe runs on the worker or it has decided
x3l_run:
.loop:
    call x3l_pick                   ; AL = the rung to measure, CF decided
    jc x3l_done
    mov [x3l_cur], al
    call x3l_cached                 ; CF = 0: AX = its speed, this session's
    jnc .known
    mov al, [x3l_cur]
    call x3l_launch                 ; CF = 0: the worker is measuring
    jnc .async
    xor ax, ax                      ; it could not even be opened: failed
.known:
    call x3l_result
    jmp short .loop
.async:
    ret

; x3l_probed - X3R_CAL: the worker's measure is in [x3l_res]
x3l_probed:
    cmp byte [x3_md], MD_CHECK
    jne .out                        ; (stopped meanwhile)
    mov ax, [x3l_res]
    call x3l_store
    call x3l_result
    jmp short x3l_run
.out:
    ret

; -----------------------------------------------------------------------------
; x3l_pick - the phase's rung. out: CF = 0 AL = it; CF = 1 the machine has
; decided ([x3l_choice], X3L_NONE for none). Moves through empty phases
; -----------------------------------------------------------------------------
x3l_pick:
    mov al, [x3l_ph]
    cmp al, PH_HINT
    jne .nh
    mov al, X3Q_XTSTD
    call x3l_ok
    jnc .got
    mov byte [x3l_ph], PH_LOW       ; (the device cannot play its rate)
    jmp short x3l_pick
.nh:
    cmp al, PH_LOW
    je .low
    cmp al, PH_ORIG
    je .orig
    cmp al, PH_HALF
    je .half
    cmp al, PH_XT
    je .xt
    cmp al, PH_XTF
    je .xtf
    cmp al, PH_ONE
    jne .done
    cmp byte [x3l_one], 0
    jne .done
    mov byte [x3l_one], 1
    mov al, [x3_st+X3S_QWANT]
    call x3l_ok1                    ; the device, and a part that does it -
    jc .onebad                      ; never the floor: the override is the
    clc                             ; user's (XT Speech is muffled by design)
    ret
.onebad:
    mov byte [x3l_ph], PH_DONE
    jmp short .done
.low:
    mov al, X3Q_QUART               ; the cheapest valid STANDARD rung
.lowl:
    call x3l_ok
    jnc .got
    dec al
    cmp al, X3Q_ORIG
    jae .lowl
    mov byte [x3l_ph], PH_XT        ; no STANDARD rung at all
    jmp short x3l_pick
.orig:
    mov al, X3Q_ORIG
    call x3l_ok
    jnc .got
    mov byte [x3l_ph], PH_HALF      ; Original SKIPPED (the device's rate, the
                                    ; floor) is not Original FAILED: Half is
                                    ; still between it and the cheaper rung
.half:
    cmp byte [x3l_low], X3Q_QUART   ; Half is between only when Quarter passed
    jne .low1
    mov al, X3Q_HALF
    call x3l_ok
    jnc .got
.low1:
    mov al, [x3l_low]               ; the cheaper rung that passed
    mov [x3l_choice], al
    mov byte [x3l_ph], PH_DONE
    jmp short .done
.xt:
    mov al, X3Q_XTSTD
    call x3l_ok
    jnc .got
    mov byte [x3l_ph], PH_XTF
.xtf:
    mov al, X3Q_XTFAST
    call x3l_ok
    jnc .got
    mov byte [x3l_ph], PH_DONE
.done:
    stc
    ret
.got:
    clc
    ret

; -----------------------------------------------------------------------------
; x3l_result - AX = the speed (x100) of rung [x3l_cur]: the next phase
; -----------------------------------------------------------------------------
x3l_result:
    push bx
    mov bl, [x3l_cur]
    xor bh, bh
    shl bx, 1
    mov [x3l_meas+bx], ax           ; for the offline choice and the estimate
    pop bx
    mov cl, [x3l_cur]
    mov ch, 0                       ; CH = 1 passed
    cmp ax, X3_SAFETY
    jb .f
    inc ch
.f:
    mov al, [x3l_ph]
    cmp al, PH_ONE
    je .one
    cmp al, PH_HINT
    je .hint
    cmp al, PH_LOW
    je .low
    cmp al, PH_ORIG
    je .orig
    cmp al, PH_HALF
    je .half
    cmp al, PH_XT
    je .xt
    or ch, ch                       ; PH_XTF
    jz .none
    jmp short .take
.one:
    mov byte [x3l_ph], PH_DONE
    or ch, ch
    jz .out
.take:
    mov [x3l_choice], cl
.none:
    mov byte [x3l_ph], PH_DONE
    jmp short .out
.low:
    or ch, ch
    jz .toxt
    cmp cl, X3Q_ORIG
    je .take
    mov [x3l_low], cl
    mov byte [x3l_ph], PH_ORIG
    jmp short .out
.toxt:
    mov byte [x3l_ph], PH_XT
    jmp short .out
.orig:
    or ch, ch
    jnz .take
    mov byte [x3l_ph], PH_HALF
    jmp short .out
.half:
    or ch, ch
    jnz .take
    mov al, [x3l_low]
    mov [x3l_choice], al
    mov byte [x3l_ph], PH_DONE
    jmp short .out
.xt:
    or ch, ch
    jnz .take
    mov byte [x3l_ph], PH_XTF
.out:
    ret
.hint:                              ; THE 8086'S ONE PROBE. XT Standard short
    mov byte [x3l_ph], PH_LOW       ; of passing by X3_CAL_HOPE (4x) leaves no
    push bx                         ; Auto rung within reach on this machine:
    mov bl, cl                      ; STANDARD at a quarter of the rate is no
    xor bh, bh                      ; cheaper than XT Standard on an 8088
    shl bx, 1                       ; (1.76 M a granule against 1.62 M, STD.md
    mov ax, [x3l_meas+bx]           ; and FIELD.md), and XT Fast is under 1.3
    pop bx                          ; times as fast. So nothing else is probed
    cmp ax, X3_SAFETY / X3_CAL_HOPE ; and the machine asks for a drive -
    jae .out                        ; 5.5 s on MartyPC's XT, not 16.1. Not
    mov byte [x3l_ph], PH_DONE      ; hopeless (a fast 8086, a V20): the whole
    ret                             ; ladder, this measure cached

; -----------------------------------------------------------------------------
; x3l_ok - may Auto try rung AL here? The device plays its rate, the build has
; its backend, and its output is not under the floor while the source is above
; it. x3l_ok1 is the same without the floor (the user's override).
; out: CF = 1 skipped. keeps every register
; -----------------------------------------------------------------------------
x3l_ok:
    mov byte [x3l_flr], 1
    jmp short x3l_okc

x3l_ok1:
    mov byte [x3l_flr], 0
x3l_okc:
    push ax
    push bx
    push cx
    push dx
    push word [x3_orate]            ; (asking may not move the output of a
    push word [x3_och]              ; stream that is playing: x3_ofmt writes
    push word [x3_odiv]             ; these, and a step's position is turned
    mov dh, [x3_st+X3S_FLAGS]       ; into ms at the rate it played at)
    mov dl, [x3_rung]
    mov [x3_rung], al
    call x3_ofmt                    ; [x3_orate] for it; CF: the device cannot
    jc .n
    call x3_rungrow
    mov cl, X3_PART_STD
    cmp byte [bx+RG_BE], X3K_XT
    jne .p
    mov cl, X3_PART_XT
.p:
    cmp cl, 0xFF
    je .n
    cmp byte [x3l_flr], 0           ; Auto's floor: not under X3_FLOOR while
    je .y                           ; the source is above it
    cmp word [x3_srate], X3_FLOOR
    jbe .y
    cmp word [x3_orate], X3_FLOOR
    jb .n
.y:
    clc
    jmp short .out
.n:
    stc
.out:
    mov [x3_rung], dl
    mov [x3_st+X3S_FLAGS], dh       ; (movs and pops keep CF)
    pop word [x3_odiv]
    pop word [x3_och]
    pop word [x3_orate]
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; x3l_launch - AL = a rung: opened, the stream at its start, the ring full,
; the card's grant taken (the feed's share is part of what is timed), and the
; worker measuring. out: CF = 1 it could not be opened (it fails)
; -----------------------------------------------------------------------------
x3l_launch:
    call x3_rungset
    jc .no
    mov byte [x3_st+X3S_STATE], X3ST_CHECK  ; (a refusal said ERROR)
    xor ax, ax
    xor dx, dx
    call x3_resync                  ; the stream's start
    jc .no
    mov ax, AP_LA_SZ                ; full: no read inside the timing
    call x3_prime
    call x3_fbytes
    mov [x3_fby], ax
    cmp byte [x3_card], 0
    je .ng
    call x3_grant                   ; (sized by the bytes a second)
    jc .no
.ng:
    mov word [x3l_goff], 0
    xor ax, ax
    mov [x3l_g], ax
    mov [x3l_acc], ax
    mov [x3l_acc+2], ax
    mov ax, [x3_ring+X3R_POS]
    mov [x3l_pos0], ax
    mov byte [x3l_warm], 1          ; the first granule untimed (below)
    call x3_wspawn
    jc .no
    mov byte [x3l_arm], 2           ; ARMED, started two timer passes on
    clc                             ; (x3_e_tick): this pass's face update -
    ret                             ; a new track's whole repaint, seconds on
                                    ; an XT - is not the probe's to pay
.no:
    mov byte [x3_st+X3S_STATE], X3ST_CHECK
    stc
    ret

; -----------------------------------------------------------------------------
; x3l_wprobe - THE PROBE (the worker): granules timed one by one, until
; X3_CAL_GRAN, X3_CAL_TICKS, the stream's end or a hopeless first one. Then
; the speed into [x3l_res] and X3R_CAL. One pass: nothing yields inside it but
; the scheduler, whose share is honestly part of what is measured
; -----------------------------------------------------------------------------
x3l_wprobe:
.loop:
    cmp byte [x3_job], WJ_CAL
    jne .out                        ; parked: the UI task stopped it
    mov al, [x3_grn]
    cmp al, [x3_grk]
    ja .have
    mov si, x3_ring
    call x3p_next
    jnc .staged
    cmp ax, X3P_END
    je .finish
    cmp word [x3l_g], 0             ; dry: what was measured is the measure
    jne .finish
    cmp byte [ap_src_done], 0
    jne .finish
    test byte [x3_req], X3R_READ
    jnz .out
    mov al, X3R_READ                ; (a tiny ring: wait for the gulp)
    call x3_kick
.out:
    ret
.staged:
    mov [x3_grn], al
    mov byte [x3_grk], 0
.have:
    call pit_now
    mov [x3l_t0], ax
    mov [x3l_t0+2], dx
    mov al, [x3_grk]
    inc byte [x3_grk]
    call x3p_gran                   ; DX:AX = the record
    mov es, dx
    mov si, ax
    mov dx, [ap_la_seg]
    mov di, X3_PCMOFF
    push ds
    mov ds, [x3_bseg]
    call far [cs:x3_bgran]          ; CX = frames
    pop ds
    push ds
    pop es
    jc .refused
    cmp byte [x3_card], 0           ; the feed's copy, into the grant (no
    je .timed                       ; stream is open: nothing plays it)
    call x3_fbytes
    mul cx
    mov cx, ax
    mov si, X3_PCMOFF
    mov di, [x3l_goff]
    and di, [x3_rmask]
    mov ax, [x3_rsz]
    sub ax, di
    cmp cx, ax
    jbe .one
    mov cx, ax
.one:
    add [x3l_goff], cx
    add di, [x3_grnt]
    mov al, 6
    push ds
    mov ds, [ap_la_seg]             ; verb 6's source is DS's (SPEC.md 34.5)
    call OSAPI_SND_STREAM
    pop ds
.timed:
    mov bx, x3l_t0
    call x3_pitd                    ; DX:AX = the granule's wall time
    cmp byte [x3l_warm], 0          ; THE FIRST GRANULE IS A WARM-UP, untimed:
    je .count                       ; it overlaps the UI task's repaint of the
    mov byte [x3l_warm], 0          ; state that launched the probe, which is
    mov ax, [x3_ring+X3R_POS]       ; not the machine's speed (and the bytes
    mov [x3l_pos0], ax              ; it consumed are not charged)
    jmp .loop
.count:
    add [x3l_acc], ax
    adc [x3l_acc+2], dx
    inc word [x3l_g]
    cmp word [x3l_g], 1
    jne .bounds
    call x3l_speed                  ; THE FIRST: hopeless?
    cmp ax, X3_SAFETY / X3_CAL_HOPE
    jb .finish
.bounds:
    cmp word [x3l_g], X3_CAL_GRAN
    jae .finish
    cmp word [x3l_acc+2], X3_CAL_TICKS  ; (65,536 counts a tick)
    jae .finish
    jmp .loop
.refused:
    mov word [x3l_g], 0
.finish:
    xor ax, ax
    cmp word [x3l_g], 0
    je .res
    call x3l_speed
.res:
    cmp ax, 0xFFFF                  ; (FFFFh is "not measured": a speed past
    jne .r2                         ; the scale is FFFEh)
    dec ax
.r2:
    mov [x3l_res], ax
    mov byte [x3_job], WJ_NONE
    mov al, X3R_CAL
    jmp x3_kick

; -----------------------------------------------------------------------------
; x3l_speed - AX = audio seconds a wall second x 100 for the probe so far:
; [x3l_g] granules of 576 source samples at [x3_srate], against [x3l_acc]
; plus the disk's share of the bytes consumed. Saturates at FFFFh
; -----------------------------------------------------------------------------
x3l_speed:
    push bx
    push cx
    push dx
    push si
    push di
    mov cx, [x3l_g]                 ; THE AUDIO, in pit counts
    call x3_audpit
    push dx
    push ax
    mov ax, [x3_dkby]               ; THE DISK: consumed x pit / byte
    mov dx, [x3_dkby+2]
    mov si, [x3_dkpit]
    mov di, [x3_dkpit+2]
.norm:
    or dx, dx
    jz .n16
    shr dx, 1
    rcr ax, 1
    shr di, 1
    rcr si, 1
    jmp short .norm
.n16:
    mov bx, ax                      ; BX = bytes, 16 bits
    xor ax, ax
    xor dx, dx
    or bx, bx
    jz .nodisk
    mov cx, [x3_ring+X3R_POS]
    sub cx, [x3l_pos0]
    mov ax, si
    mov dx, di
    call x3_mdiv                    ; DX:AX = the disk's pit counts
.nodisk:
    add ax, [x3l_acc]               ; THE WALL: decode + feed + disk
    adc dx, [x3l_acc+2]
    mov bx, ax
    mov cx, dx
    pop ax
    pop dx                          ; DX:AX = the audio
    call x3_ratio
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; x3_pitd - DX:AX = pit_now - the dword at [BX]: a span. pit_now can step
; back one whole tick (65,536 counts) under an emulator whose IRQ0 is late
; against its counter - seen on QEMU, a granule of 1.4 ms read as -53 ms -
; so a span one tick negative is given its tick back, and anything worse is
; 0. A span is never negative. keeps the rest
x3_pitd:
    call pit_now
    sub ax, [bx]
    sbb dx, [bx+2]
    jns .out
    inc dx                          ; one tick back
    jns .out
    xor ax, ax
    xor dx, dx
.out:
    ret

; x3_mdiv - DX:AX x CX / BX -> DX:AX (FFFF:FFFF when it does not fit).
; keeps the rest
x3_mdiv:
    push si
    push di
    mov si, dx
    mul cx
    mov di, ax                      ; N0
    mov ax, si
    mov si, dx
    mul cx
    add ax, si                      ; DX:AX = N2:N1
    adc dx, 0
    cmp dx, bx
    jae .sat
    div bx                          ; AX = the high quotient
    mov si, ax
    mov ax, di
    div bx
    mov dx, si
    jmp short .out
.sat:
    mov ax, 0xFFFF
    mov dx, ax
.out:
    pop di
    pop si
    ret

; x3_ratio - DX:AX x 100 / CX:BX -> AX, saturating at FFFFh (0 / 0: 0).
; keeps the rest
x3_ratio:
    push bx
    push cx
    push dx
    push si
    push di
.n:
    or cx, cx                       ; the divisor to sixteen bits, the
    jz .d                           ; dividend with it
    shr cx, 1
    rcr bx, 1
    shr dx, 1
    rcr ax, 1
    jmp short .n
.d:
    or bx, bx
    jz .zero
    mov cx, 100
    call x3_mdiv
    or dx, dx
    jz .out
.sat:
    mov ax, 0xFFFF
    jmp short .out
.zero:
    xor ax, ax
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; x3l_cached / x3l_store - the session's measures, per (fingerprint, rung,
; device). x3l_cached: CF = 0 AX = the speed of [x3l_cur]. keeps the rest
; -----------------------------------------------------------------------------
X3L_CREC    equ X3_FPSZ + 4         ; print, rung, device, speed

x3l_cached:
    push bx
    push cx
    push si
    push di
    call x3_devbyte                 ; AH = the device
    mov al, [x3l_cur]
    mov bx, x3l_cache
    mov cx, X3_NCAL
.c:
    cmp byte [bx+X3_FPSZ], 0xFF
    je .n
    cmp [bx+X3_FPSZ], ax
    jne .n
    mov si, bx
    call x3_fpcur                   ; DI -> this track's print
    push cx
    mov cx, X3_FPSZ
    call x3_memeq
    pop cx
    jne .n
    mov ax, [bx+X3_FPSZ+2]
    clc
    jmp short .out
.n:
    add bx, X3L_CREC
    loop .c
    stc
.out:
    pop di
    pop si
    pop cx
    pop bx
    ret

x3l_store:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov bx, [x3l_cnext]             ; round robin over X3_NCAL rows
    mov si, bx
    add bx, X3L_CREC
    cmp bx, X3_NCAL * X3L_CREC
    jb .w
    xor bx, bx
.w:
    mov [x3l_cnext], bx
    add si, x3l_cache
    mov [si+X3_FPSZ+2], ax
    call x3_devbyte
    mov al, [x3l_cur]
    mov [si+X3_FPSZ], ax
    mov di, si
    call x3_fpcur
    xchg si, di                     ; SI -> the print, DI -> the row
    push ds
    pop es
    mov cx, X3_FPSZ
    cld
    rep movsb
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; x3_devbyte - AH = the device in a byte: bit 0 a card, 1 16-bit, 2 stereo,
; 3 above 22 kHz. keeps the rest
x3_devbyte:
    push bx
    xor ah, ah
    cmp byte [x3_card], 0
    je .o
    mov ah, 1
    mov bx, [x3_caps]
    test bx, SND_CAP_PCM16
    jz .s
    or ah, 2
.s:
    test bx, SND_CAP_STEREO
    jz .h
    or ah, 4
.h:
    test bx, SND_CAP_PCM_HI
    jz .o
    or ah, 8
.o:
    pop bx
    ret

; x3_fpcur - DI -> the current entry's fingerprint. keeps the rest
x3_fpcur:
    push ax
    push dx
    mov al, [x3_st+X3S_CUR]
    xor ah, ah
    mov di, X3_FPSZ
    mul di
    mov di, ax
    add di, x3_qfp
    pop dx
    pop ax
    ret

; x3_memeq - CX bytes at SI and DI equal? ZF = 1. keeps all but flags
x3_memeq:
    push cx
    push si
    push di
    push es
    push ds
    pop es
    cld
    repe cmpsb
    pop es
    pop di
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; x3l_done - the machine has decided: LIVE on [x3l_choice] with a card (and no
; Decode First asked), else the decode-first dialog with the offline rung
; -----------------------------------------------------------------------------
x3l_done:
%ifdef X3REPORT
    call x3r_decided                ; FIELD (x3rep.inc): its end
%endif
    call x3_park
    mov byte [x3_job], WJ_NONE
    call x3_snd_close               ; (the probe's grant)
    cmp byte [x3_card], 0
    je .off
    cmp byte [x3_wantdf], 0
    jne .off
    mov al, [x3l_choice]
    cmp al, X3L_NONE
    je .off
    call x3_rungset
    jc .cant
    call x3_ms2samp
    call x3_live_at
    jc .cant
    ret
.cant:                              ; A RUNG THAT CANNOT START - its claims
    mov al, [x3l_choice]            ; refused (out of memory), the card - IS A
    mov [x3l_cur], al               ; RUNG THAT FAILED: stored at 0 for the
    xor ax, ax                      ; session, and the ladder runs again,
    call x3l_store                  ; every other measure out of the cache,
    call x3_snd_close               ; to the next rung or to Decode first.
    mov word [x3_st+X3S_ERR], 0     ; Never an error (each pass stores one
    jmp x3l_start                   ; more 0: it ends)
.off:
    mov byte [x3c_strict], 0        ; nothing live: any cache this device
    call x3c_cachefind              ; plays, before a question is asked
    jc .ask
    jmp x3_file_play
.ask:
    call x3l_offpick                ; AL = the conversion's rung
    call x3_fits                    ; ...in this machine (SPEC.md 121.10):
    jnc .fit                        ; else the minimum, stated - before a
    mov [x3_rung], al               ; drive is asked for
    mov [x3_st+X3S_QUAL], al
    call x3_needmsg
    mov byte [x3_md], MD_IDLE
    mov byte [x3_st+X3S_STATE], X3ST_ERROR
    ret
.fit:
%ifdef X3REPORT
    call x3r_wd                     ; FIELD (x3rep.inc): the report, EVENT D
%endif
    jmp x3c_begin                   ; (x3conv.inc) the drive dialog

; -----------------------------------------------------------------------------
; x3l_offpick - the CONVERSION's rung: the user's override; else the highest-
; quality rung measured at 1 / X3_OFFMAX or better; else XT Standard; else the
; cheapest STANDARD rung there is. out: AL = it
; -----------------------------------------------------------------------------
x3l_offpick:
    mov al, [x3_st+X3S_QWANT]
    cmp al, X3Q_AUTO
    jne .out
    mov al, X3Q_ORIG                ; quality order: Original .. XT Fast
.q:
    push bx
    mov bl, al
    xor bh, bh
    shl bx, 1
    mov bx, [x3l_meas+bx]
    cmp bx, 0xFFFF
    je .nm
    cmp bx, 100 / X3_OFFMAX
    pop bx
    jae .out
    jmp short .nq
.nm:
    pop bx
.nq:
    inc al
    cmp al, X3Q_XTFAST
    jbe .q
    mov al, X3Q_XTSTD
    call x3l_ok1
    jnc .out
    mov al, X3Q_QUART
.c:
    call x3l_ok
    jnc .out
    dec al
    cmp al, X3Q_ORIG
    jae .c
    mov al, X3Q_ORIG
.out:
    ret

; -----------------------------------------------------------------------------
; THE STATED MINIMUM (SPEC.md 121.10, 24.5's rule: a package that cannot run
; refuses in its own words). Each rung's working set, MEASURED (ENGINE.md
; section 6): the region, the eager carve, the part, its work, the rings -
; XT Standard at the ceiling's 390 KB (385-387 measured), XT Fast 360, XT
; Turbo 330, XT Speech 340, a STANDARD rung 220 (with a card's external
; ring). What the machine HAS is the same measure's: the region and the
; carve, and the heap free at launch.
; -----------------------------------------------------------------------------
x3_needkb:  dw 0, 220, 220, 220, 390, 360, 330, 340     ; by X3Q_*

; x3_haskb - AX = what this machine has for XTMP3, KB. keeps the rest
x3_haskb:
%ifdef X3STUB
    mov ax, [x3h_cfg+X3SB_HAS]      ; (the test hook's machine, when it says)
    or ax, ax
    jnz .o
%endif
    mov ax, [x3_free0]
    add ax, X3_REGIONKB
%ifdef X3_PARTS
    add ax, [op_kb]
%endif
.o:
    ret

; x3_fits - AL = a rung: CF = 1 this machine has less than it needs. keeps all
x3_fits:
    push ax
    push bx
    mov bl, al
    xor bh, bh
    and bl, 7
    shl bx, 1
    call x3_haskb
    cmp ax, [x3_needkb+bx]
    pop bx
    pop ax
    ret

; x3_needmsg - X3S_ERR = 'Needs 390 KB, has 300 KB' for rung [x3_rung]
; (24 characters: the status line says it whole). keeps everything
x3_needmsg:
    push ax
    push bx
    push dx
    push si
    push di
    mov di, x3_errbuf
    mov si, x3_s_needs
    call x3_put
    mov bl, [x3_rung]
    and bx, 7
    shl bx, 1
    mov ax, [x3_needkb+bx]
    xor dx, dx
    call dec_u32
    mov si, x3_s_kbhas
    call x3_put
    call x3_haskb
    xor dx, dx
    call dec_u32
    mov si, x3_s_kb2
    call x3_put
    mov byte [di], 0
    mov word [x3_st+X3S_ERR], x3_errbuf
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret

; x3l_below - the next live rung DOWN from [x3_rung] that this device and
; build allow (a step's target). out: CF = 0 AL = it, CF = 1 none (the last:
; the underruns are counted, nothing is converted mid-play)
x3l_below:
    mov al, [x3_rung]
.n:
    inc al
    cmp al, X3Q_XTFAST
    ja .none
    call x3l_ok
    jc .n
    clc
    ret
.none:
    stc
    ret

; =============================================================================
; THE FINISHED FILE (SPEC.md 121.7): played with no MP3 work at all - the
; decoder's part and work segments dropped first
; =============================================================================

; -----------------------------------------------------------------------------
; x3_file_play - the cache x3c_cachefind matched ([x3c_hit] its index row,
; the WAV's data range and format banked): on the card, or the speaker or a
; Covox through the Audio Player's bracket. From [x3_startms]
; -----------------------------------------------------------------------------
x3_file_play:
    call x3_be_drop                 ; no MP3 work from here
    mov ax, [x3_startms]
    mov dx, [x3_startms+2]
    call x3c_wavopen                ; (x3conv.inc) stand there, the producer on
    jc .err                         ; its data, x3_och/obits/orate its format
    mov byte [x3_st+X3S_QUAL], X3Q_AUTO
    mov al, [x3c_wprof]
    mov [x3_st+X3S_QUAL], al
    mov al, [x3_och]
    mov [x3_st+X3S_CHANS], al
    mov al, [x3_obits]
    mov [x3_st+X3S_BITS], al
    mov ax, [x3_orate]
    mov [x3_st+X3S_RATE], ax
    call x3_ms2samp                 ; the start, in samples
    cmp byte [x3_card], 0
    je .spk
    jmp x3_file_at
.spk:
    jmp x3_spk_play
.err:
    ret

; x3_ms2samp - [x3_startms] -> DX:AX output samples at [x3_orate]
x3_ms2samp:
    push bx
    push cx
    mov ax, [x3_startms]
    mov dx, [x3_startms+2]
    mov cx, 1000
    cmp dx, cx
    jae .z
    div cx                          ; AX = seconds, DX = ms
    push dx
    mul word [x3_orate]
    pop cx
    push dx
    push ax
    mov ax, cx
    mul word [x3_orate]
    mov cx, 1000
    div cx
    pop bx
    pop dx
    add ax, bx
    adc dx, 0
    jmp short .out
.z:
    xor ax, ax
    xor dx, dx
.out:
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; x3_file_at - DX:AX = an output sample: the finished WAV into the card from
; there - a direct seek, the data offset sample x frame bytes
; -----------------------------------------------------------------------------
x3_file_at:
    push ax
    push dx
    call x3_snd_close
    pop dx
    pop ax
    mov [x3_pbase], ax
    mov [x3_pbase+2], dx
    push ax
    push dx
    call x3_fbytes                  ; DX:AX x frame bytes + the data's start
    mov cx, ax
    pop dx
    pop ax
    push bx
    mov bx, dx
    mul cx
    push dx
    push ax
    mov ax, bx
    mul cx
    pop bx
    pop dx
    add dx, ax
    mov ax, bx
    pop bx
    add ax, [ap_data_off]
    adc dx, [ap_data_off+2]
    call x3_la_at
    mov ax, [x3_lasz]
    shr ax, 1
    call x3_prime
    mov byte [x3_sjob], WJ_FILE
    call x3_snd_open
    jc .out
    mov byte [x3_md], MD_FILE
    mov byte [x3_st+X3S_STATE], X3ST_FILE
    call x3_flagsvol
.out:
    ret

; =============================================================================
; THE PC SPEAKER AND THE COVOX: the Audio Player's bracket (SPEC.md 86.21,
; 86.21.3), over the finished WAV. Only an 8-bit mono file reaches it: the
; conversion makes that for a machine with no card, and a cache of another
; format is not this device's (x3c_cachefind)
; =============================================================================

; x3_spk_play - DX:AX = a sample: the bracket from there
x3_spk_play:
    mov ax, [x3_orate]
    mov [ap_rate], ax
    mov byte [apw_fmt], AP_FMT_PCM8
    mov byte [apw_spk], 0
    mov ax, [x3_startms]
    mov dx, [x3_startms+2]
    call x3_ms2samp
    call x3_spk_seek0
    mov byte [x3_md], MD_SPK
    mov byte [x3_st+X3S_STATE], X3ST_FILE
    call x3_flagsvol
    call aps_open                   ; THE BRACKET: back when paused, stopped,
    jmp x3_spk_back                 ; at the end, or refused

; x3_spk_seek - a paused speaker session moved: from DX:AX, a new bracket
x3_spk_seek:
    call aps_close
    call x3_spk_seek0
    mov byte [x3_md], MD_SPK
    mov byte [x3_st+X3S_STATE], X3ST_FILE
    mov byte [x3_paused], 0
    call aps_open
    jmp x3_spk_back

; x3_spk_seek0 - the producer and apspk's clock at sample DX:AX
x3_spk_seek0:
    mov [x3_pbase], ax
    mov [x3_pbase+2], dx
    call x3_spkrun0                 ; the per-track run, zeroed
    mov [ap_played], ax             ; the bar's: in the FILE's samples, which
    mov [ap_played+2], dx           ; are the WAV's own (8-bit mono)
    add ax, [ap_data_off]           ; one byte a sample
    adc dx, [ap_data_off+2]
    call x3_la_at
    mov ax, AP_LA_SZ
    jmp x3_prime

; x3_spkrun0 - apspk's per-track run zeroed. keeps all
x3_spkrun0:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    mov di, AP_TRK0
    mov cx, AP_TRKLEN
    xor al, al
    cld
    rep stosb
    pop es
    pop di
    pop cx
    pop ax
    ret

; x3_spk_back - the bracket returned: [ap_state] and [aps_exit] say how
x3_spk_back:
    mov ax, [ap_elapsed_s]
    mov [x3_st+X3S_ELAP], ax
    mov al, [aps_exit]
    cmp al, APX_DESK
    jne .ndesk
    mov byte [x3_st+X3S_STATE], X3ST_PAUSED
    mov byte [x3_paused], 1
    mov byte [x3_md], MD_IDLE
    clc
    ret
.ndesk:
    mov byte [x3_md], MD_IDLE
    mov byte [x3_paused], 0
    cmp al, APX_STOP
    jne .nstop
    call aps_close
    mov byte [x3_st+X3S_STATE], X3ST_READY
    clc
    ret
.nstop:
    cmp al, APX_END
    jne .err
    call aps_close
    jmp x3_advance                  ; (N inside the bracket, or the end)
.err:
    call aps_close
    mov ax, [ap_msg]
    mov [x3_st+X3S_ERR], ax
    mov byte [x3_st+X3S_STATE], X3ST_ERROR
    clc
    ret

; --- what apspk.inc asks of its host, answered over XTMP3's state ------------

; ap_prep_track - the current file from its START (apspk's P inside the
; bracket restarts the track). out: CF = 1 refused
ap_prep_track:
    xor ax, ax
    xor dx, dx
    call x3_spk_seek0
    clc
    ret

; apl_advance - XTMP3's queue is advanced OUTSIDE the bracket: the next entry
; may need a conversion, which asks a question no bracket can. So the answer
; is always "the end" (CF = 1), and x3_spk_back plays on from the desktop
apl_advance:
    stc
    ret

; apd_pull - a finished WAV is PCM: the ADPCM arm never runs
apd_pull:
    xor ax, ax
    ret

; ap_cinv_build - a finished WAV has no 'o8sp' chunk: never reached
ap_cinv_build:
    ret

; apu_repaint - the whole window through its own clip, the clip CLEARED after
; (SPEC.md 86.21.2: the bracket takes the bar clean)
apu_repaint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [ap_elapsed_s]
    mov [x3_st+X3S_ELAP], ax
    mov bx, [x3_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    mov si, bx
    call x3f_paint
    call OSAPI_WM_CLIP_CLEAR
.out:
    jmp ap_ret6

; apu_draw_dyn - the clock and the bar: x3_st, then the face's own update
apu_draw_dyn:
    push ax
    push bx
    mov ax, [ap_elapsed_s]
    mov [x3_st+X3S_ELAP], ax
    mov bx, [x3_win]
    call x3f_update
    pop bx
    pop ax
    ret

; apu_progress - AX = the bar's key: the second reached
apu_progress:
    mov ax, [ap_elapsed_s]
    ret

%include "os88spk.inc"
%include "os88spkfx.inc"
%include "apspk.inc"

; =============================================================================
; THE ENGINE'S STRINGS
; =============================================================================
x3_s_noread:  db 'Cannot read the file', 0
x3_s_empty:   db 'No audio in the file', 0
x3_s_card:    db 'The sound card refused', 0
x3_s_notask:  db 'No free task - close an app', 0
x3_s_rate:    db 'The device cannot play it', 0
x3_s_nobe:    db 'No decoder in this build', 0
x3_s_nopart:  db 'Cannot read its decoder', 0
x3_s_offer:   db 'The decoder cannot do it', 0
x3_s_table:   db 'Damaged decoder tables', 0
x3_s_decode:  db 'The decoder refused', 0
x3_s_clus:    db "Its disk's clusters are too large", 0
x3_s_ceil:    db 'Too little memory for that disk', 0
x3_s_packed:  db 'The MP3 is stored packed', 0
x3_s_needs:   db 'Needs ', 0
x3_s_kbhas:   db ' KB, has ', 0
x3_s_kb2:     db ' KB', 0

; =============================================================================
; THE ENGINE'S BSS, laid out by xtmp3.asm's X3B list
; =============================================================================
X3_FPSZ     equ 12                  ; a fingerprint: size, two CRC-32s

%macro X3_ENGBSS 0
    X3B x3_apbss, X3A_AT            ; the shared sources' (aplook, apspk)
    X3B x3_info, X3I_SIZE           ; THE STREAM: x3p_open's record...
    X3B x3_ring, X3R_SIZE           ; ...the compressed-input ring
    X3B x3_open, X3O_SIZE           ; the backend: OPEN's record...
    X3B x3_caps1, X3C_SIZE          ; ...CAPS'...
    X3B x3_ws, 2 * X3_MAXSEG        ; ...its work segments...
    X3B x3_nws, 1
    X3B x3_bpart, 1                 ; ...the part resident (FFh none)...
    X3B x3_bshr, 1                  ; ...shrunk to its resident half...
    X3B x3_bopen, 1                 ; ...OPEN and in effect (x3_reuse)...
    X3B x3_bkind, 1                 ; ...its kind, X3K_* (FFh none)...
    X3B x3_onrec, X3O_NSEG          ; ...the record the next OPEN would take
    X3B x3_errbuf, 26               ; x3_needmsg's sentence
    X3B x3_free0, 2                 ; the heap's free KB at launch (x3_memck)
    X3B x3_lasz, 2                  ; the look-ahead ring: its bytes, mask,
    X3B x3_lamask, 2                ; refill mark (x3_lasize)...
    X3B x3_lalw, 2
    X3B x3_laxkb, 2                 ; ...and a conversion's stage after it
    X3B x3_bseg, 2                  ; ...its segment...
    X3B x3_bvec, 4                  ; ...a verb's far vector (UI task's)...
    X3B x3_bgran, 4                 ; ...and GRANULE's (the worker's)
    X3B x3_qfpok, X3_QMAX           ; per queue entry: its print is known...
    X3B x3_qfp, X3_QMAX * X3_FPSZ   ; ...and it
    X3B x3_fname, 14                ; the playing file's name
    X3B x3_trkent, 2                ; ...its queue record
    X3B x3_flen, 4                  ; ...its bytes
    X3B x3_rdo, 4                   ; x3_readat's offset
    X3B x3_find, OSAPI_FIND_SZ      ; a FIND record
    X3B x3_spc, 26                  ; sectors a cluster, a volume (0 unknown)
    X3B x3_md, 1                    ; MD_*
    X3B x3_job, 1                   ; WJ_*: the worker's
    X3B x3_sjob, 1                  ; ...the stream's producer
    X3B x3_wbusy, 1                 ; a worker pass under way
    X3B x3_hasw, 1                  ; the worker spawned
    X3B x3_req, 1                   ; X3R_*: the worker's requests
    X3B x3_wfail, 2                 ; the decoder's refusal
    X3B x3_rderr, 1                 ; a source read failed
    X3B x3_paused, 1                ; a pause's position is banked...
    X3B x3_pmd, 1                   ; ...and the mode it paused
    X3B x3_ppos, 4
    X3B x3_startms, 4               ; a play's start, in ms
    X3B x3_wantdf, 1                ; 1 = decode first (D)
    X3B x3_card, 1                  ; the device: a card that streams...
    X3B x3_caps, 2                  ; ...its caps
    X3B x3_mixer, 1                 ; a mixer turns the knob
    X3B x3_gain, 2                  ; ...else this Q8 gain
    X3B x3_srate, 2                 ; the source: rate, channels
    X3B x3_snch, 1
    X3B x3_rung, 1                  ; X3Q_*: the rung open
    X3B x3_lastq, 1
    X3B x3_steprung, 1              ; the rung a step goes to...
    X3B x3_rampout, 1               ; ...1: the worker owes it a ramp out...
    X3B x3_rampst, 1                ; ...1: it ramped out, on real audio...
    X3B x3_stepby, 4                ; ...of these bytes since the open...
    X3B x3_rampin, 1                ; ...and the new rung's frames to ramp in
    X3B x3_orate, 2                 ; the OUTPUT: rate, channels, bits, divisor
    X3B x3_och, 1
    X3B x3_obits, 1
    X3B x3_odiv, 1
    X3B x3_fby, 2                   ; bytes a frame
    X3B x3_hand, 1                  ; THE STREAM: its handle...
    X3B x3_sopen, 1
    X3B x3_ghave, 1                 ; ...the grant...
    X3B x3_grnt, 2
    X3B x3_rsz, 2                   ; ...its size
    X3B x3_slp, 2                   ; the lead the worker may sleep on
    X3B x3_ext, 1                   ; ...1: an external ring, at x3_xseg:0
    X3B x3_xseg, 2
    X3B x3_rmask, 2
    X3B x3_ring_n, 2
    X3B x3_total, 2                 ; ...published, staged, consumed
    X3B x3_staged, 2
    X3B x3_cons, 2
    X3B x3_played, 4                ; ...bytes played since the open
    X3B x3_pbase, 4                 ; ...and the sample the open was at
    X3B x3_halves, 1
    X3B x3_lasthalf, 1
    X3B x3_sended, 1
    X3B x3_drvund, 1
    X3B x3_fill, 1                  ; 1 = the pre-roll is filling (UI task)
    X3B x3_grn, 1                   ; the staged frame's granules, and the next
    X3B x3_grk, 1
    X3B x3_frames, 4                ; frames staged since the resync
    X3B x3_grans, 2                 ; granules decoded
    X3B x3_clips, 2
    X3B x3_tframe, 2                ; the target frame of the last resync
    X3B x3_rsoff, 4                 ; ...the offset its scan began at
    X3B x3_nfetch, 5                ; fetches of each part (op_fetch), a byte
    X3B x3_disc, 4                  ; output frames still to drop
    X3B x3_limit, 4                 ; output frames still to keep (FFFFFFFFh)
    X3B x3_pkl, 1                   ; the meters' peaks since the last pass
    X3B x3_pkr, 1
    X3B x3_last, 2                  ; the last frame made (a ramp's start)
    X3B x3_rampn, 1
    X3B x3_wt0, 4                   ; THE SLOW WINDOW: a granule's start...
    X3B x3_wacc, 4                  ; ...the window's wall time...
    X3B x3_wg, 2                    ; ...its granules...
    X3B x3_wslow, 1                 ; ...slow windows in a row
    X3B x3_homed, 2                 ; the folder we were launched from
    X3B x3_homev, 1
    X3B x3_fhd, 2                   ; x3_fetchhome: where the track stood
    X3B x3_fhv, 1
    X3B x3_fhmv, 1                  ; ...and whether we moved from it
    X3B x3_dkpit, 4                 ; the disk's measured cost: time...
    X3B x3_dkby, 4                  ; ...for bytes
    X3B x3_rft0, 4                  ; (a gulp's start)
    X3B x3l_ph, 1                   ; THE LADDER: its phase...
    X3B x3l_cur, 1                  ; ...the rung measured...
    X3B x3l_low, 1                  ; ...the cheaper STANDARD rung that passed
    X3B x3l_one, 1                  ; ...the override measured
    X3B x3l_flr, 1                  ; (x3l_ok: the floor applies)
    X3B x3l_choice, 1               ; ...the decision
    X3B x3l_meas, X3Q_N * 2         ; ...this run's measures (FFFFh none)
    X3B x3l_res, 2                  ; the probe's: its answer...
    X3B x3l_g, 2                    ; ...granules timed
    X3B x3l_acc, 4                  ; ...their wall time
    X3B x3l_t0, 4
    X3B x3l_pos0, 2                 ; ...the file offset it began at
    X3B x3l_goff, 2                 ; ...the grant's rolling offset
    X3B x3l_warm, 1                 ; ...1: the next granule is the warm-up
    X3B x3l_arm, 1                  ; ...1: started by the next tick
    X3B x3_xtgr, 2                  ; the card's grant under XT (x3_plan)
%ifdef X3STUB
    X3B x3_dlog, 3 * 16             ; the debug log (X3LOG)
    X3B x3_dlogn, 1
%endif
    X3B x3l_cnext, 2                ; the session's cache: the next row...
    X3B x3l_cache, X3_NCAL * X3L_CREC
%ifdef X3REPORT
    X3_REPBSS                       ; FIELD (x3rep.inc): the report's
%endif
    X3_CONVBSS
%endmacro

%ifdef X3REPORT
%include "x3rep.inc"                ; FIELD: the 86Box report (never shipped)
%endif
