; =============================================================================
; os8088 - apps/audio/audio.asm
;
; AUDIO.O88 - the Audio Player (SPEC.md 86). Lightweight background music
; playback for the IBM XT: a WAV file (unsigned 8-bit PCM, or IMA/DVI 4-bit
; ADPCM decoded straight to PCM8) is STREAMED from disk - never loaded whole -
; through the existing Sound Blaster ring-stream infrastructure
; (OSAPI_SND_STREAM, SPEC.md 34.5), and keeps playing while Sheet, Note Pad
; or anything else has the focus.
;
; The engine is the one SPEC.md 34 / 45.2 was built for, plus a look-ahead
; stage in front of it so a disk read's sch_lock hold cannot starve the DMA:
;
;     disk --OSAPI_FILE_READ_AT--> look-ahead ring (heap claim) --decoder-->
;         2048-byte halves --verb 6/1--> 16 KB SND ring --> SB DMA
;
; DIVISION OF LABOUR (the Tracker rule, SPEC.md 45.2):
;   - the UI task opens/closes the stream, parses WAV headers and does every
;     disk read (file slots are UI-task only, SPEC.md 20.6 rule 7);
;   - the WORKER decodes and feeds the ring, lock-free, on the any-task verbs
;     1/3/6; when the look-ahead ring runs low it raises [ap_req] and
;     OSAPI_WM_WAKEs the UI task, which services one cluster read in ap_onwake
;     and clears [ap_req] last (the FTPD handshake, SPEC.md 77.1);
;   - [ap_mixing] brackets a feed pass; ap_stream_close drains it before any
;     UI-side reset touches engine state (SPEC.md 45.2).
;
; Files (SPEC.md 86.2):
;   audio.asm     header, icon, .WAV assoc, constants, all bss, entry, data
;   apengine.inc  look-ahead ring, track open, refill, stream open/close, reap
;   apwork.inc    the worker task and its feed pass
;   apcb.inc      window callbacks (paint / click / key / menu / about / wake)
;   apwav.inc     RIFF/WAVE parser + validation            (prefix apw_)
;   apdec.inc     PCM8 pass-through + IMA/DVI ADPCM -> PCM8 (prefix apd_)
;   apui.inc      adapter-parameterised layout + drawing   (prefix apu_)
;   aplist.inc    playlist store, shuffle, repeat           (prefix apl_)
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'AUDIO PLAYER', ap_entry, 3     ; flags bit 0 = icon, bit 1 = assoc

; --- embedded 16x16 icon (SPEC.md 20.2): a speaker with two sound waves ------
    OS88_ICON16
    dw 0x03C0, 0x07D0, 0x0FD8, 0x1FF6, 0x3FFB, 0x3FFE, 0x3FFE, 0x3FFE
    dw 0x3FFE, 0x3FFE, 0x3FFB, 0x1FF6, 0x0FD8, 0x07D0, 0x03C0, 0x0000
    dw 0x0180, 0x01C0, 0x03C8, 0x07EC, 0x7ECC, 0x7EAC, 0x7EAC, 0x7EAC
    dw 0x7EAC, 0x7EAC, 0x7ECC, 0x07EC, 0x03C8, 0x01C0, 0x0180, 0x0000
    OS88_ICON16_END

; --- the extensions this program opens (SPEC.md 54.6, flags bit 1) ----------
    OS88_ASSOC16
    db 1
    OS88_ASSOC_EXT 'WAV'
    OS88_ASSOC16_END

; =============================================================================
; Constants
; =============================================================================
AP_FMT_PCM8    equ 0
AP_FMT_ADPCM   equ 1

AP_HDRBUF_SZ   equ 16384           ; header window / streaming read bounce (shared)
AP_RD_CHUNK    equ 16384           ; bytes one ap_refill_chunk read pulls (SPEC.md
                                   ; 86.5.2): cost disk work in int-13h CALLS,
                                   ; not sectors - a 4.77 MHz XT pays ~1-2 disk
                                   ; revolutions per call whatever it moves, and
                                   ; the read holds sch_lock (SPEC.md 34.5), so a
                                   ; 2 KB-per-call refill froze the machine ~5x a
                                   ; second. One big gulp every few seconds instead.
APD_OUTSZ      equ 2048            ; decoder scratch = one stage half
APD_BLKMAX     equ 2048            ; sanity cap on nBlockAlign
APD_INBUF      equ 512             ; ADPCM input bounce: one ap_la_get feeds many
                                   ; nibbles, instead of ap_la_get per byte (the
                                   ; call overhead was most of the decode cost on
                                   ; a real XT - SPEC.md 86.4)

