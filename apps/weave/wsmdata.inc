; =============================================================================
; os8088 - apps/weave/wsmdata.inc     WEAVE.WSM's DATA (WEAVE-SPEC 6.10.4)
;
; Split out of apps/weave/wcanvas.asm for ONE reason: the boot-sector
; differential (apps/weave/hosttest/weavecv.asm) %includes wspr.inc and
; wwork.inc UNCHANGED, with no kernel under them, and those files name every
; label below. What that gate runs is the shipping text of the composer and
; the frame loop rather than a copy of it (WEAVE-SPEC 12.3's rule, applied to
; a third core).
; =============================================================================

; =============================================================================
; THE STATE BLOCK (WEAVE-SPEC 6.10.4)
;
; Every offset is pinned, because the package reads and writes this block with
; wblob.inc's w_b/w_w/w_pb/w_pw rather than through a verb - which is what
; keeps the resident cost of a counter at zero. The %if after each field is
; not decoration: a field inserted in the wrong place here is a package that
; reads its frame counter out of the middle of the staging ring, assembles
; cleanly and runs wrong.
; =============================================================================
    align 2, db 0
wsm_state:
wsm_run:    db 0
wsm_ack:    db 0
wsm_sleep:  dw 1
wsm_phase:  dw 0
wsm_frame:  dw 0
wsm_cvseg:  dw 0
wsm_win:    dw 0
wsm_bseg:   dw 0
wsm_spoff:  dw 0
wsm_nspr:   dw 0
wsm_blits:  dw 0
wsm_frames: dw 0
wsm_ovf:    dw 0
wsm_tickp:  db 0
wsm_rhead:  db 0
wsm_rtail:  db 0
wsm_cx:     dw 0
wsm_cy:     dw 0
wsm_kstate: times 5 db 0
wsm_dirty:  times WSM_MAXBAND db 0
wsm_cont:   times 15 db 0
wsm_ring:   times WSM_RING * WSM_REC db 0

%if (wsm_run    - wsm_state) != WSS_RUN
  %error "WEAVE.WSM state block: WSS_RUN moved"
%endif
%if (wsm_ack    - wsm_state) != WSS_ACK
  %error "WEAVE.WSM state block: WSS_ACK moved"
%endif
%if (wsm_sleep  - wsm_state) != WSS_SLEEP
  %error "WEAVE.WSM state block: WSS_SLEEP moved"
%endif
%if (wsm_frame  - wsm_state) != WSS_FRAME
  %error "WEAVE.WSM state block: WSS_FRAME moved"
%endif
%if (wsm_cvseg  - wsm_state) != WSS_CVSEG
  %error "WEAVE.WSM state block: WSS_CVSEG moved"
%endif
%if (wsm_win    - wsm_state) != WSS_WIN
  %error "WEAVE.WSM state block: WSS_WIN moved"
%endif
%if (wsm_blits  - wsm_state) != WSS_BLITS
  %error "WEAVE.WSM state block: WSS_BLITS moved"
%endif
%if (wsm_frames - wsm_state) != WSS_FRAMES
  %error "WEAVE.WSM state block: WSS_FRAMES moved"
%endif
%if (wsm_ovf    - wsm_state) != WSS_OVF
  %error "WEAVE.WSM state block: WSS_OVF moved"
%endif
%if (wsm_rhead  - wsm_state) != WSS_RHEAD
  %error "WEAVE.WSM state block: WSS_RHEAD moved"
%endif
%if (wsm_rtail  - wsm_state) != WSS_RTAIL
  %error "WEAVE.WSM state block: WSS_RTAIL moved"
%endif
%if (wsm_ring   - wsm_state) != WSS_RING
  %error "WEAVE.WSM state block: WSS_RING moved"
%endif

; --- the dispatcher's banked arguments, and the composer's scratch -----------
; NOT in the state block: nothing outside this file may read them, and a
; package that could would be reading an implementation rather than a
; contract.
wsm_cds:    dw 0                    ; the caller's DS, for BIND and DRAIN
wsm_ab:     dw 0
wsm_ac:     dw 0
wsm_ad:     dw 0
wsm_af:     db 0
wsm_posted: db 0                    ; the frame staged something and owes a
                                    ; WM_WAKE

; wwork.inc's per-frame working set. Statics rather than a stack frame, for
; the reason every C file in this package gives (SPEC.md 73.5, said about
; assembly): the worker's stack is 384 bytes and the tick and mouse ISRs land
; their frames on it while we run. Nothing here is re-entrant and nothing here
; needs to be - one worker per instance (SPEC.md 20.6 rule 1), and the UI
; task's own two entries (PAINT and SPRITE) touch none of it.
wsm_cid:    dw 0                    ; the canvas's comp_id
wsm_spri:   dw 0                    ; the sprite being moved
wsm_srec:   dw 0                    ; ...and its record's offset
wsm_hits:   db 0                    ; the four edge tests, taken before any
            db 0                    ; bounce (6.10.1)
wsm_oute:   dw -1                   ; the open edge it went fully out of
wsm_pair:   dw 0                    ; the collision pass's counters
wsm_ci:     dw 0
wsm_cj:     dw 0
wsm_ki:     dw 0                    ; the key poll's
wsm_kdn:    db 0
            db 0

; wspr.inc's. The composer runs inside one call and never nests.
wsm_r0:     dw 0                    ; the run's first and last pixel rows
wsm_r1:     dw 0
wsm_sx:     dw 0                    ; the canvas's screen origin
wsm_sy:     dw 0
wsm_nband:  dw 0
wsm_b0:     dw 0
wsm_b1:     dw 0
wsm_nblit:  dw 0
wsm_bofs:   dw 0
wsm_wb:     dw 0                    ; the sprite under composition
wsm_ph:     dw 0
wsm_sx0:    dw 0
wsm_sy0:    dw 0
wsm_rtop:   dw 0
wsm_nrow:   dw 0
wsm_fsz:    dw 0
wsm_cpy:    dw 0
wsm_msrc:   dw 0
wsm_rowlo:  dw 0
wsm_rowhi:  dw 0
wsm_shift:  db 0
            db 0
wsm_bels:   dw 0                    ; 4.9 rule 5's BELs emitted. Not in the
                                    ; state block: it is the boot-sector
                                    ; differential's reading and nothing on
                                    ; the machine asks for it

; The composer's frame scratch (WEAVE-SPEC 6.10.2). One sprite frame's image
; and its AND mask are copied here before a run is composed, at a FIXED
; separation, so the pixel loop reads `[si]` and `[si+WSM_SIMGMAX]` with a
; constant displacement and needs only one pointer for both. Without that it
; needs two source pointers and a segment each, and the 8086 has neither to
; spare inside that loop. 2.11 caps a frame at 8 bytes x 64 rows.
WSM_SIMGMAX equ 512
wsm_simg:   times WSM_SIMGMAX db 0
wsm_smsk:   times WSM_SIMGMAX db 0

wsm_end:
