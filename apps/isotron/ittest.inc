; =============================================================================
; apps/isotron/ittest.inc - the harness (SPEC.md 108.7.2)
;
; Bytes in the bss, ALWAYS assembled (Excitebike's shape, SPEC.md 102): a
; shipped package carries them at no cost but their size, and a row reads and
; pokes them through the offsets tests/isotronlib.py assembles out of this
; source. Nothing here changes what a player sees unless a test writes it.
;
; INPUTS, written by a test before it sets it_tgo:
;
;   it_tseed    word  the game's RNG seed
;   it_tdiff    byte  the tier, 1..3
;   it_tflags   byte  bit 0 FAST    iterations without the clock
;                     bit 1 BOT     the harness's player (itai.inc it_bot)
;                     bit 2 NOFLOW  the title starts a game on it_tgo; no
;                                   crash pause, no wait, no sweep
;                     bit 3 HOLD    stop at every round's end: it_tpause = 1
;   it_tgo      byte  1 = start the NOFLOW game now (the worker clears it)
;   it_tinn     byte  entries in it_tinput (FAST without BOT): a script of
;   it_tinput   8 x   (word iteration, byte key) - K_LEFT, K_RIGHT, K_BOOST,
;               3 B   K_NORMAL - played at that iteration of every round,
;                     POSTED into the UI task's mailbox as a keystroke is (at
;                     most IT_TINMAX entries are read, whatever it_tinn says)
;
; THE HANDSHAKE: it_tpause is set by the worker when a HOLD round ends and
; cleared by the test once it has read the round. The worker does nothing
; while it is set.
;
; OUTPUTS, the game state the model is compared on after every round:
;
;   it_iter     word  iterations in the round
;   it_round    word  rounds played this game
;   it_sp/sc    word  the scores
;   it_h        byte  the tower NOW (the next round's height)
;   it_hplayed  byte  ...and the height the round was played at
;   it_lossrow  byte  computer wins in a row
;   it_prev     byte  the last round's winner (1 player, 2 computer)
;   it_winner   byte  ...and this round's
;   it_rng      word  the LCG's state
;   it_ck       word  rol(ck, 1) XOR idx over every cell placed this round
;   it_nsteps   word  head-steps this round
;   it_gover    word  games that have ended in three losses
;
; THE BOUND COUNTERS (SPEC.md 108.10.2), kept by itcanvas.inc it_span:
;
;   it_maxspan  byte  the most rows a step wrote this round: min(k*, 23)
;   it_maxprobe byte  ...the most canvas bytes a step's k* scan EXAMINED:
;                     0 on the fast path (no TOP below in the column), m
;                     when it found k* = m, len when it found none - counted
;                     where the scan ends, and at most 22 (108.10.2)
;
; FULL SCREEN's (wave 3, SPEC.md 108.12; the bytes live in isotron.asm's
; bss): it_fsm (a bracket is up), it_fsk (its surface), it_fsn (full-screen
; whole presents), it_fsout (brackets RETURNED - the kernel's repaint done),
; it_nwhole (windowed whole presents), it_ovf (the full-screen text records);
; marks it_fsw0 / it_fsw1, it_fsf0 / it_fs_frame (a frame's work: its
; logic and its present), it_fsx0 / it_fsback (itfs.inc). The harness also
; breaks on the rect presenters' entries (it_fr_cga, it_fr_vga, it_fr_herc:
; labels) and reads it_fy0..it_fc1 there, to tile a whole present.
;
; (A span cannot leave its column - y + k* <= 196 - so there is no column
; count: itcanvas.inc's %error holds that, by construction.)
; it_round_start sets them before the start cells' spans.
;
; MARKS (for bp_trace): it_step0 / it_step1 bracket one head-step's move and
; picture (itrules.inc it_move), it_hs0 / it_hs1 a computer head-step whole -
; its RNG byte, its decision, its move and its picture, it_ai1 the decision's
; end and it_span0 the picture's start inside it - it_wake0 / it_wake1
; one wake, the gfx lock and its release included, and it_frame_end the end
; of a worker pass (isotron.asm). They are NOT labels:
; IT_MARK makes each an `equ $`, and only under -DIT_MARKS (tests/isotronlib.py
; assembles with it), so no symbol splits a routine for tools/stkdepth.py and
; the shipped image is byte-identical either way.
; =============================================================================

IT_TF_FAST   equ 1
IT_TF_BOT    equ 2
IT_TF_NOFLOW equ 4
IT_TF_HOLD   equ 8
IT_TINMAX    equ 8

; --- inputs ------------------------------------------------------------------
    WORDV it_tseed
    BYTEV it_tdiff
    BYTEV it_tflags
    BYTEV it_tgo
    BYTEV it_tpause
    BYTEV it_tinn
    BUFV  it_tinput, IT_TINMAX * 3

; --- outputs -----------------------------------------------------------------
    WORDV it_iter
    WORDV it_round
    WORDV it_sp
    WORDV it_sc
    BYTEV it_h
    BYTEV it_hplayed
    BYTEV it_lossrow
    BYTEV it_prev
    BYTEV it_winner
    WORDV it_rng
    WORDV it_ck
    WORDV it_nsteps
    WORDV it_gover
    BYTEV it_maxprobe
    BYTEV it_maxspan