AP_HALF        equ 2048           ; stage/feed granularity (SPEC.md 34.5)
AP_PREROLL     equ 6              ; halves staged before verb 0 (SPEC.md 45.17.2)
AP_MAXFEED     equ 6              ; halves per worker wake (bounds the burst)

AP_RING_KB     equ 16             ; SND ring grant, first tier
AP_RING_SM     equ 8             ; ...and the fallback tier
AP_RING_MAXB   equ 16384

AP_LA_SZ       equ 0x8000        ; look-ahead ring: 32 KB, power of two
AP_LA_MASK     equ 0x7FFF
AP_LA_LOW      equ 0x4000        ; refill when fewer than 16 KB remain - a whole
                                 ; AP_RD_CHUNK of headroom before the ring could
                                 ; starve the decoder on a slow (real) disk

; --- worker pacing (SPEC.md 86.5.1) -----------------------------------------
AP_SLP_FEED    equ 1              ; ticks the worker sleeps while feeding
AP_SLP_IDLE    equ 6             ; ...and while stopped / paused / ended
AP_DRAW_TICKS  equ 8             ; min ticks between dynamic redraws (~2/s)
AP_REAP_TICKS  equ 18            ; min ticks between end-of-track UI kicks
AP_QUE_TICKS   equ 90            ; how often a running player checks APQUEUE.DAT
                                ; (~5 s): the check REMOUNTS the system root
                                ; (ap_que_root), so it is deliberately rare and
                                ; skipped entirely while the ring is low. A
                                ; forwarded file appearing within 5 s is fine.

FR_NONE        equ 0
FR_READ        equ 1

AP_ST_STOP     equ 0
AP_ST_PLAY     equ 1
AP_ST_PAUSE    equ 2

; ap_add_file flags
AP_ADD_CUR     equ 1              ; make the new entry current
AP_ADD_PLAY    equ 2             ; ...and start it now (double-click / association)
AP_ADD_IDLE    equ 4             ; ...but only start it if nothing is playing (Open)

AP_MAXTRK      equ 48             ; playlist capacity
AP_ENTSZ      equ 16             ; per entry: 13 name + word dir + byte vol
AP_QUENAME_LEN equ 13
AP_QUEREC      equ 16             ; APQUEUE.DAT record: 13 name + word dir + byte vol

; button ids (apu_hit / dispatch)
AP_B_PREV  equ 0
AP_B_PLAY  equ 1
AP_B_PAUSE equ 2
AP_B_STOP  equ 3
AP_B_NEXT  equ 4
AP_B_SHUF  equ 5
AP_B_REP   equ 6
AP_B_NONE  equ 0xFF

; --- package-wide bss accumulator (the Arkanoid/Tracker %assign pattern) -----
%assign AP_BSS 0
%macro APB 1
%1 equ os88_image_end + AP_BSS
%assign AP_BSS AP_BSS + 1
%endmacro
%macro APW 1
%1 equ os88_image_end + AP_BSS
%assign AP_BSS AP_BSS + 2
%endmacro
%macro APD 1
%1 equ os88_image_end + AP_BSS
%assign AP_BSS AP_BSS + 4
%endmacro
%macro APBUF 2
%1 equ os88_image_end + AP_BSS
%assign AP_BSS AP_BSS + (%2)
%endmacro

; --- diagnostic counters (SPEC.md 86.5.1) - built only with -DAPROF, so the
;     shipping AUDIO.O88 pays nothing. Each is one `inc word` (~11 cycles) or
;     an `add`/`adc` for the dwords; the numbers are FORMATTED only when the
;     'D' key opens the Diagnostics view, never in the hot path.
%ifdef APROF
%macro APCW 1
%1 equ os88_image_end + AP_BSS
%assign AP_BSS AP_BSS + 2
%endmacro
%macro APCD 1
%1 equ os88_image_end + AP_BSS
%assign AP_BSS AP_BSS + 4
%endmacro
    APCW apc_iter                  ; worker loop iterations
    APCW apc_status                ; OSAPI_SND_STREAM verb 3 (status) calls
    APCW apc_stage                 ; verb 6 (stage) calls
    APCW apc_feed                  ; verb 1 (feed) calls
    APCW apc_wake                  ; OSAPI_WM_WAKE issued by us
    APCW apc_onwake                ; ap_onwake entries
    APCW apc_readat                ; OSAPI_FILE_READ_AT calls
    APCD apc_rbytes                ; bytes delivered by read_at
    APCW apc_refill                ; ap_refill_chunk that actually read
    APCW apc_under                 ; SND_ST_UNDER observed
    APCW apc_wend                  ; SND_ST_ENDED / watchdog observed
    APCW apc_uipaint               ; full content repaints (apu_draw / apu_repaint)
    APCW apc_progupd               ; dynamic redraws (apu_draw_dyn)
    APCW apc_declc                 ; apd_pull calls
    APCD apc_decby                 ; PCM8 bytes / ADPCM samples produced
    APCW apc_quechk                ; APQUEUE.DAT poll attempts
    APCW apc_opentk                ; ap_open_track calls
    APCW apc_parse                 ; apw_parse calls
    APCW apc_slide                 ; apw_slide calls
    APCW apc_prime                 ; ap_prime calls
    APCW apc_pdisc                 ; ap_stage_half input-starved, decoded nothing
    APBUF apdg_buf, 900            ; APDIAG.TXT staging (formatted on the 'D' key)
    APW   apdg_t0                  ; scratch: byte count for the write
    APB   apdg_req                 ; 1 = ap_onwake owes an APDIAG.TXT dump
