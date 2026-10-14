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
; THE BOUND COUNTERS (it_nprobe, it_maxprobe, it_span, it_maxspan, it_ncol)
; count the occlusion probe and the canvas span, which are wave 2's; they
; arrive with them rather than sit here reading zero.
;
; MARKS (for bp_trace): it_step0 / it_step1 bracket one head-step (itrules.inc
; it_move), it_wake0 / it_wake1 one wake's work under the lock and
; it_frame_end the end of a worker pass (isotron.asm). They are NOT labels:
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