%macro APCNT 1
    inc word [%1]
%endmacro
%macro APADD 2
    add word [%1], %2
    adc word [%1+2], 0
%endmacro
%else
%macro APCNT 1
%endmacro
%macro APADD 2
%endmacro
%endif

; --- shell / window state ------------------------------------------------
    APW ap_win
    APB ap_state
    APB ap_have_worker
    APB ap_have_sb
    APB ap_shuffle
    APB ap_repeat
    APW ap_msg
    APB ap_arg_pending
    APBUF ap_arg_name, 16
    APW  ap_arg_dir
    APB  ap_arg_vol
    APW  ap_elapsed_s              ; seconds played of the current track
    APW  ap_total_s               ; track duration in seconds (0 = unknown)
    APW  ap_bpt_lo                 ; bytes of PCM8 per system tick (rate/18.2)
    APW  ap_bpt_hi
    APW  ap_sub                    ; sub-tick byte remainder for the clock
    APW  ap_t0                     ; OSAPI_GET_TICKS at play start (unused net)
    APB  ap_diag                   ; 1 = the window shows the Diagnostics view
    APW  ap_last_s                 ; last elapsed second the dynamic redraw drew
    APW  ap_last_bar               ; last progress-bar fill (px) it drew
    APW  ap_last_draw_t            ; OSAPI_GET_TICKS at that redraw (throttle)
    APW  ap_reap_kick_t            ; ...and at the last end-of-track UI kick
    APW  ap_que_t                  ; ...and at the last APQUEUE.DAT poll
    APBUF ap_snap, SYS_SNAPSHOT_SIZE   ; single-instance probe buffer (entry only)
    APBUF ap_querec, 128           ; APQUEUE.DAT records, staged for a poll

; --- look-ahead ring ---------------------------------------------------
    APW ap_la_seg
    APW ap_la_wr
    APW ap_la_rd
    APW ap_clu
    APD ap_rd_pos
    APW ap_skip
    APW ap_rd_len                  ; bytes the current ap_refill_chunk read pulls
    APD ap_data_off
    APD ap_data_end
    APD ap_got
    APB ap_src_done

; --- SND ring stream -------------------------------------------------
    APW ap_grant
    APB ap_ghave
    APW ap_ring
    APW ap_rmask
    APW ap_preroll                 ; halves staged before verb 0: ring/AP_HALF - 1,
                                   ; capped at AP_PREROLL (6 at 16 KB, 3 at 8 KB)
    APB ap_hand
    APB ap_sopen
    APB ap_mixing
    APB ap_ended
    APB ap_lasthalf
    APB ap_paused
    APW ap_total
    APW ap_consumed
    APW ap_rate
    APB ap_req
    APB ap_pcmbits                 ; 8 for PCM8, effective 8 after ADPCM decode
    APB ap_first_paint             ; 1 until ap_onwake does the settled repaint

; --- decoder state (apdec.inc) --------------------------------------
    APB apd_codec
    APW apd_balign
    APW apd_want
    APW apd_bleft
    APW apd_pred
    APW apd_index
    APB apd_hasbyte
    APB apd_hinib
    APB apd_curbyte
    APBUF apd_hdr, 4
    APBUF apd_tmp, 2
    APW   apd_inlen                ; bytes currently in apd_inbuf
    APW   apd_inpos                ; read cursor into apd_inbuf
    APBUF apd_inbuf, APD_INBUF
    APBUF apd_out, APD_OUTSZ

; --- WAV parser state (apwav.inc) ---------------------------------
    APW apw_name
    APW ap_trk_dir                 ; the playing track's folder (OSAPI_FILE_HERE):
    APB ap_trk_vol                 ; File > Open elsewhere MOVES us (SPEC.md 38.10)
    APD apw_flen
    APW apw_clu
    APW apw_rdcap
    APD apw_winb
    APW apw_winlen
    APB apw_winn
    APW apw_pos
    APD apw_lpos
    APW apw_ckid
    APD apw_cksz
    APB apw_gotfmt
    APW apw_tag
    APW apw_bits
    APB apw_ok
    APB apw_fmt
    APW apw_rate
    APW apw_balign
    APW apw_spb
    APD apw_datoff
    APD apw_datlen
    APW apw_err

; --- shared header window / cluster bounce -----------------------
; THE int 13h TARGET, so 512-ALIGNED (CLAUDE.md, SPEC.md 2.4): the loader's
; claim is whole KB, so the OFFSET decides - align the accumulator here and
; pad the image end to 512 below. A base that is not answers 09h on the one
; transfer in four that straddles a 64 KB page, and the refill takes that CF
; as end of data: the song stops a few seconds in, on the machine whose heap
; happened to put the package there.
%assign AP_BSS ((AP_BSS + 511) / 512) * 512
    APBUF ap_scratch, AP_HDRBUF_SZ
apw_hdrbuf equ ap_scratch

; --- playlist (aplist.inc) --------------------------------------
    APBUF apl_ent, AP_MAXTRK * AP_ENTSZ
    APB   apl_n                    ; entries in use
    APB   apl_cur                  ; index of the current track (0..n-1)
    APBUF apl_ord, AP_MAXTRK       ; shuffle order (indices)
    APB   apl_opos                 ; position within apl_ord

; --- ui layout (apui.inc) --------------------------------------
    APBUF apu_rects, 7 * 8         ; 7 buttons x {x1,y1,x2,y2} words
    APW   apu_pbx1                 ; progress bar rect
    APW   apu_pby1
    APW   apu_pbx2
    APW   apu_pby2
    APW   apu_cw                   ; last content w/h laid out against
    APW   apu_ch
    APB   ap_abon                  ; 1 = the About card is up
    APW   apab_x1                  ; ...its top-left, for the text lines
    APW   apab_y1

; =============================================================================
; ap_entry - the loader calls this once (SPEC.md 20.1). The loader shows the
;            window afterwards, so we may not draw or spawn the worker here.
; out: CF = 0 ok / CF = 1 fatal (no window)
; =============================================================================
ap_entry:
    call OSAPI_SND_CAPS            ; AX = merged caps word
    xor cl, cl
    test ax, SND_CAP_PCM_BG
    jz .nosb
    mov cl, 1
.nosb:
    mov [ap_have_sb], cl

    call OSAPI_ARG_FILE           ; CF = 1 launched empty (the common case)
    jc .nodoc
    ; SI -> name in KERNEL_SEG; ES is KERNEL_SEG on entry - copy it out
    mov di, ap_arg_name
    mov cx, 16
.cpn:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    or al, al
    jz .cpndone
    dec cx
    jnz .cpn
.cpndone:
    mov byte [ap_arg_name+15], 0
    mov [ap_arg_dir], dx
    mov [ap_arg_vol], bl
    mov byte [ap_arg_pending], 1

    ; --- single instance (SPEC.md 86.11.1): OFF by default (-DAP_HANDOFF to
    ;     enable). If an Audio Player is already live, hand it this document
    ;     through APQUEUE.DAT and DO NOT open a second window. Disabled because
    ;     the running instance's poll must REMOUNT the system root every few
    ;     seconds to read the file, which stalls a 4.77 MHz XT badly. Without
    ;     it, a second double-click opens a second window (the OS default).
%ifdef AP_HANDOFF
    call ap_find_sibling          ; CF = 0: a live sibling exists
    jc .nodoc
    call ap_que_write             ; append the banked arg to APQUEUE.DAT
    jc .nodoc                     ; could not write (read-only volume): fall
                                  ; back to a normal launch rather than lose it
    mov si, ap_s_queued
    push ds
    pop es
    xor cx, cx
    call OSAPI_TOAST
    stc                           ; abort THIS launch - the loader frees the
    ret                           ; region, no window, no handover (SPEC.md 21)
%endif
.nodoc:
    call aplist_init

    mov si, ap_tpl
    call OSAPI_WM_CREATE          ; BX = window ptr, CF on table full
    jc .fail
    mov [ap_win], bx

    mov si, ap_menus
    call OSAPI_MENU_SET

    mov si, ap_about
    call OSAPI_ABOUT_SET

    mov ax, ap_onwake
    call OSAPI_WM_ONWAKE

    mov ax, ap_onclose
    call OSAPI_WM_ONCLOSE

    mov word [ap_msg], ap_s_ready
    cmp byte [ap_have_sb], 0
    jne .m_ok
    mov word [ap_msg], ap_s_nosb
.m_ok:
    mov byte [ap_state], AP_ST_STOP

    ; The loader shows the window AFTER the entry proc returns, so the first
    ; W_PAINT can run before wm_fit has settled the frame and OSAPI_WM_GEOM /
    ; OSAPI_WM_CONTENT answer for it. Kick ourselves once: ap_onwake then does
    ; a clean repaint with the window fully on the glass (SPEC.md 86.7).
    mov byte [ap_first_paint], 1
    mov bx, [ap_win]
    call OSAPI_WM_WAKE

    clc
    ret
.fail:
    stc
    ret

; =============================================================================
; includes: the engine, the worker, the callbacks, the decoder, the parser,
;           the ui, the playlist.
; =============================================================================
%include "apengine.inc"
%include "apwork.inc"
%include "apcb.inc"
%include "apwav.inc"
%include "apdec.inc"
%include "apui.inc"
%include "aplist.inc"

; =============================================================================
; Data: window template, menus, labels, strings
; =============================================================================
ap_tpl:
    dw 190, 90, 300, 232          ; x, y, w, h  (wm_fit clamps onto the screen)
    dw ap_ttl, ap_paint, ap_onkey, ap_onclick
ap_ttl:   db 'Audio Player', 0

    OS88_MENUSET ap_menus, ap_ttl, ap_oncmd
        OS88_MENU ap_m_file, ap_i_file, 4
        OS88_MENU ap_m_play, ap_i_play, 7
    OS88_MENUSET_END ap_menus
ap_m_file:   db 'File', 0
ap_i_file:   dw ap_it_open, ap_it_clear, ap_it_diag, ap_it_exit
AP_CMD_OPEN  equ 0
AP_CMD_CLEAR equ 1
AP_CMD_DIAG  equ 2
AP_CMD_EXIT  equ 3
ap_it_open:  db 'Open...', 0
ap_it_clear: db 'Clear playlist', 0
%ifdef APROF
ap_it_diag:  db 'Diagnostics (D)', 0
%else
ap_it_diag:  db MENU_DIS, 'Diagnostics (D)', 0   ; no counters in this build
%endif                                            ; (SPEC.md 47 rule 5)
ap_it_exit:  db 'Exit', 0
ap_m_play:   db 'Play', 0
ap_i_play:   dw ap_it_play, ap_it_pause, ap_it_stop, ap_it_prev, ap_it_next, \
                ap_it_shuf, ap_it_rep
ap_it_play:  db 'Play', 0
ap_it_pause: db 'Pause', 0
ap_it_stop:  db 'Stop', 0
ap_it_prev:  db 'Previous', 0
ap_it_next:  db 'Next', 0
ap_it_shuf:  db 'Shuffle', 0
ap_it_rep:   db 'Repeat', 0

ap_l_prev:  db 'Prev', 0
ap_l_play:  db 'Play', 0
ap_l_pause: db 'Pause', 0
ap_l_stop:  db 'Stop', 0
ap_l_next:  db 'Next', 0
ap_l_shuf:  db 'Shuf', 0
ap_l_rep:   db 'Rep', 0

ap_s_ready:   db 'Ready', 0
ap_s_playing: db 'Playing', 0
ap_s_paused:  db 'Paused', 0
ap_s_stopped: db 'Stopped', 0
ap_s_nosb:    db 'No Sound Blaster - playback off', 0
ap_s_nomem:   db 'Out of memory', 0
ap_s_nofile:  db 'Playlist is empty', 0
ap_s_loaderr: db 'Cannot play this file', 0
ap_s_notask:  db 'No free task - close an app and retry', 0
ap_s_hirate:  db 'Rate needs a Sound Blaster Pro', 0
ap_s_opening: db 'Opening...', 0
ap_s_endlist: db 'End of playlist', 0
ap_s_queued:  db 'Sent to the running Audio Player', 0
ap_s_added:   db 'Added to playlist', 0
ap_s_notwav:  db 'Not a .WAV file', 0
ap_s_full:    db 'Playlist is full', 0
ap_s_dup:     db 'Already in the playlist', 0
ap_qfile:     db 'APQUEUE.DAT', 0

%include "os88ui.inc"
    OS88_BSS AP_BSS
    align 512, db 0                ; os88_image_end on a 512 boundary, so the
    OS88_IMAGE_END                 ; aligned AP_BSS offset above is physical
