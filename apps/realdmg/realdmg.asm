; =============================================================================
; os8088 - apps/realdmg/realdmg.asm
;
; REALDMG: a Game Boy in a window (docs/REALDMG-SPEC.md section 14). The
; core is parts 0 and 1 of this file - 64KB of generated SM83 handlers,
; apps/realdmg/rdggen.py's, in two halves one segment apart - and the view the
; Game Boy's memory lives in is part 2, a zeroed 64KB carve; the cartridge is
; a claim of its own size.
; rdgsys.inc runs the machine, rdgppu.inc composes its frame, and this file is
; the window round them: open a .GB, run it to the wall clock in a worker,
; and put each frame it has time for on the glass.
;
; THE WORKER owns the machine. It counts BIOS ticks into Game Boy frames
; owed (59.7275 Hz against 18.2065: 840/256 of a frame a tick), runs the core
; in budgets of RW_BUDGET M-cycles - the core runs with interrupts off
; (REALDMG-SPEC section 5.2), so the budget is the IRQ latency, and the tick
; pre-empts it between two - and yields once a frame. It composes and blits a
; frame when the draw clock says so (rw_drawdue: half the machine at most
; once the game has caught up, a third while it is behind), so the game keeps
; its speed and the picture takes what is left (REALDMG-SPEC section 15.2).
; =============================================================================

%define RDG_LOSSY                       ; F10, F11: the lossy picture's toggles
                                        ;   (rdgppu.inc, REALDMG-SPEC 13.4)
%include "os88api.inc"
%include "os88mega.inc"                  ; the shipped build refuses under
                                        ;   CPU_F_PM (REALDMG-SPEC 10.3.4)
%include "sndpkg.inc"                   ; SOUND.DRV's FM verbs (rwfm.inc)

%ifdef RDG_AOT                          ; A PRECOMPILED CARTRIDGE (REALDMG-SPEC
                                        ;   20.10): one game's package, so it
%ifdef PKG_MEGA
    OS88_HEADER 'REALDMG', rw_entry, OS88_F_ICON | OS88_F_PARTS, OS88_STACK_384 | OS88_HIGH | OS88_HIGHMEM
%else
    OS88_HEADER 'REALDMG', rw_entry, OS88_F_ICON | OS88_F_PARTS, OS88_STACK_384
%endif
%elifdef PKG_MEGA                       ;   opens no extension
    OS88_HEADER 'REALDMG', rw_entry, OS88_F_ICON | OS88_F_ASSOC | OS88_F_PARTS, OS88_STACK_384 | OS88_HIGH | OS88_HIGHMEM
%else                                   ; (the arm's region and its claims of
    OS88_HEADER 'REALDMG', rw_entry, OS88_F_ICON | OS88_F_ASSOC | OS88_F_PARTS, OS88_STACK_384
%endif                                  ;   64KB or less may go above 1MB:
                                        ;   REALDMG-SPEC 16.2.6, 16.2.7)

; --- embedded 16x16 icon: a Game Boy, the screen and the cross --------------
    OS88_ICON16
    dw 0x0000, 0x3FF8, 0x3FF8, 0x3FF8, 0x3FF8, 0x3FF8, 0x3FF8, 0x3FF8
    dw 0x3FF8, 0x3FF8, 0x3FF8, 0x3FF8, 0x3FF8, 0x3FF8, 0x3FF8, 0x0000
    dw 0x0000, 0x3FF8, 0x2008, 0x2FE8, 0x2FE8, 0x2FE8, 0x2FE8, 0x2008
    dw 0x2008, 0x2468, 0x2E08, 0x2468, 0x2008, 0x2008, 0x3FF8, 0x0000
    OS88_ICON16_END

; --- the extension this program opens ---------------------------------------
%ifndef RDG_AOT
    OS88_ASSOC16
    db 2
    OS88_ASSOC_EXT 'GB'
    OS88_ASSOC_EXT 'GBC'                ; (a game for both: rw_hdrchk refuses
                                        ;   one that is the Color's alone)
    OS88_ASSOC16_END
%endif

%define OP_ONMOVE rw_partsmv          ; the carve MOVES (SPEC.md 20.12.12,
                                        ;   rwmove.inc)
%ifdef PKG_MEGA
%define OP_WANT_CSEG                    ; the core as a code/data PAIR under
%endif                                  ;   CPU_F_PM (rw_start, 10.3.2)
%include "os88parts.inc"

RW_CONT_W       equ 160
RW_NTCT         equ 27
RWJ_PART        equ 3                   ; the recompiler's part (rdgjitp.asm)                  ; ticks the notice stays up: 1.5s
RW_CONT_H       equ 144
RW_BUDGET       equ 2000            ; M-cycles a core run: ~5.6 ms on a 286
RW_BUDGET86     equ 700             ; ...and on an 8088, where 2000 is ~20 ms
                                    ;   with interrupts off - two bytes of a
                                    ;   1200-baud serial mouse into an 8250
                                    ;   that holds one
RW_PERTICK      equ 840             ; frames a BIOS tick, x 256
RW_BEHIND       equ 30              ; frames a machine catches up at most:
                                    ;   half a second, longer than an 8088's
                                    ;   draw, which the frames during it
                                    ;   must outlast or the game slows
RW_OWEDMAX      equ RW_BEHIND * 256
RW_ROMMAX_KB    equ 256             ; one claim's; more is the bank cache's
RW_SLOTMAX      equ 40              ; ...slots at most (rwbank.inc)
RW_SLOTWANT     equ 8               ; ...slots the plan asks for while the
                                    ;   recompiler still has memory to give
                                    ;   up (rw_load); 4 when it has none
RW_PRFN         equ 2048            ; Static's profile: entries, 4 bytes each,
RW_PRFTAIL      equ RW_PRFN * 4     ;   then the file's trailer: 'RDP1', the
RW_PRFFILE      equ RW_PRFTAIL + 9  ;   header's 014Dh byte and 014Eh word,
;
; REALDMG.CFG, in SYSTEM\APPDATA: 'RD', the version, then the settings -
; bit 0 Machine > Cache: 128K. Read at the start, written at the toggle.
; Version 2 goes on with a count at +4 and, from +8, RW_CFGGN games' own
; settings, the latest first: the header's 014Dh byte and 014Eh word (as a
; profile's name takes them), then Speed in bits 0-2 and Sound in 3-4. A
; version 1 file is read for its byte and games none.
; Version 3 goes on with Machine > Controls' eight keys at RW_CFGKEY, in
; S_JOY's order (rwkeys.inc); a version 2 file is read with the shipped keys.
RW_CFGKEY       equ 8 + RW_CFGGN * 4
RW_CFGN         equ RW_CFGKEY + 8
RW_CFGGN        equ 32
RW_CFGVER       equ 3
RW_CFGBIG       equ 01h
RW_SPEEDS       equ 4               ; CPU > Speed's settings (rw_spdtab)
RW_JIT2N        equ 900             ; a profile this many blocks or more
                                    ;   asks for the second cache segment:
                                    ;   the first holds ~600 of Kirby's
                                    ;   Dream Land's (REALDMG-SPEC 16.2.1)
                                    ;   and the core's holes ~350 more
%ifndef RW_SQZLIM
RW_SQZLIM       equ 0210h           ; RT_SQUEEZE's (tests/rdgmega.py): where a
%endif                              ;   cache segment's code ends
RW_XPRESKB      equ 256             ; kern_mega's arm: the extended pool its
                                    ;   extra cache tiers leave at least - a
                                    ;   quarter of it when that is more
                                    ;   (rw_jitxget, REALDMG-SPEC 16.2.6)
%ifndef RW_XDEF
RW_XDEF         equ 5               ; ...and how many it takes without
%endif                              ;   Machine > Cache: All (16.2.5's five;
                                    ;   tests/rdgmega.py's squeezed leg 2)
RW_PRFKB        equ 9               ;   the count (REALDMG-SPEC 16.8)
%ifdef RDG_AOT
RWA_MAX         equ 29              ; code images past the core, at most:
                                    ;   what RWA_ROWS holds with the ROM
                                    ;   beside it and the recompiler's row
                                    ;   (5 + 2 x 29 + 1 = 64). It was 14,
                                    ;   which no real-mode heap could hold
                                    ;   anyway; on kern_mega the pool can
RWA_TABLE       equ 3               ; the parts: the block table (16KB)...
RWA_META        equ 4               ; ...the meta and the list (rwaot.inc)...
RWA_CODE        equ 5               ; ...the relocations' and the list's parts
                                    ;   from here, then each image's two halves
RWA_ROWS        equ 64              ;   then the ROM's pieces if it is in the
                                    ;   package; the most rows a table has
%endif

; --- the bss: the standard's words first, then ours --------------------------
rw_win          equ os88_image_end + OP_BSS
rw_state        equ rw_win + 2      ; 0 no game, 1 running, 2 stopped
rw_pause        equ rw_state + 1
rw_resetq       equ rw_pause + 1    ; Reset asked; the worker does it
rw_abon         equ rw_resetq + 1   ; the About card is up
rw_shown        equ rw_abon + 1     ; [rw_fseg] holds a frame
rw_argpend      equ rw_shown + 1
rw_argvol       equ rw_argpend + 1
rw_last         equ rw_argvol + 1   ; word: the tick last counted
rw_owed         equ rw_last + 2     ; word: frames owed, x 256
rw_romseg       equ rw_owed + 2
rw_romkb        equ rw_romseg + 2
rw_banks        equ rw_romkb + 2
rw_argdir       equ rw_banks + 2
rw_spawned      equ rw_argdir + 2
rw_name         equ rw_spawned + 2  ; 16: the game's file name
rw_ovl          equ rw_name + 16    ; the F1 overlay is up
rw_f1was        equ rw_ovl + 1      ; F1 was held at the last look
rw_nrun         equ rw_f1was + 1    ; word: frames run since the last count
rw_ndrawn       equ rw_nrun + 2     ; word: ...and drawn
rw_tstat        equ rw_ndrawn + 2   ; word: the tick that count began
rw_ovltxt       equ rw_tstat + 2    ; 16: 'NNN% NNfps', composed once a second
rw_budget       equ rw_ovltxt + 16  ; word: M-cycles a core run, by CPU tier
rw_drawt        equ rw_budget + 2   ; dword: when the next draw may start
                                    ;   (rw_clk's counts: rw_drawdue)
rw_dlast        equ rw_drawt + 4    ; dword: how long the last draw took
rw_pfmt         equ rw_dlast + 4    ; [rw_pseg] holds: 0 nothing, 1 the dither,
                                    ;   4 BLITP's planes (rw_planes)
rw_nobp         equ rw_pfmt + 1     ; BLITP refused us: dither until a paint
rw_incore       equ rw_nobp + 1     ; the worker is in the machine (rwfsx.inc)
rw_fsq          equ rw_incore + 1   ; full screen asked: the worker keeps out
rw_infsx        equ rw_fsq + 1      ; the machine runs in the bracket
rw_fsrate       equ rw_infsx + 1    ; ...paced by FSXF_RATE, not the tick
rw_fsrun        equ rw_fsrate + 1   ; frames run undrawn
rw_ovlnew       equ rw_fsrun + 1    ; rw_ovltxt changed since it was drawn
rw_fowed        equ rw_ovlnew + 1   ; word: rate periods counted by the hook
rw_fsleft       equ rw_fowed + 2    ; word: frames still to run this turn
rw_fsi          equ rw_fsleft + 2   ; FSI_SIZE: the mode's info block
rw_fontp        equ rw_fsi + FSI_SIZE ; dword: the kernel's glyph table, for
rw_font0        equ rw_fontp + 4    ;   the bracket's text, and its first
                                    ;   code (rw_fscopy; a spare byte after)
rw_jitseg       equ rw_font0 + 2    ; word: the recompiler's 64KB, or 0
rw_jitwant      equ rw_jitseg + 2   ; translate when there is a cache for it
rw_jitq         equ rw_jitwant + 1  ; Machine > Recompiler asked; the worker
rw_tone         equ rw_jitq + 1     ; word: the hertz the speaker plays (0 off)
rw_sch          equ rw_tone + 2     ; byte: the square channel on the speaker
rw_ramseg       equ rw_sch + 1      ; word: cartridge RAM's banks, or 0
rw_rambanks     equ rw_ramseg + 2   ; byte: 8KB banks there (0 = none)
rw_batt         equ rw_rambanks + 1 ; byte: a battery: save it on close
rw_rtc          equ rw_batt + 1     ; byte: an MBC3 clock
rw_savelen      equ rw_rtc + 1      ; word: the .SAV's RAM bytes
rw_rtcgap       equ rw_savelen + 2  ; dword: seconds the save was put away
rw_rtcsave      equ rw_rtcgap + 4   ; 16: the clock, across a reset
rw_savename     equ rw_rtcsave + 16 ; 13: NAME.SAV
rw_fskind       equ rw_savename + 13 ; byte: full screen's writer (RW_FS*)
rw_htab         equ rw_fskind + 1   ; 512: a full-screen writer's table
rw_chg          equ rw_htab + 512   ; 144: the lines rdg_renderd changed
rw_fsper        equ rw_chg + 144    ; word: rate periods in the bracket (rw_clk)
rw_logon        equ rw_fsper + 2    ; byte: Machine > Log is on (rwlog.inc)
rw_logn         equ rw_logon + 1    ; word: records taken
rw_ltick        equ rw_logn + 2     ; word: ticks since the log began
rw_acomp        equ rw_ltick + 2    ; word: lines composed this second
rw_achg         equ rw_acomp + 2    ; word: ...and changed
rw_ams          equ rw_achg + 2     ; word: ms drawing this second
rw_amax         equ rw_ams + 2      ; byte: the longest draw, ms
rw_aowed        equ rw_amax + 1     ; byte: the most frames owed
rw_aevt         equ rw_aowed + 1    ; word: RLE_* this second, RLE2_* its
                                    ;   high byte
rw_stspan       equ rw_aevt + 2     ; word: rw_logsec's ticks
rw_llen         equ rw_stspan + 2   ; word: the log text's length
rw_ldig         equ rw_llen + 2     ; word: where its name's digits are
rw_lfile        equ rw_ldig + 2     ; 13: its name
rw_pkgdir       equ rw_lfile + 13   ; word: REALDMG.O88's folder...
rw_pkgvol       equ rw_pkgdir + 2   ; byte: ...and volume (the log's)
rw_romdir       equ rw_pkgvol + 1   ; word: the game's folder...
rw_romvol       equ rw_romdir + 2   ; byte: ...and volume (its .SAV's)
rw_hdir         equ rw_romvol + 1   ; word: where rw_fgo found us...
rw_hvol         equ rw_hdir + 2     ; byte
rw_dgen         equ rw_hvol + 1     ; byte: rw_drawnow's count (rw_timed)
rw_bcache       equ rw_dgen + 1     ; byte: the cartridge is the bank cache's
rw_nslot        equ rw_bcache + 1   ; word: its slots (rwbank.inc)
rw_slotseg      equ rw_nslot + 2    ; word: ...their claim
rw_xbase        equ rw_slotseg + 2  ; dword: the store above 1MB
rw_bcur         equ rw_xbase + 4    ; FSEQ_SIZE: the load's read cursor
rw_bnum         equ rw_bcur + FSEQ_SIZE ; word: the bank being loaded
rw_slotbank     equ rw_bnum + 2     ; RW_SLOTMAX words: each slot's bank
rw_bankreq      equ rw_slotbank + RW_SLOTMAX * 2 ; word: the bank x 2 the
                                    ;   worker waits for, or 0
rw_closing      equ rw_bankreq + 2  ; byte: the window is closing
rw_nmiss        equ rw_closing + 1  ; word: banks fetched
rw_pavail       equ rw_nmiss + 2    ; word: the heap's largest run at load
rw_tscan        equ rw_pavail + 2   ; dword: when rdg_renderd's scan ended
rw_asms         equ rw_tscan + 4    ; dword: counts scanning this second
rw_acms         equ rw_asms + 4     ; dword: ...composing
rw_agms         equ rw_acms + 4     ; dword: ...running the Game Boy
rw_tgame        equ rw_agms + 4     ; dword: when this frame's run began
rw_lstat        equ rw_tgame + 4    ; 8: S_EVCOUNT, S_JITNB, S_JITFL and
                                    ;   rw_nmiss at the last record
rw_jitmode      equ rw_lstat + 8    ; byte: CPU > Recompiler 1, Interpreter
                                    ;   2, Static Recompiler 3 (REALDMG-SPEC
                                    ;   16.8)
rw_undrawn      equ rw_jitmode + 1  ; byte: a frame ran since the last draw
rw_klatch       equ rw_undrawn + 1  ; byte: buttons TYPED since the last look
rw_kraw         equ rw_klatch + 1   ; byte: the buttons at the last look
rw_kpref        equ rw_kraw + 1     ; byte: of each opposite pair, the newer
rw_prfseg       equ rw_kpref + 1    ; word: Static's profile (REALDMG-SPEC
                                    ;   16.8), RW_PRFKB, or 0
rw_prfdirty     equ rw_prfseg + 2   ; byte: the profile grew this session
rw_npre         equ rw_prfdirty + 1 ; word: the blocks the last preload made
rw_prfname      equ rw_npre + 2     ; 13: NAME.RDP
rw_f2was        equ rw_prfname + 13 ; F2 was held at the last look...
rw_f3was        equ rw_f2was + 1    ; ...and F3
rw_msgt         equ rw_f3was + 1    ; byte: a note is on the speed line
rw_msgclr       equ rw_msgt + 1     ; byte: ...and has just come down
rw_msgend       equ rw_msgclr + 1   ; word: the tick it comes down at
rw_msg          equ rw_msgend + 2   ; 21: the note (rw_note)
rw_logseg       equ rw_msg + 22     ; word: the log's records, RW_LOGN x
                                    ;   RW_LOGSZ, claimed while it is on
                                    ;   (rwlog.inc), or 0 (after a spare
                                    ;   byte: the words after stay even)
rw_lrec         equ rw_logseg + 2   ; RW_LOGSZ: the second's record, made
                                    ;   here and copied into the claim
rw_lrec2        equ rw_lrec + RW_LOGSZ ; RW_LOGSZ: the write's, a record
                                    ;   back out of it (the UI task's own)
rw_lring        equ rw_lrec2 + RW_LOGSZ ; word: the write's copy of
                                    ;   [rw_logseg]
rw_prfon        equ rw_lring + 2    ; byte: Machine > Profile is on (and a
                                    ;   spare byte: rw_jit2seg stays even)
rw_jit2seg      equ rw_prfon + 2    ; word: the recompiler's second 64KB
                                    ;   (REALDMG-SPEC 16.2.1), or 0
rw_cfg          equ rw_jit2seg + 2  ; RW_CFGN: REALDMG.CFG as it is read
                                    ;   and written (SYSTEM\APPDATA)
rw_cfgip        equ rw_cfg + RW_CFGN ; byte: os88data.inc's stamp for it
rw_big          equ rw_cfgip + 1    ; byte: Machine > Cache: 128K (the
                                    ;   setting, kept in REALDMG.CFG)
rw_seg2q        equ rw_big + 1      ; byte: the cache's segments changed:
                                    ;   the worker's to start it again
rw_f3q          equ rw_seg2q + 1    ; byte: the mode F3 asked for in the
                                    ;   window, when it wants the UI task
                                    ;   first - Static's profile, or the
                                    ;   cache claimed (rw_jitprep, rw_onwake)
rw_jfreeq       equ rw_f3q + 1      ; byte: the worker is on the interpreter:
                                    ;   the cache back (rw_jfree, rw_onwake)
rw_jitqn        equ rw_jfreeq + 1   ; byte: ...and the switch it queued says
                                    ;   itself on the speed line
rw_speed        equ rw_jitqn + 1    ; byte: CPU > Speed, 0-3 (rw_spdtab)
rw_sndm         equ rw_speed + 1    ; byte: Machine > Sound: 0 On, 1 at full
                                    ;   speed only, 2 Off
rw_spdq         equ rw_sndm + 1     ; byte: F4 changed Speed: the UI task's
                                    ;   to keep it (rw_onwake)
rw_f4was        equ rw_spdq + 1     ; byte: F4 was held at the last look
rw_fsfrac       equ rw_f4was + 1    ; byte: a frame's 256ths the bracket's
                                    ;   clock owes at a Speed under 100%
rw_gkon         equ rw_fsfrac + 1   ; byte: rw_gkey is the game's
rw_spdk         equ rw_gkon + 1     ; word: Speed, 256ths of the Game Boy's
rw_pertick      equ rw_spdk + 2     ; word: ...and frames a BIOS tick, x 256
rw_gkey         equ rw_pertick + 2  ; 3: the game's header 014Dh, 014Eh
rw_hmask        equ rw_gkey + 3     ; byte: the block table's mask (S_HMASK,
                                    ;   REALDMG-SPEC 16.3): 31 for 64KB, 7
rw_kwin         equ rw_hmask + 1    ; word: Machine > Controls' window, or 0
rw_kx0          equ rw_kwin + 2     ; word: its content's left...
rw_ky0          equ rw_kx0 + 2      ; word: ...and top, as last asked
rw_kbx          equ rw_ky0 + 2      ; word: rw_kbox's box: left, top, width
rw_kby          equ rw_kbx + 2
rw_kbw          equ rw_kby + 2
rw_kbn          equ rw_kbw + 2      ; byte: ...and its cells
rw_ksel         equ rw_kbn + 1      ; byte: the button waiting for a key, or
                                    ;   RW_KNONE
rw_kmsg         equ rw_ksel + 1     ; byte: what its screen says (rw_kmsgs)
rw_kbuf         equ rw_kmsg + 1     ; 19: a name padded to its cells
rw_slot         equ rw_kbuf + 19    ; byte: the save state's slot, 0-8 for 1-9
rw_stq          equ rw_slot + 1     ; byte: a state being saved or loaded -
                                    ;   the worker backs off (1), and it has
                                    ;   (2): the UI task's machine (rwstate.inc)
rw_stlen        equ rw_stq + 1      ; word: the state's RAM part, bytes
rw_stbyte       equ rw_stlen + 2    ; byte: OSAPI_FILE_READ's 1-byte buffer
rw_stmsg        equ rw_stbyte + 1   ; 25: what rw_stsay says (rw_hdrchk's
                                    ;   'REALDMG: MBC xxh unknown' and its NUL)
rw_ssname       equ rw_stmsg + 25   ; 13: NAME.SS1 to NAME.SS9
rw_pseg         equ rw_ssname + 13  ; word: the frame as the screen takes it,
                                    ;   RW_PBUF bytes in a claim of its own
                                    ;   (the region had no room for them)
rw_fmon         equ rw_pseg + 2     ; byte: the FM chip is ours (rwfm.inc)
rw_fmsh         equ rw_fmon + 1     ; 16: what each channel's B0h, A0h and
                                    ;   two levels were last written
rw_fmbuf        equ rw_fmsh + 16    ; 32: a frame's register/value pairs
rw_slotmin      equ rw_fmbuf + 32   ; byte: the bank plan's least slots
rw_rpmode       equ rw_slotmin + 1  ; byte: a replay recording or playing,
                                    ;   RPM_* (rwreplay.inc)
rw_rpatb        equ rw_rpmode + 1   ; byte: the last run ended a frame
rw_rpseg        equ rw_rpatb + 1    ; word: the replay's claim...
rw_rpcap        equ rw_rpseg + 2    ; word: ...its bytes
rw_rplen        equ rw_rpcap + 2    ; word: the pairs' end in it
rw_rppos        equ rw_rplen + 2    ; word: the next pair, playing
rw_rpleft       equ rw_rppos + 2    ; byte: frames left of the pair's...
rw_rpcur        equ rw_rpleft + 1   ; byte: ...buttons
rw_rpfr         equ rw_rpcur + 1    ; dword: frames recorded or played
rw_rpq          equ rw_rpfr + 4     ; byte: RPQ_*, the UI task's to do
rw_rpres        equ rw_rpq + 1      ; byte: RPR_*, how it ended
rw_rpslot       equ rw_rpres + 1    ; byte: the replay's slot, 0-8
rw_rpstq        equ rw_rpslot + 1   ; byte: F6 again, at the next frame
rw_rpname       equ rw_rpstq + 1    ; 13: NAME.RPn
rw_fseg         equ rw_rpname + 13  ; word: rdg_render's frame (monochrome;
                                    ;   two shade planes, 40 bytes a line),
                                    ;   5,760 bytes after RW_PBUF's in
                                    ;   [rw_pseg]'s claim - the region had no
                                    ;   room for them either
rw_lbuf         equ rw_fseg + 2     ; 40: a line of it, for the Hercules and
                                    ;   CGA writers (rwfsx.inc)
rw_ntc          equ rw_lbuf + 40    ; the window's notice is up (rw_ntcwait)
rw_ntct         equ rw_ntc + 1      ; word: the tick it went up
rw_mhold        equ rw_ntct + 2     ; byte: a load holds every claim still -
                                    ;   rw_mfree declares nothing (rwmove.inc)
rw_slotlent     equ rw_mhold + 1    ; byte: bank slots lent to the side
                                    ;   features (rw_mclaim, rwbank.inc)
rw_lgrab        equ rw_slotlent + 1 ; byte: rw_slotlend stilled the machine
rw_loans        equ rw_lgrab + 1    ; byte: side buffers living in lent slots
rw_lused        equ rw_loans + 1    ; word: ...their paragraphs, from the top
rw_pace         equ rw_lused + 2    ; byte: full screen's pacing Steady (1) or
                                    ;   Free (0) - Shift+F2 (rwfsx.inc)
rw_pk           equ rw_pace + 1     ; byte: Steady draws every k-th frame...
rw_pup          equ rw_pk + 1       ; byte: ...cadence draws behind in a row
rw_pdn          equ rw_pup + 1      ; byte: ...and with a frame's room in a row
rw_pidl         equ rw_pdn + 1      ; word: the cycle's waits, counts / 4
rw_pt0          equ rw_pidl + 2     ; word: a wait's start (rw_pwait)
rw_plast        equ rw_pt0 + 2      ; word: [rw_nrun] at the last draw...
rw_phist        equ rw_plast + 2    ; 8 words: draws by the frames run since
                                    ;   the one before, 1-7 and 8+ (rw_timed;
                                    ;   tests/rdgcore/rdgwin.py reads them)
%ifdef RDG_AOT
rw_aseg         equ rw_phist + 16   ; RWA_MAX words: each code image's claim,
                                    ;   or 0 (rwaot.inc)
rw_anseg        equ rw_aseg + RWA_MAX * 2 ; byte: images claimed
rw_afix         equ rw_anseg + 1    ; byte: the images are read and put right
rw_ans          equ rw_afix + 1     ; byte: images the meta names
rw_ahot         equ rw_ans + 1      ; byte: ...of them the HOT ones, first
rw_ahm          equ rw_ahot + 1     ; byte: the block table's mask
rw_acore        equ rw_ahm + 1      ; word: the core's segment (part 0)
rw_atseg        equ rw_acore + 2    ; word: the block table's (part 3)
rw_amseg        equ rw_atseg + 2    ; word: the meta's (part 4)...
rw_alseg        equ rw_amseg + 2    ; word: ...the list's, inside it...
rw_aln          equ rw_alseg + 2    ; word: ...and its entries
rw_asega        equ rw_aln + 2      ; word: the first code image there is,
                                    ;   or the core (S_JITSEGA)
rw_afseg        equ rw_asega + 2    ; word: the list's filter, after the table
rw_arow         equ rw_afseg + 2    ; word: rw_apart's row...
rw_awant        equ rw_arow + 2     ; word: ...bytes still to come...
rw_adst         equ rw_awant + 2    ; word: ...where they go...
rw_askip        equ rw_adst + 2     ; word: ...bytes of the chunk before them
rw_apos         equ rw_askip + 2    ; dword: ...the chunk's file offset
rw_aclb         equ rw_apos + 4     ; word: bytes a cluster
rw_abseg        equ rw_aclb + 2     ; word: the bounce...
rw_abcap        equ rw_abseg + 2    ; word: ...its bytes
rw_abown        equ rw_abcap + 2    ; byte: ...it is a claim of ours
rw_aemb         equ rw_abown + 1    ; byte: this load is the ROM in the
                                    ;   package (rw_aname, REALDMG-SPEC 20.13)
rw_aromkb       equ rw_aemb + 1     ; word: its KB...
rw_arombase     equ rw_aromkb + 2   ; dword: ...and its byte offset in the
                                    ;   package's file
rw_aebank       equ rw_arombase + 4 ; word: rw_bread's next bank out of it
rw_aunh         equ rw_aebank + 2   ; byte: the load's hold is let go for
                                    ;   the plan (rw_aloose)
rw_alx          equ rw_aunh + 1     ; RDG_LXMAX x 4: the list's parts, each
                                    ;   segment and entries (REALDMG-SPEC
                                    ;   20.17)...
rw_alxn         equ rw_alx + RDG_LXMAX * 4 ; byte: ...how many
rw_alrow        equ rw_alxn + 1     ; byte: ...and the first's row
rw_arr          equ rw_alrow + 1    ; byte: the relocations' first row...
rw_arn          equ rw_arr + 1      ; byte: ...and their rows
rw_arcur        equ rw_arn + 1      ; byte: rw_afixup's row...
rw_arleft       equ rw_arcur + 1    ; byte: ...and rows to go
rw_acode        equ rw_arleft + 1   ; byte: the first image's row (after
                                    ;   the relocations' and the list's)
rw_bssend       equ rw_acode + 1
%else
rw_bssend       equ rw_phist + 16
%endif
RW_BSS          equ rw_bssend - rw_win   ; derived: a sum by hand drifted
RW_FBUF         equ 5760            ; rdg_render's frame, at [rw_fseg]:0
RW_PBUF         equ 11520           ; the frame as the screen takes it: the
                                    ;   dither's band (20 a line) or BLITP's
                                    ;   four planes (80), at [rw_pseg]:0

; =============================================================================
; rw_entry - the loader calls this once; the window appears afterwards
; =============================================================================
rw_entry:
%ifdef RDG_AOT
%ifndef PKG_MEGA
    call OSAPI_CPU_INFO             ; NOT ON kern_mega (REALDMG-SPEC 20.14):
    test ah, CPU_F_PM               ;   every image, the list, the
    jz .rmode                       ;   relocations and the doors hold DATA
    mov si, rw_s_pm                 ;   segments, and the loader adds to
    call rw_toast                   ;   them - a selector each, there. A
    stc                             ;   refusal before anything is claimed
    ret                             ;   or read; `gbaot.py --mega` makes the
.rmode:                             ;   package that runs there (20.15)
%endif
    mov byte [rw_jitmode], 2        ; no recompiler: the code is the file's,
%else                               ;   and what it lacks is interpreted
    OS88_MEGA_REFUSE                ; (protected mode: the arm's, 10.3.2)
    mov byte [rw_jitmode], 1        ; Recompiler: On (REALDMG-SPEC 16.7)
%endif
    call rw_cfgload
    call rw_spdset                  ; (Speed: 100% until a game says)
    call OSAPI_FILE_HERE            ; where the program is: the log's folder
    mov [rw_pkgdir], dx
    mov [rw_pkgvol], bl
    push es                         ; op_load spends ES, and OSAPI_ARG_FILE
    call op_load                    ;   reads through the kernel's
    pop es
    jc .refused                     ; no room for the core and view: it said
%ifdef RDG_AOT
    call rw_ameta                   ; the meta, checked and banked
    jc .refused                     ;   (it said)
%endif
    mov ax, (RW_PBUF + RW_FBUF + 1023) / 1024 ; the frame as the screen
    call OSAPI_MEM_CLAIM            ;   takes it, and rdg_render's after it
    jc .noroom
    mov [rw_pseg], dx
%ifdef PKG_MEGA
    mov ax, RW_PBUF / 16            ; a selector under CPU_F_PM, the sum
    call rw_sub                     ;   elsewhere (10.3.2)
    jc .noroom                      ;   (no LDT entry: the claim goes with us)
%else
    add dx, RW_PBUF / 16
%endif
    mov [rw_fseg], dx
    call rw_mfree                   ; ...and it moves (rwmove.inc)
    nop                             ; (PARITY: the play path at its old
                                    ;   alignment - os88rdiff's --hot)
    mov al, KSC_UP                  ; arm the key map (the first call only
    call OSAPI_KEY_DOWN             ;   arms it)
    OS88_ALTENTER_ARM               ; and Alt, for Alt+Enter
    call OSAPI_ARG_FILE             ; CF = 1: launched empty
    jc .nodoc
    mov di, rw_name
    mov cx, 13
.cpn:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    or al, al
    jz .cpd
    loop .cpn
.cpd:
    mov byte [rw_name+13], 0
    mov [rw_argdir], dx
    mov [rw_argvol], bl
    mov byte [rw_argpend], 1
%ifdef RDG_AOT
    jmp short .doc
.nodoc:
    call rw_aname                   ; the game it was made from, beside it
.doc:
%else
.nodoc:
%endif
%ifdef PKG_MEGA
    call OSAPI_CPU_INFO             ; kern_mega's arm: under CPU_F_PM the
    and ah, CPU_F_PM                ;   cartridge's banks are addressed in
    mov [rw_pm], ah                 ;   place above 1MB (rwbank.inc,
    jz .real                        ;   REALDMG-SPEC 10.3.1) - and it is
    mov word [rw_tpl+WT_TITLE], rw_unttl  ;   not real mode any more, so the
.real:                              ;   title bar owns up: (Un)RealDMG
%endif
    mov si, rw_tpl
    call OSAPI_WM_CREATE            ; BX = the window
    jc .refused
    mov [rw_win], bx
    mov ax, rw_onclose              ; the save RAM on the way out
    call OSAPI_WM_ONCLOSE
    mov al, 1
    call OSAPI_WM_SNAP              ; content on the byte grid, for BLITP
    call OSAPI_WM_OWNBG             ; we paint every pixel of it
    OS88_REGION_MOVABLE rw_reloc
    mov [rw_dsel], ds               ; (our data, for code entered with DS
                                    ;   not ours: rw_dscanhk)
    mov si, rw_menus
    call OSAPI_MENU_SET
    mov si, rw_about
    call OSAPI_ABOUT_SET
    mov ax, rw_onwake
    call OSAPI_WM_ONWAKE
    cmp byte [rw_argpend], 0
    je .ok
    call OSAPI_WM_WAKE              ; the document, once the window is up
.ok:
    clc
    ret
.noroom:
    mov si, rw_s_noram
    call rw_toast
.refused:
    stc
    ret

; rw_reloc - OUR REGION MOVED (SPEC.md 66.6.1): BX = where it was, DX = where
; it is. rdgsys's own words moved with it; the state block in the view, which
; did not, names our code segment in six vectors - by the selector a far
; jump loads, which on kern_mega is the region's code alias and not BX or DX
; (rw_csel, REALDMG-SPEC 16.2.4). The worker is parked at OSAPI_TASK_ALIVE
; whenever this runs, never in the core.
rw_reloc:
    mov [rw_dsel], dx
    push ax
    push bx
    push dx
    push ds
    mov ax, bx                      ; the old code selector and the new
    call rw_csel
    mov bx, ax
    mov ax, dx
    call rw_csel
    mov dx, ax
    mov ax, [cs:rdg_viewseg]
    or ax, ax
    jz .r
    mov ds, ax
    cmp [S_EVVEC+2], bx
    jne .1
    mov [S_EVVEC+2], dx
.1:
    cmp [S_IORVEC+2], bx
    jne .2
    mov [S_IORVEC+2], dx
.2:
    cmp [S_IOWVEC+2], bx
    jne .3
    mov [S_IOWVEC+2], dx
.3:
    cmp [S_MBCVEC+2], bx
    jne .4
    mov [S_MBCVEC+2], dx
.4:
    cmp [S_RXVEC+2], bx             ; (rdg_rx, the lazy bank's fetch:
    jne .4x                         ;   REALDMG-SPEC 10.3.2)
    mov [S_RXVEC+2], dx
.4x:
    cmp [S_TRACEVEC+2], bx
    jne .5
    mov [S_TRACEVEC+2], dx
.5:
    cmp [S_JITVEC+2], bx            ; (rdg_jit: in the recompiler's claim
                                    ;   now, 16.9, so never the region's)
    jne .6
    mov [S_JITVEC+2], dx
.6:
%ifdef RDG_AOT
    cmp [S_AOTVEC+2], bx            ; (rdg_aotmiss, ours)
    jne .7
    mov [S_AOTVEC+2], dx
.7:
    cmp [S_HYLVEC+2], bx            ; (rdg_aotlink, ours: 20.16)
    jne .r
    mov [S_HYLVEC+2], dx
%endif
.r:
    pop ds
    pop dx
    pop bx
    pop ax
    ret

; rdgsys asks its host for this: the link port's bytes go nowhere
rdg_putc:
    ret

; rw_csel (rdgsys's rdg_csel, RDG_CSEL) - AX = a segment of ours holding
; code: AX = the selector a far jump to it loads (REALDMG-SPEC 16.2.4) - the
; segment itself on a real-mode kernel, its code alias on kern_mega, where a
; far jump through the data segment is a #GP the kernel completes, one per
; transfer: every entry into the core, every chain between the recompiler's
; segments. 0 stays 0. Every other register kept
; - in kern_mega's ARM alone: the shipped build refuses under CPU_F_PM
; (OS88_MEGA_REFUSE, 10.3.2), so on every kernel it runs on the segment IS
; what a far jump loads, and AX comes back as it went in
rdg_csel:
rw_csel:
%ifdef PKG_MEGA
    push dx
    mov dx, ax
    call OSAPI_MEM_CODESEL          ; (a claim's pair, or a SEG_SUBC one:
    jnc .r                          ;   its code half, 10.3.2)
    mov ax, dx                      ; none: a LONE data selector - a claim
    or ax, ax                       ;   OS88_HIGHMEM put in the pool, cache
    jz .r                           ;   B (16.2.6) - so a pair over it is
    push bx                         ;   asked for, its code half 8 below
    push cx                         ;   the data half the answer is; refused
    mov bl, SEG_SUBC                ;   (no LDT entry, real mode) the
    xor cx, cx                      ;   segment itself, a #GP the kernel
    push ax                         ;   completes per jump
    xor ax, ax
    call OSAPI_SEG
    pop ax
    jc .s
    mov ax, dx
    sub ax, 8
.s:
    pop cx
    pop bx
.r:
    pop dx
%else
    nop                             ; (PARITY: what follows at its old
%endif                              ;   alignment - os88rdiff's --hot)
    ret

%ifdef PKG_MEGA
; rw_sub / rw_subc - kern_mega's arm: DX = a claim of ours (or a selector in
; one), AX = paragraphs into it: DX = "the claim + AX" - OSAPI_SEG's SEG_SUB,
; or SEG_SUBC's pair (its data half; rw_csel answers the code half 8 below)
; for code. A real-mode kernel answers the sum, so one path serves both;
; under CPU_F_PM the selector moves with the claim and goes with it (MC7,
; REALDMG-SPEC 10.3.4). CF = 1 no LDT entry was free, DX kept. Every other
; register kept
rw_subc:
    push bx
    mov bl, SEG_SUBC
    jmp short rw_sub.s
rw_sub:
    push bx
    mov bl, SEG_SUB
.s:
    push ax
    push cx
    xor cx, cx                      ; to its end, 64KB at most
    call OSAPI_SEG                  ; (CF = 1: AX the reason, popped)
    pop cx
    pop ax
    pop bx
    ret
%endif

rw_dsel         dw 0                ; our DATA segment, for code entered
                                    ;   with DS somebody else's (rw_reloc)
%ifdef PKG_MEGA
rw_pm           db 0                ; CPU_F_PM at entry: kern_mega's arm
rw_xsel         dw 0                ; the banks in place: the first window
rw_jtsel        dw 0                ; the block table's selector, or 0 (rw_jtx)
rw_jtsub        dw 0                ; ...or in the claim under CPU_F_PM
                                    ;   (OSAPI_XMEM_RUN, REALDMG-SPEC 10.3.1)
%endif

; =============================================================================
; Loading a game
; =============================================================================

%macro RW_JSEGS 0                   ; (the arm's rw_jsegs; the shipped
%ifdef PKG_MEGA                     ;   build's sum, as it was)
    call rw_jsegs
%else
    add bx, dx
%endif
%endmacro

; rw_onwake - on the UI task, no lock held: a document we were launched with,
; or the core having stopped
rw_onwake:
    call rw_bserve                  ; a bank the worker waits for
    cmp byte [rw_spdq], 0           ; F4 changed Speed: kept here, where a
    je .nf4                         ;   file may be written
    mov byte [rw_spdq], 0
    call rw_cfgsave
.nf4:
    mov al, [rw_f3q]                ; F3 in the window, to a mode that
    or al, al                       ;   wants the UI task first: what it
    jz .nf3                         ;   needs here, then the switch the
    mov byte [rw_f3q], 0            ;   worker's (rw_jitq), said as F3's
    call rw_jitprep                 ;   own is
    jc .nf3
    mov byte [rw_jitqn], 1
    mov [rw_jitq], al
.nf3:
    call rw_jfree                   ; the interpreter's: the cache back
    cmp byte [rw_lflq], 0           ; F12 in the window: the log written
    je .nfl                         ;   here, where a file may be
    mov byte [rw_lflq], 0
    call rw_logflush
.nfl:
    call rw_rpserve                 ; a replay written, or its end said
    cmp byte [rw_argpend], 0
    je .stopped
    mov byte [rw_argpend], 0
    call OSAPI_GFX_LOCK
    mov dx, [rw_argdir]
    mov bl, [rw_argvol]
    call OSAPI_FILE_GOTO_QM
    jc .unlock
    mov si, [rw_win]
    call rw_load
.unlock:
    call OSAPI_GFX_UNLOCK
    ret
.stopped:
    cmp byte [rw_state], 2
    jne .r
    call OSAPI_GFX_LOCK
    mov si, [rw_win]
    call rw_repaint
    call OSAPI_GFX_UNLOCK
.r:
    ret

; rw_open - File > Open: the Standard File dialog, then rw_opened
rw_open:
    cmp byte [rw_rpmode], 0         ; (a replay is one game's: it ends
    jne .rpb                        ;   first)
    mov al, FDLG_OPEN
    mov bx, si
    mov di, rw_opened
    xor si, si
    call OSAPI_FILE_DLG
    ret
.rpb:
    mov si, rw_s_rpbusy
    jmp rw_note

; rw_opened - the dialog's completion: DI = the name, DX:CX = its size
rw_opened:
%ifdef RDG_AOT
    mov byte [rw_aemb], 0           ; (Load Game: a file, whatever is inside)
%endif
    push si
    mov si, di                      ; copy the kernel's name now
    mov di, rw_name
    mov bx, 13
.c:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    or al, al
    jz .cd
    dec bx
    jnz .c
.cd:
    mov byte [rw_name+13], 0
    pop si
    call rw_unload                  ; the game there was, if one: its place
    ; (the size the dialog read out of the listing is not used: rw_load asks
    ; the file itself, which knows its size unpacked)
; rw_load - rw_name in the current folder, SI = our window, the gfx lock
; held. THE SIZE PROBE first - a read with no room answers FERR_BIG with the
; KB the read needs, from the directory and unpacked, with some slack for a
; compressed file's expansion - then a claim that size, the read, and the
; check on the bytes that actually came in: whole 16KB banks, 32 to 256KB.
; EVERY CLAIM OF OURS IS PINNED FOR THE LOAD (rwmove.inc): it claims and reads
; with segments in registers, and declares them all again at its end.
rw_load:
    inc byte [rw_mhold]
    call rw_mpin
    call rw_load0
    dec byte [rw_mhold]
    jmp rw_mfree
rw_load0:
    push si
%ifdef RDG_AOT
    cmp byte [rw_aemb], 0           ; THE ROM IN THE PACKAGE (REALDMG-SPEC
    je .file                        ;   20.13): its size is the meta's, and
    mov dx, [rw_aromkb]             ;   no file is asked
    jmp short .sized
.file:
%endif
    push ds
    pop es
    xor bx, bx
    xor cx, cx
    xor dx, dx
    mov si, rw_name
    call OSAPI_FILE_READ
    jnc .notrom                     ; an empty file
    cmp ax, FERR_BIG
    jne .rdfail0
    cmp dx, 32
    jb .notrom
.sized:
    mov [rw_romkb], dx
%ifdef RDG_AOT
%ifdef PKG_MEGA
    cmp byte [rw_pm], 0             ; KERN_MEGA'S ARM UNDER CPU_F_PM (REALDMG-
    je .aheap                       ;   SPEC 20.15): the ROM first - in place
    call rw_dmgclaim                ;   above 1MB, or one claim - and every
    call rw_aloose                  ;   image after it, in one pool block
    mov byte [rw_slotmin], RW_SLOTWANT ;   (rw_aclaim at .loaded): there is
    call rw_plan                    ;   no bank cache to trade them against
    jnc .planned                    ;   here (10.3.2), and the images are
    call rw_atight                  ;   what can go short, interpreted
    jmp .say
.aheap:
%endif
    call rw_dmgclaim                ; the damage memory and EVERY code
    call rw_aloose                  ;   (our claims movable for the plan)
    mov al, [rw_ans]                ;   image FIRST, the hot ones leading
    call rw_aclaim                  ;   (the meta's: the ones the counters
    mov byte [rw_slotmin], RW_SLOTWANT ;   ran in), then the ROM in what is
.aplan:                             ;   left. A slot short is a bank
    call rw_plan                    ;   fetched off XMS every time the game
    jnc .planned                    ;   swings to it - Kirby with 4 images
    mov al, [rw_anseg]              ;   beside 5 slots fetched 130 banks a
    cmp al, [rw_ahot]               ;   second on the owner's Victory - so
    jbe .aslot                      ;   cold images go back until eight
    call rw_adrop                   ;   slots fit; and a slot more than
    jmp short .aplan                ;   eight is worth less than an image,
.aslot:                             ;   whose code is otherwise
    cmp byte [rw_slotmin], 4        ;   INTERPRETED: the plan took every
    je .adrop                       ;   free KB for slots once, and Looney
    mov byte [rw_slotmin], 4        ;   Tunes kept 1 image of 8 beside 9
    jmp short .aplan                ;   slots it fetched 6 banks into.
.adrop:                             ;   Then four slots, then a hot image
    call rw_adrop                   ;   given back (REALDMG-SPEC 20.10)
    jnc .aplan
    call rw_atight
    jmp .say
%endif
    call rw_dmgclaim                ; the damage memory and the recompiler's
    call rw_jitclaim                ;   cache FIRST: what the ROM's read
    call rw_jit2claim               ;   rebuilds of the disk caches lands in
    mov byte [rw_slotmin], RW_SLOTWANT ;   what is left, and took the 64KB on
    call rw_plan                    ;   the second field run's Victory - but
    jnc .planned                    ;   a cartridge in the bank cache wants
                                    ;   RW_SLOTWANT slots before the
                                    ;   recompiler keeps its second segment
                                    ;   or its big table: Kirby's Dream Land
                                    ;   on 5 fetched ~95 banks a second off
                                    ;   XMS on the Victory, at 15% (REALDMG-
                                    ;   SPEC 10.3)
    cmp word [rw_jit2seg], 0        ; no room with the cache's second
    je .nojit2                      ;   segment: the ROM without it first
    mov dx, [rw_jit2seg]
    call OSAPI_MEM_FREE
    mov word [rw_jit2seg], 0
    call rw_plan
    jnc .planned
.nojit2:
    cmp word [rw_jitseg], 0         ; (none to give up: four, and no more
    je .nojit                       ;   asked for)
    cmp byte [rw_hmask], 7          ; ...then with the table at 16KB
    je .min4
.jit16:
    mov dx, [rw_jitseg]
    call OSAPI_MEM_FREE
    mov word [rw_jitseg], 0
    call rw_jpkb                    ; the claim WITH the recompiler's part,
    add ax, 64 + 16                 ;   read again into its new tail: the
    mov byte [rw_hmask], 7          ;   64 + 16 this was left the part past
    call OSAPI_MEM_CLAIM            ;   the claim's end and its pointers at
    jc .nojit                       ;   the old place, and the slots were
    mov bx, 1000h + 400h            ;   claimed over it - a 1MB cartridge
    RW_JSEGS                        ;   hung on its first frame, or ran the
    call rw_jpart                   ;   stack over (rw_jitget's fallback)
    jnc .jp16
    call OSAPI_MEM_FREE
    jmp short .nojit
.jp16:
    mov [rw_jitseg], dx
    call rw_plan
    jnc .planned
.min4:
    mov byte [rw_slotmin], 4        ; (fewer slots before no recompiler)
    call rw_plan
    jnc .planned
.nobig:
    cmp word [rdg_dmgseg], 0        ; no room with it: the damage memory
    je .nobig1                      ;   goes back BEFORE the recompiler, as
    mov dx, [rdg_dmgseg]            ;   it does for the ROM's claim (.claim)
    call OSAPI_MEM_FREE             ;   - .loaded claims it again if there
    mov word [rdg_dmgseg], 0        ;   is room - and the recompiler is
    jmp short .jit16                ;   claimed again over the place it left
.nobig1:
    mov dx, [rw_jitseg]             ; no room with it: the ROM without it
    call OSAPI_MEM_FREE
    mov word [rw_jitseg], 0
.nojit:
    mov byte [rw_jitwant], 0
    mov byte [rw_slotmin], 4
    call rw_plan
    jc .say
.planned:
    cmp byte [rw_bcache], 0
    je .one
    call rw_bload                   ; (rw_atight, after its slots' claim)
    jc .say
    jmp .loaded
.one:
    mov ax, [rw_romkb]              ; whole 16KB banks first: the probe answers
    and ax, 0FFF0h                  ;   a KB over an exact size (SPEC.md
    jnz .claim                      ;   20.14.6.3), and on a 1MB 286 Kirby's
    mov ax, [rw_romkb]              ;   Dream Land fits by exactly that KB
.claim:
    push ax
    call OSAPI_MEM_CLAIM            ; DX = its segment
    pop ax
    jnc .got
    cmp word [rdg_dmgseg], 0         ; no room: the damage memory, claimed
    je .room                        ;   before the plan for the recompiler's
    mov dx, [rdg_dmgseg]             ;   sake, goes back - .loaded claims it
    call OSAPI_MEM_FREE             ;   again if there is room after the ROM,
    mov word [rdg_dmgseg], 0         ;   and rdg_renderd composes whole frames
    jmp short .claim                ;   without it
.got:
%ifdef RDG_AOT
    call rw_atight                  ; (the claim compacted what it had to:
%endif                              ;   rw_bload's order, rw_aloose's reason)
    mov [rw_romseg], dx
%ifdef RDG_AOT
    cmp byte [rw_aemb], 0
    je .gotf
    call rw_aromrd                  ; out of the package: DX:AX = the bytes
    jc .rdfail
    jmp .read
.gotf:
%endif
    mov es, dx
    xor bx, bx
    push ax
    mov cx, 1024
    mul cx
    mov cx, ax                      ; DX:CX = the claim's capacity
    mov si, rw_name
    call OSAPI_FILE_READ            ; DX:AX = the bytes read
    pop bx                          ; (BX = the KB claimed)
    jnc .read
    cmp ax, FERR_BIG                ; the exact banks were short - a packed
    jne .rdfail                     ;   file expands in place - so the
    cmp bx, [rw_romkb]              ;   probe's figure after all, once
    jae .rdfail
    mov dx, [rw_romseg]
    call OSAPI_MEM_FREE
    mov ax, [rw_romkb]
    jmp short .claim
.read:
    ; the ROM itself: whole 16KB banks, 2 to 16 of them
    test ax, 3FFFh
    jnz .notrom1
    mov cx, 14
.sh:
    shr dx, 1
    rcr ax, 1
    loop .sh                        ; AX = banks
    cmp ax, 2
    jb .notrom1
    cmp ax, RW_ROMMAX_KB / 16
    ja .notrom1
    mov [rw_banks], ax
    push es
    mov es, [rw_romseg]
    call rw_hdrchk                  ; SI = why not
    pop es
    jc .refuse1
.loaded:
    call rw_jitclaim
    call rw_dmgclaim
    call rw_ramsetup
    call rw_prfload
%ifdef RDG_AOT
    mov al, [rw_ans]                ; the cold images, in what the ROM and
    call rw_aclaim                  ;   the cartridge's RAM left
    call rw_aread                   ; the images read and put right, once
%endif
%ifdef PKG_MEGA
    call rw_jitxget                 ; (the extra tiers: what is left, 16.2.5)
%endif
    call rw_start
    call rw_spdgame                 ; its Speed and Sound, if it has them
    call rw_fmopen                  ; the FM chip, if there is one for us
    mov byte [rw_state], 1
    call OSAPI_GET_TICKS            ; the window's notice first (14.3.1):
    mov [rw_ntct], ax               ;   the worker starts the game after it
    mov byte [rw_ntc], 1
    pop si
    call rw_hire
    call rw_repaint
    ret
.refuse1:
    push si
    mov dx, [rw_romseg]
    call OSAPI_MEM_FREE
    pop si
    jmp short .say
.notrom1:
    mov dx, [rw_romseg]             ; the claim back
    call OSAPI_MEM_FREE
.notrom:
    mov si, rw_s_notrom
    jmp short .say
.rdfail:
    mov dx, [rw_romseg]
    call OSAPI_MEM_FREE
.rdfail0:
%ifdef RDG_AOT
    call rw_anoname                 ; (a precompiled cartridge: the file it
    jmp short .say                  ;   opens, named - REALDMG-SPEC 20.10)
%else
    mov si, rw_s_read
    jmp short .say
%endif
.room:
%ifdef RDG_AOT
    call rw_atight                  ; (the hold back: .one's claim failed)
%endif
    mov si, rw_s_room
.say:
    pop bx                          ; (the window, banked)
    jmp rw_toast

; rw_hdrchk - ES = the cartridge's bank 0: CF = 1 and SI = why, for a file
; that is not a Game Boy ROM (its header checksum, 014Dh, the boot ROM's own
; test), a game for the Game Boy Color alone (0143h = C0h), or a cartridge
; whose MBC rdgsys does not have - which would run with its banks wrong and
; say nothing (REALDMG-SPEC section 10)
rw_hdrchk:
    push ax
    push bx
    push cx
    mov si, 0134h                   ; x = x - byte - 1, 0134h-014Ch
    xor ah, ah
    mov cx, 014Dh - 0134h
.ck:
    mov al, [es:si]
    sub ah, al
    dec ah
    inc si
    loop .ck
    cmp ah, [es:014Dh]
    mov si, rw_s_notrom
    jne .no
    cmp byte [es:0143h], 0C0h
    mov si, rw_s_cgb
    je .no
%ifdef RDG_AOT
    call rw_ahdr                    ; the game this was made from (SI = why)
    jne .no
%endif
    mov bl, [es:0147h]              ; the types rdgsys runs (rdgsys rdg_init):
    cmp bl, 20h                     ;   a bit each in rw_mbcok
    jae .mbc
    mov cl, bl
    and cl, 0Fh
    mov ax, 1
    shl ax, cl
    mov si, rw_mbcok
    test bl, 10h
    jz .lo
    inc si
    inc si
.lo:
    test ax, [si]
    jnz .ok
.mbc:
    push di                         ; 'REALDMG: MBC xxh unknown'
    push es
    push ds
    pop es
    mov si, rw_s_mbc
    mov di, rw_stmsg
    mov cx, rw_s_mbcx - rw_s_mbc
    rep movsb
    mov al, bl
    mov cl, 4
    shr al, cl
    call .hex
    mov al, bl
    call .hex
    mov si, rw_s_mbcx
.t:
    lodsb
    stosb
    or al, al
    jnz .t
    pop es
    pop di
    mov si, rw_stmsg
.no:
    stc
    jmp short .r
.ok:
    clc
.r:
    pop cx
    pop bx
    pop ax
    ret
.hex:
    and al, 0Fh
    add al, '0'
    cmp al, '9'
    jbe .h
    add al, 'A' - '9' - 1
.h:
    stosb
    ret
rw_mbcok:       dw 836Fh, 7E0Fh     ; 00-03 05 06 08 09 0F, 10-13 19-1E

; rw_start - rdg_init: the view, the core, the ROM, its banks - and the
; recompiler over them when it has its cache (REALDMG-SPEC section 16)
rw_start:
    call rw_rpdrop                  ; (a replay's machine is gone)
    mov al, 2                       ; the view
    call op_seg
    push ax
    xor al, al                      ; the core, both halves from here
%ifdef PKG_MEGA
    call op_cseg                    ; (a pair under CPU_F_PM: its data half
    cmp byte [rw_pm], 0             ;   8 above, and rw_csel's answer 8
    je .cs                          ;   below that, 10.3.2)
    add ax, 8
.cs:
    call rw_b0set                   ; bank 0's window (rwbank.inc)
%else
    call op_seg
%endif
    mov bx, ax
    pop ax
    mov cx, [rw_romseg]
    mov dx, [rw_banks]
    call rdg_init
    call rw_ramattach
    call rw_dmgfull
    mov ax, [rw_prfseg]             ; the profile, recorded from here on
    push ds
    mov ds, [cs:rdg_viewseg]
    mov [S_PRFSEG], ax
    mov word [S_PRFMASK], RW_PRFN - 1
    pop ds
rw_jiton:                           ; (and Machine > Recompiler's way back in)
    push ds
    push bx
    mov ds, [cs:rdg_viewseg]
%ifdef RDG_AOT
    call rw_aoton                   ; the precompiled code, always
    pop bx
    pop ds
    ret
%endif
    cmp word [cs:rw_jitseg], 0      ; no cache or not wanted: the plain
    je .off                         ;   interpreter, hooks and all removed
    cmp byte [cs:rw_jitwant], 0
    je .off
    call rw_rtinit
    cmp byte [cs:rw_jitmode], 3     ; Static: the profile made now
    jne .dyn
    call far [cs:rw_jpre]
    mov [cs:rw_npre], ax
.dyn:
    pop bx
    pop ds
    ret
.off:
    call rw_jdoff
    pop bx
    pop ds
    ret

%ifdef PKG_MEGA
; rw_jsegs - DX = the recompiler's new claim, BX = paragraphs to its part:
; BX = the part's segment - the sum, or under kern_mega's protected mode a
; SEG_SUBC pair over it (rw_subc), and the block table's selector at 1000h
; banked beside it for rw_jitsegs (REALDMG-SPEC 10.3.4). A pair refused (no
; LDT entry) is a part that cannot be read: BX = 0, and rw_jpart says no.
; Every register but BX kept
rw_jsegs:
    push ax
    push dx
    mov ax, bx
    call rw_subc                    ; DX = the part's (the sum, real mode)
    mov bx, dx
    jnc .t
    xor bx, bx
.t:
    pop dx
    push dx
    mov word [rw_jtsub], 0
    cmp byte [rw_pm], 0             ; (real mode: rw_jitsegs' own sum, which
    je .x                           ;   a move puts right)
    mov ax, 1000h                   ; the table's, after the pair so a pair
    call rw_sub                     ;   at the same base is not asked twice
    jc .nt
    mov [rw_jtsub], dx
    jmp short .x
.nt:
    xor bx, bx                      ; (none: no recompiler either)
.x:
    pop dx
    pop ax
    ret
%endif

; rw_jitsegs - AX = the recompiler's cache, BX = its block table: the 16KB
; after it in the one claim (rw_jitclaim); CX = its second 64KB, or 0; DL
; = the table's mask
rw_jitsegs:
    mov ax, [cs:rw_jitseg]
    mov bx, ax
    add bh, 10h
%ifdef PKG_MEGA
    cmp word [cs:rw_jtsel], 0       ; (the arm's table, above 1MB)
    je .t
    mov bx, [cs:rw_jtsel]
.t:
    cmp word [cs:rw_jtsub], 0       ; (...or in the claim, a selector under
    je .c                           ;   CPU_F_PM: rw_jsegs)
    cmp word [cs:rw_jtsel], 0
    jne .c
    mov bx, [cs:rw_jtsub]
.c:
%endif
    mov cx, [cs:rw_jit2seg]
    mov dl, [cs:rw_hmask]
    ret

; rw_rtinit - DS = the view: rt_init over the package's claims, every
; register kept
rw_rtinit:
    push ax
    push bx
    push cx
    push dx
    call rw_jitsegs
    push ax
    call rw_csel                    ; the two cache segments' code
    mov [S_JITCSA], ax              ;   selectors (REALDMG-SPEC 16.2.4)
    mov ax, cx
    call rw_csel
    mov [S_JITCSB], ax
    pop ax
%ifdef RT_SQUEEZE                   ; (tests/rdgmega.py's squeezed arm only,
    mov word [S_JITLIM], RW_SQZLIM  ;   never shipped: every tier ~0.5KB, so
%endif                              ;   a test ROM recycles round them all)
    call far [cs:rw_jinit]
%ifdef PKG_MEGA
    push si                         ; ...and the extra tiers (16.2.5)
    mov si, rw_jitx
.x:
    mov ax, [cs:si]
    or ax, ax
    jz .xn
    call rw_csel
    mov dx, ax
    mov ax, [cs:si]
    call far [cs:rw_jmore]
.xn:
    inc si
    inc si
    cmp si, rw_jitx + RT_XMAX * 2
    jb .x
    pop si
%endif
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; rw_jitback - the recompiler on again, between two core runs: over the
; cache it had, or from nothing when there is none since rdg_init
rw_jitback:
    cmp word [cs:rw_jitseg], 0
    je .r
    push ds
    mov ds, [cs:rdg_viewseg]
    cmp byte [S_JITON], 0
    jne .x
    cmp word [S_JITSEG], 0
    je .init
    call far [cs:rw_jres]
.x:
    pop ds
.r:
    ret
.init:
    push bx
    call rw_rtinit
    pop bx
    pop ds
    ret

; rw_jitset - CPU > the recompiler's mode, between two core runs:
; Interpreter is the interpreter alone; Recompiler the dynamic recompiler;
; Static the preloaded one
rw_jitset:
    mov byte [rw_jitwant], 1        ; (rw_jiton's, at a reset)
    cmp byte [rw_jitmode], 3
    je rw_static
    push ds                         ; anything but Static: the table is the
    mov ds, [cs:rdg_viewseg]         ;   dynamic recompiler's again, over
    mov byte [S_JITFIX], 0          ;   what a preload left in it
    mov byte [S_JITPRE], 0
    pop ds
    cmp byte [rw_jitmode], 2
    jne .on
    mov byte [rw_jitwant], 0
    mov al, RLE_JITOFF
    call rw_logevt
    call rw_jitoff
    mov byte [rw_jfreeq], 1         ; the cache back, on the UI task
    push bx                         ;   (rw_jfree) - after the bracket, if
    mov bx, [rw_win]                ;   this is in it
    call OSAPI_WM_WAKE
    pop bx
    ret
.on:
    mov al, RLE_JITON
    call rw_logevt
    jmp rw_jitback

; rw_static - Machine > Recompiler: Static (REALDMG-SPEC 16.8), between two
; core runs: the cache flushed, the profile - everything translated in
; this game's sessions so far, this one's included - translated into it
; heaviest first, and the block table fixed
rw_static:
    cmp word [rw_jitseg], 0         ; (no cache, so no recompiler's part)
    je .r
    mov al, RLE_JITON
    call rw_logevt
    push ds
    push bx
    mov ds, [cs:rdg_viewseg]
    call rw_rtinit
    call far [cs:rw_jpre]
    mov [cs:rw_npre], ax
    pop bx
    pop ds
    ret
.r:
    ret

; rw_jitoff - the interpreter alone, between two core runs (the worker's)
rw_jitoff:
    push ds
    mov ds, [cs:rdg_viewseg]
    mov byte [S_JITON], 0
    mov word [S_RESVEC], RDGC_RDG_DISPATCH
    call rw_jdoff
    pop ds
    ret

; rw_jitclaim - on the UI task, once: a 64KB cache and the 16KB of its block
; table after it (REALDMG-SPEC 16.3), on every CPU - an 8088 runs a busy
; M-cycle in 21.7 clocks translated against 51 interpreted (REALDMG-SPEC
; 16.10); without the memory it is simply not used.
rw_jitclaim:
    cmp byte [rw_jitmode], 2        ; (CPU > Interpreter: none - the ROM
    je rw_jitget.r                  ;   has the room, REALDMG-SPEC 16.7)
rw_jitget:                          ; (CF = 1: none, a game running too)
    cmp word [rw_jitseg], 0
    jne .r
%ifdef PKG_MEGA
    call rw_jtx                     ; kern_mega's arm: the table above 1MB,
    jc .conv                        ;   the claim the cache and the part
    call rw_jpkb                    ;   alone (16.2.5)
    add ax, 64
    mov byte [rw_hmask], JIT_HMMAX
    call OSAPI_MEM_CLAIM
    mov bx, 1000h
    jnc .got
    jmp short .r
.conv:
%endif
    call rw_jpkb                    ; AX = the recompiler's own KB, its part
    push ax                         ;   read into the claim's tail (16.9)
    add ax, 64 + JIT_HASHN * 8 / 1024 ; the cache and its block table: 64KB
    mov byte [rw_hmask], JIT_HMMAX  ;   of table, a quarter of the slot
    call OSAPI_MEM_CLAIM            ;   fights 16KB's has (REALDMG-SPEC
    pop ax                          ;   16.3: Kirby's Dream Land 330 -> 266
    mov bx, 1000h + JIT_HASHN / 2   ;   ticks, the CPU alone) - or 16KB
    jnc .got
    add ax, 64 + 16
    mov byte [rw_hmask], 7
    call OSAPI_MEM_CLAIM            ; DX = its segment (the state block
    mov bx, 1000h + 400h            ;   names it: rw_mreloc, rwmove.inc)
    jc .r
.got:
    RW_JSEGS                        ; BX = the part's place, past the table
    call rw_jpart
    jc .nopart
    mov [rw_jitseg], dx
    mov byte [rw_jitwant], 1
.r:
    clc
    cmp word [rw_jitseg], 0
    jne .x
    stc
.x:
    ret
.nopart:                            ; the part could not be read: no
    call OSAPI_MEM_FREE             ;   recompiler, as with no memory
    jmp short .r

%ifdef PKG_MEGA
; rw_jtx - kern_mega's arm: the recompiler's 64KB block table in a block of
; the extended pool, [rw_jtsel] the data selector it is read through
; (OSAPI_XMEM_RUN's first: the 64KB window at its start) - the table is
; only ever data, ES at offset 0 (REALDMG-SPEC 16.2.5). Kept once made: a
; cache claimed again takes the same table. CF = 1: real mode or no room,
; and the table goes in the claim as before. Keeps every register but AX
rw_jtx:
    cmp byte [rw_pm], 0
    je .no
    cmp word [rw_jtsel], 0
    jne .yes
    push cx
    push dx
    xor ax, ax                      ; DX:AX = 64KB
    mov dx, 1
    call OSAPI_XMEM_ALLOC
    jc .nox
    push ax
    push dx
    call OSAPI_XMEM_RUN             ; DX = the first selector
    jc .nor
    mov [rw_jtsel], dx
    pop dx
    pop ax
    pop dx
    pop cx
.yes:
    clc
    ret
.nor:
    pop dx
    pop ax
    call OSAPI_XMEM_FREE
.nox:
    pop dx
    pop cx
.no:
    stc
    ret
%endif

; rw_jpkb - AX = the KB the recompiler's part takes in its claim: its
; expanded length, and a sector over for the stream read beneath its end
rw_jpkb:
    mov ax, [op_table + OP_T_ROWS + RWJ_PART * OP_ROW + OP_R_LEN]
    add ax, 1023 + 512
    mov al, ah
    xor ah, ah
    shr al, 1
    shr al, 1
    ret

; rw_jpart - BX = a segment in the recompiler's claim: the recompiler read
; into it (REALDMG-SPEC 16.9) - part RWJ_PART of this file, OP_SELF's read
; as mra_load's in apps/midirack: the packed stream at the part's own
; sector, R up, expanded down onto BX:0 - and the four far pointers aimed
; at it. CF = 1: it could not be, and nothing points at it. Every register
; but the flags kept
;
; IN REALDMG.O88'S OWN FOLDER (rw_pkgdir, banked at entry): op_name is a bare
; name, and by the time a game is open the instance stands where the GAME is.
; Read here, a game in any other folder than the package's ran on the
; interpreter with the recompiler chosen - the apps disks keep REALDMG.O88 in
; GAMES and a cartridge is the user's, wherever they keep it
rw_jpart:
%ifdef PKG_MEGA
    or bx, bx                       ; (rw_jsegs found no selector for it)
    jnz .go
    stc
    ret
.go:
%endif
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov bp, bx
    mov dx, [rw_pkgdir]             ; where op_name is (rw_fback below)
    mov bl, [rw_pkgvol]
    call rw_fgo
    jc .no
    mov si, op_table + OP_T_ROWS + RWJ_PART * OP_ROW
    mov cx, [si + OP_R_ZKB]         ; CX = the packed bytes
    mov bx, [si + OP_R_LEN]         ; BX = what they expand to
    sub bx, cx                      ; BX = R, a sector boundary: where the
    jbe .no                         ;   stream is read, up from where it
    add bx, 511                     ;   expands to
    and bx, ~511
    mov ax, [si + OP_R_OFF]         ; DX:AX = the part's byte offset
    mov dx, ax
    push cx
    mov cl, 9
    shl ax, cl
    mov cl, 7
    shr dx, cl
    pop cx
    mov es, bp
    push cx
    add cx, 511
    and cx, ~511                    ; whole sectors, the file ending inside
    push bx                         ;   the last one
    mov si, op_name
    call OSAPI_FILE_READ_AT         ; DX:AX = the bytes delivered
    pop si                          ; SI = R
    pop cx
    jc .no
    cmp ax, cx
    jb .no
    mov al, [op_table + OP_T_FMT]
    xor ah, ah
    mov dx, [op_table + OP_T_ROWS + RWJ_PART * OP_ROW + OP_R_LEN]
    xor bx, bx
    xor di, di
    push ds
    mov ds, bp                      ; DS:SI = the stream, ES:0 its output
    call OSAPI_DECOMP
    pop ds
    jc .no
    mov [rw_jpseg], bp
    call rw_jdset
    clc
    jmp short .r
.no:
    stc
.r:
    call rw_fback                   ; (CF kept)
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; rw_jdset - the recompiler's four doors at its part, [rw_jpseg], by the
; selector a far call loads (rw_csel). DS = ours; every register kept
rw_jdset:
    push ax
    mov ax, [rw_jpseg]
    call rw_csel
    mov [rw_jinit + 2], ax
    mov [rw_jres + 2], ax
    mov [rw_jpre + 2], ax
    mov [rw_jdisp + 2], ax
%ifdef PKG_MEGA
    mov [rw_jmore + 2], ax
%endif
    pop ax
    ret

rw_jpseg        dw 0                ; the recompiler's part: its DATA segment
; the recompiler's four doors, far (rdgjitp.asm's entries): DS = the view
rw_jinit        dw 0, 0             ; rt_init
rw_jres         dw 10, 0            ; rt_resume
rw_jpre         dw 20, 0            ; rt_preload, AX = the count
rw_jdisp        dw 30, 0            ; rt_dispatch, AL as its
%ifdef PKG_MEGA
rw_jmore        dw 40, 0            ; rt_more, AX and DX as its (the arm's
%endif                              ;   part is built RDG_XTIER, 16.2.5)

; rw_jdoff - DS = the view: the interpreter's control transfers DISPATCH
; again (rdgjit.inc's rt_dispatch with AL = 0), which an 8088 needs without
; the recompiler's part ever being read: the core is built looking for
; translated code, and the 46 sites (RDGC_JDTAB, rdgtmpl.py's) are each made
; mov dh,[si] / inc si / jmp dx
rw_jdoff:
    push bx
    push cx
    push si
    push es
    mov es, [S_COREDS]              ; the core
    mov si, rw_jdtab
    mov cx, RDGC_JDN
.s:
    mov bx, [cs:si]
    mov word [es:bx], 348Ah
    mov word [es:bx+2], 0FF46h
    mov byte [es:bx+4], 0E2h
    inc si
    inc si
    loop .s
    pop es
    pop si
    pop cx
    pop bx
    ret
; rw_jitto - CPU > a mode, AL, on the UI task: with no game the setting the
; next one starts in; with one, what it needs first (rw_jitprep) and then
; the worker's switch, between two core runs (rw_jitq)
rw_jitto:
%ifdef RDG_AOT
    ret                             ; (one mode: the menu has no others)
%endif
    cmp al, [rw_jitmode]
    je .r
    cmp byte [rw_state], 1
    je .g
    mov [rw_jitmode], al
    jmp rw_mgrey
.g:
    cmp byte [rw_jitq], 0
    jne .r
    call rw_jitprep
    jc .r
    mov [rw_jitq], al
.r:
    ret

; rw_jitprep - AL = a mode about to be switched to, on the UI task: the
; recompiler's cache claimed if there is none (the interpreter gave it
; back), and for Static its profile read (rw_stprep). CF = 1: no room for
; the cache, and it is said. Keeps AL
rw_jitprep:
    cmp al, 2
    je .ok
    push ax
    call rw_jitget
%ifdef PKG_MEGA
    jc .nx
    call rw_jitxget                 ; (and the arm's extra tiers, 16.2.5)
    clc
.nx:
%endif
    pop ax
    jc .room
    cmp al, 3
    jne .ok
    push ax
    call rw_stprep
    pop ax
.ok:
    clc
    jmp rw_mfree                    ; the cache and its second 64KB move
.room:
    push si
    mov si, rw_s_jroom
    call rw_note
    pop si
    stc
    ret

; rw_jfree - on the UI task, after the worker is on the interpreter
; (rw_jitset): the recompiler's cache and its second segment given back -
; CPU > Interpreter is how a 286 user has that memory (REALDMG-SPEC 16.7).
; The worker is kept out of the machine while they go
rw_jfree:
    cmp byte [rw_jfreeq], 0
    je .r
    mov byte [rw_jfreeq], 0
    cmp word [rw_jitseg], 0
    je .r
    mov byte [rw_fsq], 1            ; the worker out of the machine first
.wait:
    cmp byte [rw_incore], 0
    je .go
    call rw_bserve
    call OSAPI_TASK_YIELD
    jmp short .wait
.go:
    cmp byte [rw_jitmode], 2        ; (and still the interpreter: F3 may
    jne .x                          ;   have moved on meanwhile)
    push ds
    mov ds, [cs:rdg_viewseg]
    cmp byte [S_JITON], 0
    jne .xd
    xor ax, ax                      ; the core forgets them: rw_jitback
    mov [S_JITSEG], ax              ;   claims and starts again from
    mov [S_JITSEGA], ax             ;   nothing
    mov [S_JITSEGB], ax
%ifdef PKG_MEGA
    mov [S_JITXN], al               ; (and the extra tiers, freed below)
%endif
    pop ds
%ifdef PKG_MEGA
    call rw_jitxfree
%endif
    xor dx, dx
    xchg dx, [rw_jit2seg]
    or dx, dx
    jz .one
    call OSAPI_MEM_FREE
.one:
    xor dx, dx
    xchg dx, [rw_jitseg]
    call OSAPI_MEM_FREE
    call rw_mgrey
    jmp short .x
.xd:
    pop ds
.x:
    mov byte [rw_fsq], 0
.r:
    ret

; rw_hire - the worker, once (the lock is held: paint, a menu or a dialog)
rw_hire:
    cmp byte [rw_spawned], 0
    jne .r
    mov ax, rw_worker
    mov bx, [rw_win]
    call OSAPI_TASK_SPAWN
    jc .r                           ; refused for now: the next paint asks
    OS88_WORKER_RESTARTABLE rw_worker_rs
    mov byte [rw_spawned], 1
.r:
    ret

; =============================================================================
; Cartridge RAM, its battery and the MBC3 clock (REALDMG-SPEC section 10.1)
; =============================================================================

; rw_ramsetup - after the ROM is read, on the UI task: what the header says
; the cartridge carries, a claim for the banks when there is more than one
; (or a clock, or a battery), and a battery's NAME.SAV from beside the game
rw_ramsetup:
    call OSAPI_FILE_HERE            ; the game's folder: its .SAV's
    mov [rw_romdir], dx
    mov [rw_romvol], bl
    push es
    mov es, [rw_romseg]
    mov al, [es:0147h]              ; the cartridge type
    mov ah, [es:0149h]              ; the RAM size code
    pop es
    xor bx, bx
    mov [rw_ramseg], bx
    mov [rw_rtc], bl
    mov [rw_batt], bl
    mov si, rw_battab               ; a battery?
.bt:
    cmp byte [si], 0
    je .bd
    cmp al, [si]
    je .by
    inc si
    jmp short .bt
.by:
    mov byte [rw_batt], 1
.bd:
    cmp al, 0Fh                     ; MBC3 + timer
    je .rt
    cmp al, 10h
    jne .sz
.rt:
    mov byte [rw_rtc], 1
.sz:
    mov cx, 512                     ; MBC2: 512 nibbles of its own
    mov bl, 1
    cmp al, 05h
    je .have
    cmp al, 06h
    je .have
    xor cx, cx
    xor bl, bl
    cmp ah, 1
    jb .have                        ; none
    mov cx, 2048
    mov bl, 1
    je .have                        ; 2KB
    mov cx, 8192
    cmp ah, 2
    je .have
    mov cx, 32768                   ; 32KB - and 64/128KB kept as 32KB
    mov bl, 4
.have:
    mov [rw_savelen], cx
    mov [rw_rambanks], bl
    cmp bl, 1                       ; one bank and nothing to keep: the
    ja .claim                       ;   window alone holds it
    cmp byte [rw_rtc], 0
    jne .claim
    cmp byte [rw_batt], 0
    je .r
    or bl, bl
    jz .r                           ; a battery and no RAM: nothing to save
.claim:
    mov al, bl
    or al, al
    jnz .k
    inc al
.k:
    mov ah, 8
    mul ah                          ; AX = 8KB a bank
    add al, [rw_rtc]                ; + 1KB for the clock's trailer
    push ax
    call OSAPI_MEM_CLAIM            ; DX = its segment (the state block
    pop ax                          ;   names it: rw_mreloc, rwmove.inc)
    jc .room
    mov [rw_ramseg], dx
    push es                         ; FFh, as a cartridge without a save
    mov es, dx
    xor di, di
    mov ch, al                      ; (AX = KB, under 128)
    xor cl, cl
    shl ch, 1                       ; CX = KB x 512 words
    mov ax, 0FFFFh
    cld
    rep stosw
    pop es
    call rw_savenm
    cmp byte [rw_batt], 0
    je .r
    ; the save, if there is one: a short or missing file leaves FFh
    push es
    mov es, [rw_ramseg]
    xor bx, bx
    mov cx, [rw_savelen]
    xor dx, dx
    cmp byte [rw_rtc], 0
    je .cap
    add cx, 48
.cap:
    mov si, rw_savename             ; beside the game, which is where we
    call OSAPI_FILE_READ            ;   stand (rw_load): DX:AX = bytes
    pop es
    jc .r
    cmp byte [rw_rtc], 0
    je .r
    mov cx, [rw_savelen]
    add cx, 44
    cmp ax, cx
    jb .r
    call rw_rtcin
.r:
    ret
.room:
    mov si, rw_s_noram
    jmp rw_toast

; rw_savenm - rw_savename = rw_name with its extension made .SAV
rw_savenm:
    mov di, rw_savename
    mov ax, '.S'
    mov dx, 'AV'
; rw_extnm - DI = rw_name with its extension made AX:DX's four characters
rw_extnm:
    mov si, rw_name
    mov cx, 8
.c:
    lodsb
    or al, al
    jz .e
    cmp al, '.'
    je .e
    mov [di], al
    inc di
    loop .c
.e:
    mov [di], ax
    mov [di+2], dx
    mov byte [di+4], 0
    ret

; rw_ramattach - after rdg_init (which zeroed the view): the banks and the
; clock into the state block, bank 0 into the window, and any time a save
; spent switched off onto the clock
rw_ramattach:
    mov ax, [rw_ramseg]
    mov bl, [rw_rambanks]
    mov bh, [rw_rtc]
    push ds
    pop es                          ; ES = ours, for the clock's copy
    push ds
    mov ds, [cs:rdg_viewseg]
    mov [S_RAMSEG], ax
    mov [S_RAMBANKS], bl
    mov byte [S_RAMCUR], 0
    mov si, rw_rtcsave              ; our copy of the clock into the view
    mov di, S_RTC
    mov cx, 13
.c:
    mov dl, [es:si]
    mov [di], dl
    inc si
    inc di
    loop .c
    mov [S_RTC+RTC_ON], bh
    mov byte [S_RTC+RTC_LST], 0FFh
    or ax, ax
    jz .nr
    call rdg_ramin                   ; bank 0 into the window
.nr:
    mov ax, [es:rw_rtcgap]          ; the time a save spent put away
    mov dx, [es:rw_rtcgap+2]
    mov cx, ax
    or cx, dx
    jz .p
    call rdg_rtcadd
.p:
    pop ds
    xor ax, ax
    mov [rw_rtcgap], ax
    mov [rw_rtcgap+2], ax
    ret

; =============================================================================
; Static's profile: NAME.RDP beside the game (REALDMG-SPEC 16.8)
; =============================================================================

; rw_prfload - after the ROM is read, on the UI task, beside the game: a
; claim for the profile when there is a recompiler, emptied, and NAME.RDP
; read into it - kept only if it is this game's (the trailer's magic, the
; header's checksums and the count)
rw_prfload:
    cmp word [rw_jitseg], 0
    je .r
    push es
    cmp word [rw_prfseg], 0         ; read before the ROM's plan
    jne .check                      ;   (rw_jit2claim): checked now
    cmp byte [rw_jitmode], 3        ; Static reads it, so Static is the
    jne .want                       ;   profile on
    mov byte [rw_prfon], 1
.want:
    cmp byte [rw_prfon], 0          ; Machine > Profile: off, no claim
    je .r0
    mov ax, RW_PRFKB
    call OSAPI_MEM_CLAIM            ; DX = its segment (the state block
    jc .r0                          ;   names it: rw_mreloc, rwmove.inc)
    mov [rw_prfseg], dx
    call rw_prfrd
    jc .empty
.check:
    mov es, [rw_prfseg]
    xor ch, ch
    call rw_prftail                 ; ZF = 1: the trailer is this game's
    je .r0
.empty:
    mov es, [rw_prfseg]
    xor di, di
    mov cx, RW_PRFFILE / 2 + 1
    xor ax, ax
    cld
    rep stosw
.r0:
    pop es
.r:
    ret

; rw_prfrd - NAME.RDP beside the game into [rw_prfseg]'s claim. CF = 1: not
; there, or not a profile's size
rw_prfrd:
    push es
    mov byte [rw_prfdirty], 0
    mov di, rw_prfname
    mov ax, '.R'
    mov dx, 'DP'
    call rw_extnm
    cmp byte [rw_state], 0          ; a game running: its folder (the
    je .here                        ;   current one may have moved since)
    mov dx, [rw_romdir]
    mov bl, [rw_romvol]
    call rw_fgo
    jc .back
.here:
    mov es, [rw_prfseg]
    xor bx, bx
    mov cx, RW_PRFFILE
    xor dx, dx
    mov si, rw_prfname
    call OSAPI_FILE_READ            ; DX:AX = the bytes read
    jc .back
    cmp ax, RW_PRFFILE
    je .back
    stc
.back:
    pushf
    cmp byte [rw_state], 0
    je .r
    call rw_fback
.r:
    popf
    pop es
    ret

; rw_prfblocks - AX = the blocks the profile names (its entries weighing
; anything), or 0 with none
rw_prfblocks:
    xor ax, ax
    cmp word [rw_prfseg], 0
    je .r
    push bx
    push cx
    push es
    mov es, [rw_prfseg]
    mov bx, 3
    mov cx, RW_PRFN
.c:
    cmp byte [es:bx], 0
    je .n
    inc ax
.n:
    add bx, 4
    loop .c
    pop es
    pop cx
    pop bx
.r:
    ret

; rw_jit2claim - on the UI task at the load, after rw_jitclaim and before
; the ROM's plan: the cache's second 64KB (REALDMG-SPEC 16.2.1) when the
; game's NAME.RDP names more blocks than the first holds. Read here, it is
; kept, and the Profile is on: the game has one. The ROM takes what is left,
; and gives it back if that is not enough (rw_load).
rw_jit2claim:
    cmp word [rw_jitseg], 0
    je .r
    cmp word [rw_prfseg], 0         ; (a load that failed left one)
    jne .have
    mov ax, RW_PRFKB
    call OSAPI_MEM_CLAIM            ; DX = the profile's segment
    jc .r
    mov [rw_prfseg], dx
.have:
    call rw_prfrd
    jnc .got
    xor dx, dx                      ; no profile: no claim
    xchg dx, [rw_prfseg]
    call OSAPI_MEM_FREE
    cmp byte [rw_big], 0            ; Machine > Cache: 128K asks for it
    jne rw_jit2get                  ;   whatever the profile says
%ifdef PKG_MEGA
    cmp byte [rw_pm], 0             ; ...and so does kern_mega's arm
    jne rw_jit2get                  ;   (rw_jit2more)
%endif
.r:
    ret
.got:
    call rw_jit2more                ; (the second segment, if it names more)
    cmp byte [rw_prfon], 0          ; kept for recording (Profile for Static
    jne .r                          ;   on) or for Static's preload - else
    cmp byte [rw_jitmode], 3        ;   the claim back: Profile for Static
    je .r                           ;   is off by default, and a NAME.RDP
    xor dx, dx                      ;   beside the game does not turn it on
    xchg dx, [rw_prfseg]
    call OSAPI_MEM_FREE
    ret
; rw_jit2more - the profile is in: the second 64KB if it names more than the
; first holds, or Machine > Cache says 128K, from what the heap has left.
; One segment without it.
rw_jit2more:
    cmp byte [rw_big], 0
    jne rw_jit2get
%ifdef PKG_MEGA
    cmp byte [rw_pm], 0             ; kern_mega's arm: the cartridge is out
    jne rw_jit2get                  ;   of the heap (10.3.1), so the cache
%endif                              ;   takes both segments, whatever game
    call rw_prfblocks
    cmp ax, RW_JIT2N
    jb rw_jit2get.r
rw_jit2get:
    cmp word [rw_jitseg], 0
    je .r
    cmp word [rw_jit2seg], 0
    jne .r
    mov ax, 64
    call OSAPI_MEM_CLAIM            ; DX = its segment (the state block
    jc .r                           ;   names it: rw_mreloc, rwmove.inc)
    mov [rw_jit2seg], dx
.r:
    ret

%ifdef PKG_MEGA
; rw_jitxget - kern_mega's arm under CPU_F_PM: the recompiler's EXTRA tiers
; (REALDMG-SPEC 16.2.5, 16.2.6) - 64KB cache segments past the second, in
; ONE block of the extended pool (OSAPI_XMEM_ALLOC), so the conventional
; heap gives them nothing and a claim record is not spent on them (a package
; holds MEM_OWNER_MAX = 8, and a game five). How many: Machine > Cache: All
; (or Static, whose preload must not stop at a full cache, 16.8) takes up to
; RT_XMAX; otherwise RW_XDEF. Either way the pool keeps a quarter of what it
; has, and RW_XPRESKB at least, for whatever runs beside. Each tier is a
; SEG_SUBC pair over its 64KB of the block (rw_jitxset), through one
; OSAPI_XMEM_SEL selector over its start; a move of the block rebases them
; all and the cache needs no flush (R3). The second segment first, and
; nothing without the first. On the UI task; rw_rtinit hands them to the
; core. Every register kept
rw_jitxget:
    cmp byte [rw_pm], 0
    je .r0
    cmp byte [rw_jitxk], 0
    jne .r0                         ; (made already)
    call rw_jit2get
    cmp word [rw_jit2seg], 0
    je .r0
    push ax
    push bx
    push cx
    push dx
    call OSAPI_XMEM_CAPS            ; AX = KB the pool can hand out
    mov bx, ax
    shr bx, 1
    shr bx, 1                       ; BX = a quarter of it...
    cmp bx, RW_XPRESKB
    jae .q
    mov bx, RW_XPRESKB              ; ...RW_XPRESKB at least
.q:
    sub ax, bx
    jbe .x
    mov cl, 6                       ; tiers it pays for, 64KB each
    shr ax, cl
    mov bx, RW_XDEF
    cmp byte [rw_big], 0            ; Cache: All, or Static: the lot
    jne .all
    cmp byte [rw_jitmode], 3
    jne .k0
.all:
    mov bx, RT_XMAX
.k0:
    cmp ax, bx
    jbe .k
    mov ax, bx
.k:
    mov cx, ax
    jcxz .x
.try:
    mov dx, cx                      ; DX:AX = k x 64KB, k from the most
    xor ax, ax                      ;   down
    call OSAPI_XMEM_ALLOC           ; DX:AX = the block's base
    jnc .got
    loop .try
    jmp short .x
.got:
    mov [rw_xjbase], ax
    mov [rw_xjbase + 2], dx
    push cx
    xor cx, cx                      ; its first 64KB: the selector every
    call OSAPI_XMEM_SEL             ;   tier's pair is asked against
    pop cx
    jc .nosel
    mov [rw_xjsel], ax
    mov [rw_jitxk], cl
    call rw_jitxset
    jmp short .x
.nosel:
    mov ax, [rw_xjbase]
    call OSAPI_XMEM_FREE
    xor ax, ax
    mov [rw_xjbase], ax
    mov [rw_xjbase + 2], ax
.x:
    pop dx
    pop cx
    pop bx
    pop ax
.r0:
    ret

; rw_jitxset - the extra tiers' segments off the block: [rw_jitxk] of them,
; tier n a SEG_SUBC pair at n x 64KB from [rw_xjsel]'s base (rw_subc: a tier
; is CODE, 10.3.2), its data half in rw_jitx; a pair refused (no LDT entry)
; ends the run there, and the rest are 0. Every register kept but the flags
rw_jitxset:
    push ax
    push cx
    push dx
    push si
    xor ax, ax                      ; AX = paragraphs past the block's start
    mov cl, [rw_jitxk]
    xor ch, ch
    mov si, rw_jitx
.l:
    cmp si, rw_jitx + RT_XMAX * 2
    jae .r
    mov word [si], 0
    jcxz .n
    mov dx, [rw_xjsel]
    call rw_subc
    jnc .st
    xor cx, cx
    jmp short .n
.st:
    mov [si], dx
    dec cx
    add ah, 10h
.n:
    inc si
    inc si
    jmp short .l
.r:
    pop si
    pop dx
    pop cx
    pop ax
    ret

; rw_jitxfree - ...and back: the block freed, its selector and every tier's
; pair with it (a game closed, the interpreter's cache given back). The
; core must have forgotten them first (S_JITXN 0). Every register kept
rw_jitxfree:
    cmp byte [rw_jitxk], 0
    je .r
    mov byte [rw_jitxk], 0
    push ax
    push dx
    xor ax, ax
    xor dx, dx
    xchg ax, [rw_xjbase]
    xchg dx, [rw_xjbase + 2]
    call OSAPI_XMEM_FREE
    mov word [rw_xjsel], 0
    call rw_jitxset                 ; (the table: every word 0)
    pop dx
    pop ax
.r:
    ret

rw_xjbase       dw 0, 0             ; the extra tiers' block, or 0
rw_xjsel        dw 0                ; ...its first 64KB's selector
%endif

; rw_cfgload - REALDMG.CFG out of SYSTEM\APPDATA at the start: the
; settings, or none of them for a file that is not ours
rw_cfgload:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    push ds
    pop es
    mov si, rw_s_cfg
    mov bx, rw_cfg
    mov cx, RW_CFGN
    xor dx, dx
    call od_read
    jc .r
    or dx, dx
    jnz .r
    cmp word [rw_cfg], 'RD'
    jne .none
    cmp ax, RW_CFGN
    jne .v2
    cmp byte [rw_cfg + 2], RW_CFGVER
    jne .none
    call rw_cfgkeys                 ; the keys, when they are keys
    jmp short .gn
.v2:
    cmp ax, RW_CFGKEY               ; version 2: the shipped keys
    jne .v1
    cmp byte [rw_cfg + 2], 2
    jne .none
.gn:
    cmp byte [rw_cfg + 4], RW_CFGGN
    jbe .big
    jmp short .none
.v1:
    cmp ax, 4                       ; version 1: the byte, and no games
    jne .none
    cmp byte [rw_cfg + 2], 1
    jne .none
    mov byte [rw_cfg + 4], 0
.big:
    test byte [rw_cfg + 3], RW_CFGBIG
    jz .r
    mov byte [rw_big], 1
    jmp short .r
.none:
    mov byte [rw_cfg + 4], 0        ; (not ours: no games of it either)
.r:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; rw_cfgkeys - REALDMG.CFG's keys into rw_keymap, unless one of them is not
; a key Machine > Controls could have given (then all eight as they shipped)
rw_cfgkeys:
    mov si, rw_cfg + RW_CFGKEY
    mov cx, 8
.k:
    lodsb
    cmp al, 0Ah                     ; (Esc, and 1-9: the save states')
    jbe .r
    cmp al, 58h                     ; (F12: the log's, rw_logflush)
    jae .r
    cmp al, 38h
    je .r
    cmp al, 3Bh
    jb .n
    cmp al, 42h                     ; (F1-F4; F5 and F8 the states', F6
    jbe .r                          ;   and F7 the replays')
.n:
    loop .k
    mov si, rw_cfg + RW_CFGKEY
    mov di, rw_keymap
    mov cx, 8
    cld
    rep movsb
.r:
    ret

; rw_cfgsave - the settings back into REALDMG.CFG, on the UI task. A
; refusal says nothing: the setting holds for this session
rw_cfgsave:
    push es
    push ds
    pop es
    mov word [rw_cfg], 'RD'
    mov byte [rw_cfg + 2], RW_CFGVER
    mov al, [rw_big]
    mov [rw_cfg + 3], al
    mov si, rw_keymap               ; Machine > Controls' keys
    mov di, rw_cfg + RW_CFGKEY
    mov cx, 8
    cld
    rep movsb
    mov si, rw_s_cfg
    mov bx, rw_cfg
    mov cx, RW_CFGN
    mov di, rw_cfgip
    call od_write
    pop es
    ret

; rw_bigtoggle - Machine > Cache, on the UI task with the lock held: 64K
; or 128K, the recompiler's code (REALDMG-SPEC 16.2.1), kept in REALDMG.CFG
; for every game after. 128K in a session takes the second 64KB from what
; the heap has left, and the worker starts the recompiler again over both;
; without the room - or going back to 64K - it is the next game's.
rw_bigtoggle:
    xor byte [rw_big], 1
    call rw_cfgsave
    call rw_mgrey
    cmp byte [rw_state], 0          ; no game: the next one's
    je .r
    mov si, rw_s_bignx
%ifdef PKG_MEGA
    cmp byte [rw_pm], 0             ; the arm's All under CPU_F_PM: the pool's
    jne .say                        ;   tiers are sized at the load (16.2.6)
%endif
    cmp byte [rw_big], 0
    je .say                         ; 64K: the next game's
    cmp word [rw_jit2seg], 0
    jne .r                          ; (already had it, from its profile)
    call rw_jit2get
    cmp word [rw_jit2seg], 0
    je .say                         ; no room now
    call rw_mfree                   ; (it moves, rwmove.inc)
    mov byte [rw_seg2q], 1          ; the worker: the recompiler again
    ret
.say:
    jmp rw_toast
.r:
    ret

; rw_prftoggle - Machine > Profile, on the UI task with the lock held. On:
; the claim and NAME.RDP, and recording from here on. Off: what was
; recorded saved, then the claim given back - a session meant for On alone
; keeps its 9KB and writes no file at the close. Static reads the profile,
; so it stays on while Static is the setting. Before a game it is the
; setting the load takes.
rw_prftoggle:                       ; (before a game too: its load claims it)
    cmp byte [rw_prfon], 0
    jne .off
    mov byte [rw_prfon], 1
    call rw_prfup
    jmp short .m
.off:
    cmp byte [rw_jitmode], 3
    je .st
    mov byte [rw_prfon], 0
    call rw_prfdrop
.m:
    jmp rw_mgrey
.st:
    mov si, rw_s_prfst
    jmp rw_toast
.r:
    ret

; rw_stprep - on the UI task, before Machine > Recompiler's next setting
; (the menu, or F3): when that is Static, which reads the profile, the
; profile on and read here, where a file can be, and the cache's second
; 64KB if it names more than the first holds (REALDMG-SPEC 16.2.1)
rw_stprep:
    mov byte [rw_prfon], 1
    call rw_prfup
    call rw_jit2more
    call rw_mgrey
.r:
    ret

; rw_prfup - on the UI task, a game running: the profile claimed, read and
; handed to the core. The table is whole before the core is told of it.
rw_prfup:
    cmp byte [rw_state], 0
    je .r
    cmp word [rw_prfseg], 0         ; already in, and recording: as it is
    jne .r                          ;   (rw_prfload would check a trailer
    call rw_prfload                 ;   only the save writes)
    mov ax, [rw_prfseg]
    or ax, ax
    jz .r
    push ds
    mov ds, [cs:rdg_viewseg]
    mov word [S_PRFMASK], RW_PRFN - 1
    mov [S_PRFSEG], ax
    pop ds
.r:
    ret

; rw_prfdrop - on the UI task: the profile saved if it grew, the core told
; to record no more, and - once the worker has been seen out of the
; machine, so a rt_prfadd that read [S_PRFSEG] first has finished - the
; claim freed
rw_prfdrop:
    cmp word [rw_prfseg], 0
    je .r
    call rw_prfsave
    push ds
    mov ds, [cs:rdg_viewseg]
    mov word [S_PRFSEG], 0
    pop ds
.wait:
    cmp byte [rw_incore], 0
    je .free
    call rw_bserve                  ; (it may be waiting on us for a bank)
    call OSAPI_TASK_YIELD
    jmp short .wait
.free:
    xor dx, dx
    xchg dx, [rw_prfseg]
    call OSAPI_MEM_FREE
.r:
    ret

; rw_prftail - ES = the profile: ZF = 1 when its trailer is this game's;
; with CH = 1, the trailer written first. The header is bank 0's, which
; rw_romseg holds in either load (rw_ramsetup reads it so too)
rw_prftail:
    push ds
    mov ds, [rw_romseg]
    mov ax, [014Eh]
    mov dl, [014Dh]
    pop ds
    mov di, RW_PRFTAIL
    or ch, ch
    jz .cmp
    mov word [es:di], 'RD'
    mov word [es:di+2], 'P1'
    mov [es:di+4], dl
    mov [es:di+5], ax
    mov word [es:di+7], RW_PRFN
.cmp:
    cmp word [es:di], 'RD'
    jne .x
    cmp word [es:di+2], 'P1'
    jne .x
    cmp [es:di+4], dl
    jne .x
    cmp [es:di+5], ax
    jne .x
    cmp word [es:di+7], RW_PRFN
.x:
    ret

; rw_prfnote - the profile's growth this run, before rdg_init zeroes the count
rw_prfnote:
    push ds
    mov ds, [cs:rdg_viewseg]
    cmp word [S_PRFNEW], 0
    pop ds
    je .r
    mov byte [rw_prfdirty], 1
.r:
    ret

; rw_prfsave - at the close, on the UI task: NAME.RDP beside the game, if
; the profile grew. A failure says nothing - the profile is only a speed-up
rw_prfsave:
    cmp word [rw_prfseg], 0
    je .r
    call rw_prfnote
    cmp byte [rw_prfdirty], 0
    je .r
    push es
    mov es, [rw_prfseg]
    mov ch, 1
    call rw_prftail                 ; the trailer, written
    mov dx, [rw_romdir]             ; beside the game
    mov bl, [rw_romvol]
    call rw_fgo
    jc .back
    xor bx, bx
    mov cx, RW_PRFFILE
    xor dx, dx
    mov si, rw_prfname
    call OSAPI_FILE_WRITE
.back:
    call rw_fback
    pop es
.r:
    ret

; rw_ramkeep - before a reset or a save: the window back to its bank, and the
; clock into rw_rtcsave
rw_ramkeep:
    push ds
    pop es
    push ds
    mov ds, [cs:rdg_viewseg]
    call rdg_ramsync
    mov si, S_RTC
    mov di, rw_rtcsave
    mov cx, 16
    cld
    rep movsb
    pop ds
    ret

; rw_onclose - the close negotiator, on the UI task with the lock held: a
; battery's RAM saved, and the close always goes ahead
rw_onclose:
    mov byte [rw_closing], 1        ; a worker waiting on a bank ends now
    cmp byte [rw_logon], 0          ; the log, if it was on
    je .s
    mov byte [rw_logon], 0
    call rw_logwrite
.s:
    cmp byte [rw_state], 0
    je .r
    mov byte [rw_fsq], 1            ; the worker out of the machine first, as
.w:                                 ;   rw_unload has it: a RAM bank it was
    cmp byte [rw_incore], 0         ;   switching (rdg_rammap) would be saved
    je .go                          ;   half copied. It stays out - the
    call rw_bserve                  ;   window is going
    call OSAPI_TASK_YIELD
    jmp short .w
.go:
    call rw_fmclose
    call rw_prfsave
    cmp byte [rw_batt], 0
    je .r
    cmp word [rw_ramseg], 0
    je .r
    call rw_save
.r:
    clc
    ret

; rw_unload - Machine > Load Game over a game that is running, on the UI
; task: the game put away as the window's close puts it away (its battery
; save, its profile, the log, the FM chip), and its own memory given back -
; the ROM or the bank cache's slots and store, its RAM, its profile - so the
; next one is planned (rw_load) over a heap with room in it. The recompiler's
; cache and the damage memory stay: the next game takes them up (rw_start's
; rt_init forgets what was translated). The worker is kept out of the
; machine while it goes. A load that then fails leaves no game. Keeps SI
rw_unload:
    cmp byte [rw_state], 0
    je .r
    push si
    mov byte [rw_fsq], 1            ; the worker out of the machine first
.wait:
    cmp byte [rw_incore], 0
    je .go
    call rw_bserve
    call OSAPI_TASK_YIELD
    jmp short .wait
.go:
    call rw_onclose                 ; (it turns rw_closing on: back off -
    mov byte [rw_closing], 0        ;   this window stays)
    xor dx, dx                      ; a replay's buffer rw_rpdrop left: let
    xchg dx, [rw_rpseg]             ;   go while the slots it may be lent
    or dx, dx                       ;   from are still the game's (10.3.3),
    jz .nrp                         ;   or the next replay frees a stale one
    call rw_mrel
.nrp:
    mov byte [rw_state], 0
    mov byte [rw_pause], 0
    mov byte [rw_ntc], 0
    cmp byte [rw_bcache], 0         ; the ROM: the bank cache's store and
    je .one                         ;   slots, or its one claim
    call rw_xfree                   ; (or the banks in place, 10.3.1)
    xor dx, dx
    xchg dx, [rw_slotseg]
    or dx, dx                       ; (none: the banks in place, 10.3.1)
    jz .nsl
    call OSAPI_MEM_FREE
.nsl:
    mov byte [rw_bcache], 0
    xor ax, ax
    mov [rdg_nres], ax               ; (rdgsys's: the next is whole unless
    mov [rdg_bfill], ax              ;   its plan says otherwise)
    mov [rdg_bmiss], ax
    jmp short .ram
.one:
    xor dx, dx
    xchg dx, [rw_romseg]
    or dx, dx
    jz .ram
    call OSAPI_MEM_FREE
.ram:
    xor dx, dx
    xchg dx, [rw_ramseg]
    or dx, dx
    jz .prf
    call OSAPI_MEM_FREE
.prf:
    xor dx, dx
    xchg dx, [rw_prfseg]
    or dx, dx
    jz .seg2
    call OSAPI_MEM_FREE
.seg2:
%ifdef PKG_MEGA
    call rw_jitxfree                ; (the extra tiers: the next game's load
%endif                              ;   takes what its ROM leaves, 16.2.5)
    cmp byte [rw_big], 0            ; the cache's second segment, unless
    jne .x                          ;   Cache: 128K keeps it for every game
    xor dx, dx
    xchg dx, [rw_jit2seg]
    or dx, dx
    jz .x
    call OSAPI_MEM_FREE
.x:
    push ds
    mov ds, [cs:rdg_viewseg]
    mov word [S_JITSEGB], 0         ; (rw_start's rt_init takes what there is)
%ifdef PKG_MEGA
    mov byte [S_JITXN], 0           ; (the extra tiers went with the game)
%endif
    mov word [S_PRFSEG], 0
    pop ds
    mov byte [rw_fsq], 0
    call rw_mgrey
    call rw_repaint
    pop si
.r:
    ret

; rw_save - NAME.SAV beside the game: the RAM, and for a clock VBA's
; 48-byte trailer, so the file is any emulator's
rw_save:
    call rw_ramkeep
    mov cx, [rw_savelen]
    cmp byte [rw_rtc], 0
    je .w
    call rw_rtcout
    add cx, 48
.w:
    push cx
    mov dx, [rw_romdir]             ; beside the game
    mov bl, [rw_romvol]
    call rw_fgo
    pop cx
    jc .no
    push es
    mov es, [rw_ramseg]
    xor bx, bx
    xor dx, dx
    mov si, rw_savename
    call OSAPI_FILE_WRITE
    pop es
.no:
    call rw_fback                   ; (CF kept)
    jnc .r
    mov si, rw_s_nosave
    jmp rw_toast
.r:
    ret

; rw_rtcout - the trailer at rw_ramseg:rw_savelen: ten dwords (the live
; registers, then the latched) and the time it was written as 64 bits
rw_rtcout:
    push ax
    push cx
    push dx
    push si
    push di
    push es
    mov es, [rw_ramseg]
    mov di, [rw_savelen]
    mov si, rw_rtcsave
    mov cx, 10
    cld
.c:
    lodsb
    xor ah, ah
    stosw
    xor ax, ax
    stosw
    loop .c
    call rw_unix                    ; DX:AX
    stosw
    mov ax, dx
    stosw
    xor ax, ax
    stosw
    stosw
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; rw_rtcin - the trailer read back: the registers, and the seconds since it
; was written (none if the clock reads earlier than the stamp)
rw_rtcin:
    push ds
    push ds
    pop es
    mov si, [rw_savelen]
    mov di, rw_rtcsave
    mov cx, 10
    mov ds, [rw_ramseg]
    cld
.c:
    lodsw
    stosb
    lodsw
    loop .c
    mov ax, [si]                    ; the stamp's low dword
    mov dx, [si+2]
    pop ds
    mov bx, ax
    or bx, dx
    jz .r
    mov cx, ax
    mov bx, dx
    call rw_unix
    sub ax, cx
    sbb dx, bx
    jc .r                           ; the clock is behind the save
    mov [rw_rtcgap], ax
    mov [rw_rtcgap+2], dx
.r:
    ret

; rw_unix - the system clock as seconds since 1970 (DX:AX). Years 1980-2099,
; so every fourth year is a leap year.
rw_unix:
    push bx
    push cx
    push si
    push di
    call OSAPI_CLK_GET              ; CX year, DH:DL month:day, BH:BL h:m,
    push ax                         ;   AH seconds
    push bx
    mov si, cx                      ; SI = the year
    mov bl, dh
    xor bh, bh
    dec bx
    shl bx, 1
    mov di, [rw_cumdays+bx]         ; days before the month
    xor dh, dh
    dec dx
    add di, dx                      ; + the day
    cmp bx, 4                       ; past February in a leap year
    jb .nl
    test si, 3
    jnz .nl
    inc di
.nl:
    mov ax, si
    sub ax, 1969
    shr ax, 1
    shr ax, 1
    add di, ax                      ; + the leap days since 1970
    mov ax, si
    sub ax, 1970
    mov cx, 365
    mul cx
    add ax, di                      ; AX = days since 1970
    mov cx, 43200
    mul cx
    shl ax, 1
    rcl dx, 1                       ; DX:AX = days x 86,400
    mov si, ax
    mov di, dx
    pop bx                          ; BH hour, BL minute
    mov al, bh
    xor ah, ah
    mov cx, 3600
    mul cx
    add si, ax
    adc di, dx
    mov al, bl
    mov cl, 60
    mul cl
    add si, ax
    adc di, 0
    pop ax                          ; AH = seconds
    mov al, ah
    xor ah, ah
    add ax, si
    adc di, 0
    mov dx, di
    pop di
    pop si
    pop cx
    pop bx
    ret

rw_battab:      db 03h, 06h, 09h, 0Fh, 10h, 13h, 1Bh, 1Eh, 0
rw_cumdays:     dw 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334

; rw_fgo - stand in folder DX of volume BL (UI task), remembering where we
; stood for rw_fback. CF = 1: it could not, and nothing moved.
rw_fgo:
    call rw_mpin                    ; every claim of ours still until rw_fback:
    push dx                         ;   the callers hold one in ES across the
    push bx                         ;   file call (rwmove.inc)
    call OSAPI_FILE_HERE
    mov [rw_hdir], dx
    mov [rw_hvol], bl
    pop bx
    pop dx
    push si
    push di
    call OSAPI_FILE_GOTO_QM
    pop di
    pop si
    jnc .r
    call rw_mfree                   ; (no file call follows: free again)
.r:
    ret

; rw_fback - back where rw_fgo found us. Every register and flag kept.
rw_fback:
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov dx, [rw_hdir]
    mov bl, [rw_hvol]
    call OSAPI_FILE_GOTO_QM
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    jmp rw_mfree                    ; (flags kept)

; rw_toast - SI = a line of our own, 24 characters at most
rw_toast:
    push ds
    pop es
    xor cx, cx
    call OSAPI_TOAST
    ret

; =============================================================================
; The worker
; =============================================================================
rw_worker_rs:                       ; THE RESTART POINT: our region moved with
rw_worker:                          ;   the worker parked at the top of its
    call OSAPI_CPU_INFO             ;   loop; draw the next frame whole
    mov bx, RW_BUDGET86
    cmp al, CPU_8086
    je .bud
    mov bx, RW_BUDGET
.bud:
    mov [rw_budget], bx
    call rw_ntcwait                 ; (the notice first: the clock after it)
    call OSAPI_GET_TICKS
    mov [rw_last], ax
    call rw_drawnow                 ; the next frame draws
.loop:
    mov bx, [rw_win]
    call OSAPI_TASK_ALIVE           ; no lock held; a closed window ends here
    mov byte [rw_incore], 1         ; raised BEFORE [rw_fsq] is read: the
    cmp byte [rw_fsq], 0            ;   order rwfsx.inc's handoff rests on
    jne .sleep
    cmp byte [rw_stq], 0            ; (and rwstate.inc's, the same way)
    jne .sleep
    cmp byte [rw_resetq], 0
    je .noreset
    mov byte [rw_resetq], 0
    call rw_reset
.noreset:
    cmp byte [rw_seg2q], 0          ; Machine > Cache's second segment: the
    je .nosq                        ;   recompiler again over both (and
    mov byte [rw_seg2q], 0          ;   Static's preload, into the room)
    call rw_jiton
.nosq:
    cmp byte [rw_jitq], 0
    je .nojq
    mov al, [rw_jitq]               ; the mode CPU's item asked for
    mov byte [rw_jitq], 0
    mov [rw_jitmode], al
    call rw_jitset
    call rw_mgrey                   ; the items say what it is now
    cmp byte [rw_jitqn], 0          ; (F3's, through the UI task: said)
    je .nojq
    mov byte [rw_jitqn], 0
    call rw_jnote
.nojq:
%ifdef RDG_LOSSY
    mov al, 0                       ; Screen's toggles, posted by the UI task:
    xchg al, [rw_lossq]             ;   turned over here, between frames
    or al, al
    jz .nolq
    call rw_lossyt
.nolq:
%endif
    cmp byte [rw_state], 1
    jne .held
    cmp byte [rw_pause], 0
    jne .held
    call OSAPI_GET_TICKS
    mov bx, ax
    sub bx, [rw_last]
    js .resync
    mov [rw_last], ax
    cmp bx, 10
    jbe .acc
    mov bx, 10
.acc:
    mov ax, [rw_pertick]            ; (RW_PERTICK at 100%: Machine > Speed)
    mul bx
    add ax, [rw_owed]
    cmp ax, RW_OWEDMAX              ; a machine that fell behind runs on from
    jbe .owe                        ;   here rather than racing to catch up
    mov ax, RW_OWEDMAX
.owe:
    mov [rw_owed], ax
    push ax
    mov al, ah                      ; (frames owed, for the log)
    call rw_logowed
    pop ax
    cmp ax, 256                     ; nothing owed yet: draw what has not
    jb .idle                        ;   been, else sleep to the tick
    call rw_keys
    call rw_frame
    jc .loop                        ; it stopped
    call rw_sound
    inc word [rw_nrun]
    call rw_stat
    sub word [rw_owed], 256
    xor bx, bx                      ; caught up: draw this one, once the
    mov ax, [rw_owed]               ;   last draw's own length has passed
    cmp ax, [rw_pertick]            ;   since it ended - and a tick's frames
    jb .due                         ;   owed is the tick's own burst, not
                                    ;   behind...
    mov bl, [rw_owed + 1]           ; ...or, behind, draws' lengths later
    xor bh, bh                      ;   again (rw_drawdue): a third of the
    call rw_behind                  ;   machine to the picture, or less the
.due:                               ;   further behind, the rest to catching
    call rw_drawdue                 ;   up
    jnc .loop
    mov si, rw_present
    call rw_timed                   ; a draw of D ticks: the next after D more
    jmp .loop                       ; the clock is read again after EVERY
                                    ;   frame: a worker that spent what it
                                    ;   was owed before looking slept a tick
                                    ;   it did not have whenever it was behind
.idle:                              ; caught up, and the machine would
    cmp byte [rw_undrawn], 0        ;   only sleep: the picture has it,
    je .sleep                       ;   whatever rw_drawdue says (its half
    mov si, rw_present              ;   of the machine is for a game that
    call rw_timed                   ;   has frames to run)
    jmp .loop
.held:                              ; paused or stopped: nothing owed, and
    mov word [rw_owed], 0           ;   the clock kept - Go ran at once what
    call OSAPI_GET_TICKS            ;   the menu's own stall had owed before
.resync:                            ;   the pause (gblog: 30 frames, 133%)
    mov [rw_last], ax
.sleep:
    cmp byte [rw_pause], 0          ; paused or stopped: the speaker quiet
    jne .q
    cmp byte [rw_state], 1
    je .sl
.q:
    call rw_quiet
.sl:
    mov byte [rw_incore], 0
    mov ax, 1
    call OSAPI_TASK_SLEEP
    jmp .loop

; rw_drawdue - CF = 1 if a draw is due. BX = 0: the game has caught up, and
; a draw may start once as long as the last one took has passed since it
; ended, so drawing takes at most half the machine. BX = k: the game is
; behind, and k more such lengths must pass (rw_behind) - so a
; game that slips a little loses a little of its picture rate rather than
; dropping to a draw every few ticks, which is how the first field run saw
; Super Mario Land fall from ~30 frames drawn to 4 whenever it slipped under
; 100%. The clock is rw_clk's, 838 ns a count: a 286's draw is milliseconds
; and a tick could not tell one from nothing.
rw_drawdue:
    push ax
    push dx
    push bx
    call rw_clk
    sub ax, [rw_drawt]
    sbb dx, [rw_drawt + 2]
    js .no                          ; (signed: [rw_drawt] may be ahead)
.k:
    or bx, bx
    jz .yes
    sub ax, [rw_dlast]
    sbb dx, [rw_dlast + 2]
    js .no
    dec bx
    jmp short .k
.yes:
    pop bx
    pop dx
    pop ax
    stc
    ret
.no:
    pop bx
    pop dx
    pop ax
    clc
    ret

; rw_behind - BX = frames owed: BX = the draws' lengths rw_drawdue waits
; beyond the last one's - one, and one more for every eight frames behind,
; to three (a third of the machine to the picture, down to a fifth). The
; second field run's logs had Super Mario Land owing the whole half second
; through every scroll with a third still going to the picture.
rw_behind:
    shr bx, 1
    shr bx, 1
    shr bx, 1
    inc bx
    cmp bx, 3
    jbe .r
    mov bx, 3
.r:
    ret

; rw_ntcwait - on the worker, before the game's first frame: the notice
; that the window is slow stays up RW_NTCT ticks (REALDMG-SPEC 14.3.1), or
; until full screen is asked, which is what it asks for. Then every line of
; the picture is drawn, over it
rw_ntcwait:
    cmp byte [rw_ntc], 0
    je .r
    cmp byte [rw_fsq], 0
    jne .end
    call OSAPI_GET_TICKS
    sub ax, [rw_ntct]
    cmp ax, RW_NTCT
    jae .end
    mov bx, [rw_win]
    call OSAPI_TASK_ALIVE           ; a closed window ends here
    mov ax, 1
    call OSAPI_TASK_SLEEP
    jmp short rw_ntcwait
.end:
    mov byte [rw_ntc], 0
    call rw_dmgfull
    call OSAPI_GET_TICKS            ; the speed line's second starts now: the
    mov [rw_tstat], ax              ;   notice is no time the game ran slow
    xor ax, ax
    mov [rw_nrun], ax
    mov [rw_ndrawn], ax
.r:
    ret

; rw_drawnow - the next draw is due at once (a fresh start, or a clock that
; changed under the last one: into or out of full screen)
rw_drawnow:
    inc byte [rw_dgen]
    push ax
    push dx
    call rw_clk
    sub ax, 1
    sbb dx, 0
    mov [rw_drawt], ax
    mov [rw_drawt + 2], dx
    xor ax, ax
    mov [rw_dlast], ax
    mov [rw_dlast + 2], ax
    pop dx
    pop ax
    ret

; rw_timed - call SI (a draw), then [rw_drawt] = its end + its length, and
; the log told how long it took
rw_timed:
    push ax
    push bx
    mov ax, [rw_nrun]               ; the frames run since the last draw,
    mov bx, ax                      ;   counted 1-7 and 8+ - what Steady
    xchg bx, [rw_plast]             ;   pacing evens out
    sub ax, bx
    jnb .g1
    mov ax, [rw_nrun]               ; (rw_stat began its count again: the
.g1:                                ;   frames since then)
    or ax, ax
    jz .g0
    dec ax
.g0:
    cmp ax, 7
    jbe .g
    mov ax, 7
.g:
    shl ax, 1
    mov bx, ax
    inc word [rw_phist + bx]
    pop bx
    pop ax
    mov byte [rw_undrawn], 0
    mov al, [rw_dgen]               ; (a clock changed under the draw - it
    push ax                         ;   parked across a whole full screen -
    call rw_clk                     ;   is no draw's length: the field run's
    push dx                         ;   window drew nothing for 34 seconds
    push ax                         ;   after leaving one)
    inc word [rw_ndrawn]
    call si
    call rw_clk                     ; DX:AX = the end
    pop bx
    pop cx                          ; CX:BX = the start
    pop si                          ; (SI's low byte: the count then)
    xchg si, ax
    cmp al, [rw_dgen]               ; ...against the count now
    xchg si, ax                     ; (xchg keeps the flags)
    je .same
    ret
.same:
    push dx
    push ax
    sub ax, bx
    sbb dx, cx                      ; DX:AX = the length
    mov [rw_dlast], ax
    mov [rw_dlast + 2], dx
    pop bx
    pop cx                          ; CX:BX = the end
    add bx, ax
    adc cx, dx
    mov [rw_drawt], bx
    mov [rw_drawt + 2], cx
    jmp rw_logdraw

; rw_frame - run the machine to its next frame, yielding between budgets.
; CF = 1: the core stopped (a bad opcode), and the UI task is told.
rw_frame:
    mov byte [rw_undrawn], 1
    call rw_clk                     ; (the log's: how long the machine ran,
    mov [rw_tgame], ax              ;   two reads of the clock a frame)
    mov [rw_tgame + 2], dx
    mov cx, 26                      ; with the LCD off no frame ever ends:
.run:                               ;   a frame's worth of budgets is one
    push cx
    mov ax, [rw_budget]
    push es                         ; the recompiler on: twice the budget -
    mov es, [rdg_viewseg]            ;   it is the IRQ latency, and recompiled
    cmp byte [es:S_JITON], 0        ;   2000 M-cycles are ~2 ms on a 286, so
    pop es                          ;   4000 are still inside a 1200-baud
    je .b                           ;   mouse's 8 ms; half the runs, and of
    shl ax, 1                       ;   rdg_run's rdg_sched and event exit
.b:
    call rdg_run                     ; ...and each returns with interrupts
    pop cx                          ;   on, so the tick can pre-empt us
    cmp al, RDGX_FRAME               ;   between two: no yield per budget,
    je .fr                          ;   which was nine task switches a frame
    cmp al, RDGX_BADOP
    je .bad
    loop .run
    jmp short .done
.fr:
    mov byte [rw_rpatb], 1          ; (a replay's buttons change here only)
.done:
    call rw_gameacc
    cmp byte [rw_infsx], 0          ; (in the bracket there is no one else)
    jne .ok
    call OSAPI_TASK_YIELD           ; one a frame, for a UI task a click woke
.ok:
    clc
    ret
.bad:
    mov byte [rw_state], 2
    mov bx, [rw_win]
    call OSAPI_WM_WAKE
    stc
    ret

; rw_sound - the speaker, after a frame: the LOUDER of the two square
; channels rdgsys keeps (REALDMG-SPEC section 17), at 131,072 / (2048 - x)
; Hz, through OSAPI_SND_TONE - and only when the note changed. A refusal (a
; louder owner of the speaker) leaves [rw_tone] stale-zero, so the next
; frame asks again.
rw_sound:
    cmp byte [rw_sndm], 0           ; Machine > Sound: Off, or at full speed
    je .on                          ;   only - a tune slowed to Speed's 75%
    cmp byte [rw_sndm], 2           ;   is in time with nothing
    je rw_quiet
    cmp byte [rw_speed], 0
    jne rw_quiet
.on:
    cmp byte [rw_fmon], 0           ; all four channels on the FM chip
    jne rw_fmframe                  ;   (rwfm.inc), else the speaker
    push ds
    mov ds, [cs:rdg_viewseg]
    xor bx, bx                      ; BL, BH = channel 1's, 2's volume (0 off)
    cmp byte [S_SQ1 + SQ_ON], 0
    je .v2
    mov bl, [S_SQ1 + SQ_VOL]
.v2:
    cmp byte [S_SQ2 + SQ_ON], 0
    je .v
    mov bh, [S_SQ2 + SQ_VOL]
.v:
    pop ds
    mov al, [rw_sch]                ; the channel playing STAYS while it
    cmp al, 1                       ;   sounds, unless the other is clearly
    jne .c2                         ;   louder: two voices crossing as their
    or bl, bl                       ;   envelopes fall made one speaker hop
    jz .any                         ;   between melody and harmony note by
    mov ah, bl                      ;   note (the first field run's "odd")
    add ah, 4
    cmp bh, ah
    jb .have
    mov al, 2
    jmp short .have
.c2:
    cmp al, 2
    jne .any
    or bh, bh
    jz .any
    mov ah, bh
    add ah, 4
    cmp bl, ah
    jb .have
    mov al, 1
    jmp short .have
.any:
    xor al, al                      ; none playing: the louder, if any
    cmp bl, bh
    jb .a2
    or bl, bl
    jz .have
    mov al, 1
    jmp short .have
.a2:
    mov al, 2
.have:
    mov [rw_sch], al
    push ds
    mov ds, [cs:rdg_viewseg]
    mov dx, 0FF13h
    mov bl, al                      ; BL = the channel (0 none)
    cmp al, 2
    jne .pick
    mov dx, 0FF18h
.pick:
    xor ax, ax                      ; silent unless a channel sounds
    or bl, bl
    jz .play
    mov si, dx
    mov cx, [si]                    ; NRx3, NRx4: the 11-bit frequency
    and ch, 7
    mov bx, 2048
    sub bx, cx                      ; BX = 2048 - x, 1..2048
    cmp bx, 9                       ; 131,072 / 8 is past the range anyway,
    jb .zero                        ;   and / 2 or / 1 overflows the divide
    mov dx, 2                       ; DX:AX = 131,072
    xor ax, ax
    div bx                          ; AX = hertz
    cmp ax, 20                      ; past the speaker's range: silent
    jb .zero
    cmp ax, 15000
    jbe .play
.zero:
    xor ax, ax
.play:
    pop ds
    cmp ax, [rw_tone]
    je .r
    mov [rw_tone], ax
    xor cx, cx                      ; until off
    mov dl, 40h
    call OSAPI_SND_TONE
    jnc .r
    mov word [rw_tone], 0FFFFh      ; refused: ask again next frame
.r:
    ret

; rw_quiet - the speaker off, if we had it, and the FM chip's keys
rw_quiet:
    cmp byte [rw_fmon], 0
    je .spk
    call rw_fmquiet
.spk:
    cmp word [rw_tone], 0
    je .r
    mov word [rw_tone], 0
    xor ax, ax
    xor cx, cx
    mov dl, 40h
    call OSAPI_SND_TONE
.r:
    ret

; rw_reset - Machine > Reset, on the worker: rdg_init again from the ROM
rw_reset:
    cmp byte [rw_state], 0
    je .r
    mov al, RLE_RESET
    call rw_logevt
    call rw_prfnote                 ; the profile's growth, before rdg_init
    call rw_ramkeep                 ; the cartridge's RAM and clock survive
    mov ax, [rw_romseg]             ; bank 0 read into the view itself (the
    cmp ax, [rdg_viewseg]           ;   bank cache): rdg_init copies it onto
    jne .st                         ;   itself, so the seam's trap goes back
    push ds                         ;   to the ROM's opcode first, or it is
    mov ds, ax                      ;   kept as D3 with S_SEAMPC zeroed and
    mov bx, [S_SEAMPC]              ;   the game stops on it (REALDMG-SPEC
    or bx, bx                       ;   3.1)
    jz .ns
    mov al, [S_SEAMOP]
    mov [bx - 1], al
.ns:
    pop ds
.st:
    call rw_start
    mov byte [rw_state], 1
    mov word [rw_owed], 0
.r:
    ret

; rw_stat - once a second, the overlay's line: the game's speed, frames run
; against the 59.7275 a second the cartridge expects, and the frames drawn
rw_stat:
    call OSAPI_GET_TICKS
    mov cx, ax
    sub cx, [rw_tstat]              ; CX = ticks counted
    cmp cx, 18
    jb .r
    mov [rw_tstat], ax
    cmp cx, 600                     ; a pause, or the first look: start over
    ja .zero0
    ; speed = run x 100 x 18.2065 / (59.7275 x ticks) = run x 3048 / (ticks x 100)
    mov ax, 100
    mul cx
    mov bx, ax                      ; BX = ticks x 100
    mov ax, [rw_nrun]
    mov dx, 3048
    mul dx                          ; DX:AX = run x 3048
    div bx                          ; AX = speed, %
    push ds
    pop es
    mov di, rw_ovltxt
    call rw_dec
    mov al, '%'
    stosb
    mov al, ' '
    stosb
    ; fps = drawn x 18.2065 / ticks ~ drawn x 182 / (ticks x 10)
    mov ax, 10
    mul cx
    mov bx, ax                      ; BX = ticks x 10
    mov ax, [rw_ndrawn]
    mov dx, 182
    mul dx
    div bx
    call rw_dec
    mov si, rw_s_fps
.c:
    lodsb
    stosb
    or al, al
    jnz .c
    push ds                         ; " JIT": the recompiler is running,
    mov ds, [cs:rdg_viewseg]         ;   for a photograph of the glass to say
    mov al, [S_JITON]
    pop ds
    or al, al
    jz .rec
    dec di
%ifdef RDG_AOT
    call rw_apre                    ; " Pn/m": a precompiled cartridge's,
    jmp short .rec                  ;   its images in memory of all of them
%endif
    mov si, rw_s_jit
    cmp byte [rw_jitmode], 3        ; " AOT": Static's
    jne .cj
    mov si, rw_s_aot
.cj:
    lodsb
    stosb
    or al, al
    jnz .cj
.rec:
    call rw_logsec                  ; the second, into the log if it is on
    jmp short .zero
.zero0:
    call rw_logsec.zero
.zero:
    mov byte [rw_ovlnew], 1
    xor ax, ax
    mov [rw_nrun], ax
    mov [rw_ndrawn], ax
.r:
    ret

; rw_dec - AX (0-999) as decimal at ES:DI, no leading zeros
rw_dec:
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in CX
    push bx
    push cx
    push dx
    mov bx, 10
    xor cx, cx
.d:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .d
.o:
    pop ax
    add al, '0'
    stosb
    loop .o
    pop dx
    pop cx
    pop bx
    ret

; rw_keys - the joypad, from the keys held - and nothing held while another
; window has the keyboard. F1 toggles the speed overlay.
rw_keys:
    xor dl, dl
    xor al, al                      ; the typed presses, taken whoever's the
    xchg al, [rw_klatch]            ;   keyboard is
    mov [rw_ktyped], al
    xor al, al                      ; ...and F1-F4's, likewise
    xchg al, [rw_flatch]
    mov [rw_ftyped], al
    inc word [rw_flook]
    cmp byte [rw_infsx], 0          ; the bracket's keyboard is ours
    jne .f1
    call OSAPI_WM_TOP
    cmp bx, [rw_win]
    jne .set
    call OSAPI_MENU_OWNER
    cmp bx, [rw_win]
    jne .set
.f1:
    mov al, KSC_ALT                 ; Alt held: a chord of ours (Alt+Enter
    call OSAPI_KEY_DOWN             ;   would otherwise press Start too)
    jc .set
    mov al, 3Bh                     ; F1, on its press
    mov bx, rw_f1was
    mov cl, 1
    call rw_fedge
    jnc .nof1
    call rw_stats
.nof1:
    push dx
    mov al, 3Ch                     ; F2: Screen > Interlace's next, and
    mov bx, rw_f2was                ;   said (rw_note)
    mov cl, 2
    call rw_fedge
    jnc .nof2
    mov al, 2Ah                     ; Shift+F2: full screen's pacing, Free
    call OSAPI_KEY_DOWN             ;   or Steady (rwfsx.inc's rw_pace) -
    jc .pace                        ;   the F-keys are all taken, so the
    mov al, 36h                     ;   least used get a Shift (2Ah, 36h)
    call OSAPI_KEY_DOWN
    jc .pace
    call rw_iltoggle
    mov si, [rw_i_scr + 2 * RW_SIL] ; (what rw_mgrey made the item say)
    call rw_note
    jmp short .nof2
.pace:
    xor byte [rw_pace], 1
    mov byte [rw_pk], 1
    mov si, rw_s_pfree
    cmp byte [rw_pace], 0
    je .pn
    mov si, rw_s_pstdy
.pn:
    call rw_note
.nof2:
%ifdef RDG_AOT
    jmp short .nof3                 ; (one mode: F3 has nothing to step to)
%endif
    mov al, 3Dh                     ; F3: Machine > Recompiler's next - here,
    mov bx, rw_f3was                ;   between two core runs, in the window
    mov cl, 4
    call rw_fedge                   ;   and the bracket alike (the menu's
    jnc .nof3                       ;   goes through the worker's rw_jitq)
    mov bl, [rw_jitmode]
    xor bh, bh
    mov al, [rw_jnext + bx]         ; AL = the next mode. Static, or the
    cmp al, 3                       ;   recompiler with no cache, wants
    je .f3ui                        ;   the UI task first (rw_jitprep): in
    cmp al, 2                       ;   the bracket that is this one, and
    je .f3go                        ;   the window's is woken for it
    cmp word [rw_jitseg], 0
    jne .f3go
.f3ui:
    cmp byte [rw_infsx], 0
    jne .f3in
    mov [rw_f3q], al
    mov bx, [rw_win]
    call OSAPI_WM_WAKE
    jmp short .nof3
.f3in:
    call rw_jitprep
    jc .nof3
.f3go:
    mov [rw_jitmode], al
    call rw_jitset
    call rw_mgrey
    call rw_jnote
.nof3:
    mov al, 3Eh                     ; F4: Machine > Speed's next, said, and
    mov bx, rw_f4was                ;   kept for the game by the UI task
    mov cl, 8
    call rw_fedge
    jnc .nof4
    call rw_spdnext
    mov bl, [rw_speed]
    xor bh, bh
    shl bx, 1
    mov si, [rw_spdit + bx]
    call rw_note
    mov byte [rw_spdq], 1
    mov bx, [rw_win]
    call OSAPI_WM_WAKE
.nof4:
    mov al, 43h                     ; F9: Machine > Sound's next, said, and
    mov bx, rw_f9was                ;   kept for the game by the UI task (as
    mov cl, 20h                     ;   F4's)
    call rw_fedge
    jnc .nof9
    call rw_sndnext
    mov si, [rw_i_mach + 2 * RW_MSND]
    call rw_note
    mov byte [rw_spdq], 1
    mov bx, [rw_win]
    call OSAPI_WM_WAKE
.nof9:
%ifdef RDG_LOSSY
    mov al, 44h                     ; F10: TWO SHADES on or off, F11: THE
    mov bx, rw_f10was               ;   CAMERA IN STEPS OF 2, 4, 8 (rdg_lossy,
    mov cl, 40h                     ;   REALDMG-SPEC 13.4) - said, and the
    call rw_fedge                   ;   next draw whole. No menu item, and
    jnc .nof10                      ;   not kept: they are to be tried
    mov al, RDG_LBW
    call rw_lossyt
.nof10:
    mov al, 57h
    mov bx, rw_f11was
    mov cl, 80h
    call rw_fedge
    jnc .nof11
    mov al, RDG_LCOARSE
    call rw_lossyt
.nof11:
%endif
    mov al, 58h                     ; F12: the log written now (rw_logflush,
    mov bx, rw_f12was               ;   the UI task's - after the bracket,
    mov cl, 10h                     ;   where a toast can be read)
    call rw_fedge
    jnc .nof12
    mov byte [rw_lflq], 1
    cmp byte [rw_infsx], 0
    jne .nof12
    mov bx, [rw_win]
    call OSAPI_WM_WAKE
.nof12:
    pop dx
    mov si, rw_keymap
    mov dh, 1
.k:
    lodsb
    or al, al
    jz .typ
    call OSAPI_KEY_DOWN             ; CF = 1: held
    jnc .n
    or dl, dh
.n:
    shl dh, 1
    jnz .k
.typ:
    ; A press TYPED since the last look is one, held now or not - a tap
    ; inside a draw is never seen held - but only for a button that was up
    ; then: a held key's typematic repeat, taken from the buffer after its
    ; release, would otherwise be a press nobody made
    mov al, [rw_kraw]
    not al
    and al, [cs:rw_ktyped]
    or dl, al
    ; OPPOSITE DIRECTIONS: a Game Boy's pad cannot hold both, a keyboard can,
    ; and Super Mario Land given both runs on to the right. Of a pair held
    ; together, the one pressed LATER wins - a roll from Right to Left turns
    ; at once, and letting Left go turns back while Right is still held
    push bx
    push cx
    mov ah, [rw_kraw]
    mov [rw_kraw], dl
    not ah
    and ah, dl                      ; AH = pressed since the last look
    mov cl, [rw_kpref]
    mov bl, 03h                     ; Right, Left
    call rw_socd
    mov bl, 0Ch                     ; Up, Down
    call rw_socd
    mov [rw_kpref], cl
    pop cx
    pop bx
.set:
    cmp byte [rw_rpmode], 0         ; a replay's: recorded, or played in
    je .s2                          ;   the keys' place, between frames
    call rw_rpkey                   ;   only (rwreplay.inc)
    jnc .s2
    ret
.s2:
    push ds
    mov ds, [cs:rdg_viewseg]
    mov al, [S_JOY]                 ; a button pressed since the last look
    not al                          ;   is the JOYPAD interrupt's (IF bit 4),
    and al, dl                      ;   which wakes a game waiting in HALT
    jz .nj                          ;   for a press (REALDMG-SPEC section 7)
    or byte [0FF0Fh], 10h
.nj:
    mov [S_JOY], dl
    pop ds
    ret
%ifdef RDG_LOSSY
; rw_lossyt - AL = RDG_LBW to turn Two Shades over, RDG_LCOARSE to take
; Scroll's next step (Off, 2, 4, 8 - F11, Screen's items): the composer
; patched (rdg_lpatch - so this runs where no task is inside it: rw_keys,
; the worker's loop, a game not running), said, kept for the game, and the
; next draw whole. Clobbers BX, SI, DX
rw_lossyt:
    test al, RDG_LBW
    jz .co
    xor [rdg_lossy], al
    call rdg_lpatch
    mov si, rw_s_bw
    test [rdg_lossy], al
    jnz .on
    mov si, rw_s_bw2                ; (its "Off")
.on:
    call rw_mgrey
    jmp short .say
.co:
    mov ah, [rdg_lossy]             ; the level's next - DOWN, the levels
    mov dl, ah                      ;   being 8, 4, 2 - wrapping to Off
    and dl, ~RDG_LCOARSE
    sub ah, RDG_LCO8
    and ah, RDG_LCOARSE
    or ah, dl
    mov [rdg_lossy], ah
    call rdg_lpatch
    call rw_mgrey
    mov si, [rw_i_scr + 2 * RW_SCO] ; (what rw_mgrey made the item say)
.say:
    call rw_note
    call rw_dmgfull
    call rw_cfgrec                  ; kept for the game, REALDMG.CFG the UI
                                    ;   task's to write (as F4's and F9's)
    mov byte [rw_spdq], 1
    mov bx, [rw_win]
    call OSAPI_WM_WAKE
    ret

; rw_losspost - Screen > Two Shades or Scroll, on the UI task: AL the
; bit, for the worker to turn over between frames (rw_lossq) - or now, with
; no game running and so no draw to be inside
rw_losspost:
    cmp byte [rw_state], 1
    je .q
    jmp rw_lossyt
.q:
    xor [rw_lossq], al
    ret

%endif
; rw_fedge - AL = F1-F4's scancode, BX = its was-held byte, CL = its bit in
; rw_ftyped: CF = 1 when it is pressed now (held now, not at the last look),
; or TYPED since the last look and up at both - a tap that fell between two
; looks, which a draw in full screen is long enough to hide. Held at either
; look, the press is the edge's to say, so it is never said twice - and a
; typed one counts only when the key has not been seen held for RW_FQUIET
; looks: a held key's typematic repeats queue in the BIOS's buffer, and one
; drained after the release was a second press, which turned F1's line off
; again the moment it was let go (gbwindow, an 8088's full screen). Clobbers
; CH
RW_FQUIET       equ 30
rw_fedge:
    push dx
    push si
    call OSAPI_KEY_DOWN
    sbb al, al
    mov ah, al                      ; AH = held now
    xchg al, [bx]                   ; AL = held at the last look
    mov ch, al
    xor si, si                      ; SI = the key's word in rw_fwhen
    mov dl, cl
.i:
    shr dl, 1
    jz .ix
    inc si
    inc si
    jmp short .i
.ix:
    or ah, ah
    jz .up
    mov dx, [cs:rw_flook]
    mov [cs:rw_fwhen + si], dx
.up:
    not al
    and al, ah
    jnz .yes
    or ch, ah
    jnz .no
    test [cs:rw_ftyped], cl
    jz .no
    mov dx, [cs:rw_flook]           ; (typed: not a held key's repeat)
    sub dx, [cs:rw_fwhen + si]
    cmp dx, RW_FQUIET
    jb .no
.yes:
    pop si
    pop dx
    stc
    ret
.no:
    pop si
    pop dx
    clc
    ret

; rw_note - SI = a line (a menu item's, its MENU_DIS mark skipped): said for
; a moment - a toast on the desktop, the speed line in the bracket, where a
; toast would wait for the desktop to come back (F2, F3)
rw_note:
    cmp byte [si], MENU_DIS
    jne .s
    inc si
.s:
    cmp byte [si], ' '              ; a menu item (RW_MI's): its text alone,
    je .it                          ;   from after its mark to its key's
    cmp byte [si], '*'              ;   padding
    jne .say
.it:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    inc si
    mov di, rw_nbuf
    mov cx, RW_MW
.ic:
    lodsb
    cmp al, ' '
    jne .ik
    cmp byte [si], ' '              ; (two spaces: the padding)
    je .ie
.ik:
    stosb
    or al, al
    loopnz .ic
.ie:
    mov byte [di], 0
    pop es
    pop di
    pop cx
    pop ax
    mov si, rw_nbuf
.say:
    cmp byte [rw_infsx], 0
    jne .fs
    push es
    push cx
    push ds
    pop es
    xor cx, cx                      ; ~3 s
    call OSAPI_TOAST
    pop cx
    pop es
    ret
.fs:
    push di
    push cx
    push es
    push ds
    pop es
    mov di, rw_msg
    mov cx, 20
.c:
    lodsb
    stosb
    or al, al
    loopnz .c
    mov byte [di], 0
    pop es
    call OSAPI_GET_TICKS
    add ax, 45                      ; ~2.5 s
    mov [rw_msgend], ax
    mov byte [rw_msgt], 1
    mov byte [rw_ovlnew], 1
    pop cx
    pop di
    ret

; rw_jnote - the recompiler's mode, said (F3's)
rw_jnote:
    push bx
    push si
    mov bl, [rw_jitmode]
    xor bh, bh
    dec bx
    shl bx, 1
    mov si, [rw_jit + bx]
    call rw_note
    pop si
    pop bx
    ret

rw_nbuf:    times MENU_MAXCH + 1 db 0   ; rw_note's item, without its mark and key

; S_JOY's bits: Right, Left, Up, Down, A, B, Select, Start
rw_jnext:   db 1, 3, 1, 2           ; F3: the mode after (none), Recompiler,
                                    ;   Interpreter, Static
rw_keymap:  db KSC_RIGHT, KSC_LEFT, KSC_UP, KSC_DOWN, 2Dh, 2Ch, 39h, KSC_ENTER, 0
rw_ktyped:  db 0                    ; (rw_keys: this look's rw_klatch)
rw_flatch:  db 0                    ; F1-F4, F12 and F9 TYPED since the last look
                                    ;   (rw_klat)
rw_f12was:  db 0                    ; F12 was held at the last look
rw_f9was:   db 0                    ; ...and F9
rw_f10was:  db 0                    ; ...and F10, F11
rw_f11was:  db 0
rw_lossq:   db 0                    ; Screen's toggles for the worker
rw_s_bw:    db 'Two shades: On', 0  ; (rw_lossyt's: each "On", then its
rw_s_bw2:   db 'Two shades: Off', 0 ;   "Off")
rw_lflq:    db 0                    ; F12: the log to be written by the UI
                                    ;   task (rw_onwake, rw_fsenter's end)
rw_ftyped:  db 0                    ; ...and this look's (rw_fedge)
rw_flook:   dw 0                    ; rw_keys's looks, counted...
rw_fwhen:   dw 0, 0, 0, 0, 0, 0, 0, 0 ; ...and the last that saw F1, F2,
                                    ;   F3, F4, F12, F9, F10 and F11 held

; rw_socd - one opposite pair, BL its two bits: DL the buttons, AH those
; pressed since the last look, CL the preferred of each pair. Not both held:
; the held one, or none, is the preference. Both: one newly pressed takes
; it, and the other is dropped from DL
rw_socd:
    mov bh, dl
    and bh, bl
    cmp bh, bl
    je .both
.pref:
    not bl
    and cl, bl
    not bl
    or cl, bh
    ret
.both:
    mov bh, ah
    and bh, bl
    jz .k                           ; neither new: as it was
    cmp bh, bl
    je .k                           ; both new at once: as it was
    call .pref                      ; the newer
.k:
    mov bh, cl
    and bh, bl
    not bl
    and dl, bl
    not bl
    or dl, bh
    ret

; rw_klat - AH = a typed key's scancode: its button latched for rw_keys -
; or a save state's key (rw_stkey: 1-9, F5, F8)
rw_klat:
    call rw_stkey
    push ax
    push si
    mov al, 10h                     ; F12 (an AT's 86h, a PC's 58h): bit 4
    cmp ah, 86h
    je .fl
    cmp ah, 58h
    je .fl
    mov al, 20h                     ; F9: bit 5
    cmp ah, 43h
    je .fl
    mov al, 40h                     ; F10: bit 6
    cmp ah, 44h
    je .fl
    mov al, 80h                     ; F11 (an AT's 85h, a PC's 57h): bit 7
    cmp ah, 85h
    je .fl
    cmp ah, 57h
    je .fl
    cmp ah, 3Bh                     ; F1-F4: latched for rw_fedge
    jb .m
    cmp ah, 3Eh
    ja .m
    push cx
    mov cl, ah
    sub cl, 3Bh
    mov al, 1
    shl al, cl
    pop cx
.fl:
    or [rw_flatch], al
    jmp short .r
.m:
    mov si, rw_keymap
    mov al, 1
.l:
    cmp byte [si], 0
    je .r
    cmp ah, [si]
    je .hit
    inc si
    shl al, 1
    jmp short .l
.hit:
    or [rw_klatch], al
.r:
    pop si
    pop ax
    ret

; rw_present - compose the frame and put it on the glass (the worker's)
rw_present:
    mov bx, [rw_win]
    call OSAPI_WM_DISPLAY           ; DH = bits per pixel
    push ds
    pop es
    cmp dh, 1
    jbe .mono
    cmp byte [rw_nobp], 0
    jne .mono
    cmp byte [rw_pfmt], 4           ; colour: the changed lines, as the
    je .c1                          ;   four planes BLITP reads (a buffer of
    call rw_dmgfull                 ;   the other format: every line again)
.c1:
    call rw_render
    mov byte [rw_pfmt], 4
    or ax, ax
    jz .none
    call rw_planes
    jmp short .show
.mono:
    cmp byte [rw_pfmt], 1           ; the buffer held the other format:
    je .m1                          ;   every line again
    call rw_dmgfull
.m1:
    call rw_render                  ; AX = the lines that changed
    mov byte [rw_pfmt], 1
    or ax, ax
    jnz .m2
.none:
    mov byte [rw_incore], 0         ; nothing did: nothing to put up
    ret
.m2:
    call rw_dither
.show:
    mov byte [rw_incore], 0         ; out of the machine BEFORE the lock: the
                                    ;   UI task may hold it, waiting on this
    mov byte [rw_shown], 1
    cmp byte [rw_abon], 0           ; the About card is over us: keep it
    jne .r
    call OSAPI_GFX_LOCK
    mov ax, KERNEL_SEG              ; a worker loads ES itself
    mov es, ax
    mov bx, [rw_win]
    test word [es:bx + W_FLAGS], 2
    jz .un
    call OSAPI_WM_CLIP_SET          ; CF = 1: not a pixel of us shows
    jc .un
    call rw_blitd                   ; the changed lines' bands
    call rw_overlay
.un:
    call OSAPI_GFX_UNLOCK
.r:
    ret

; rw_overlay - the speed line over the picture's top left, when F1 put it up
rw_overlay:
    cmp byte [rw_ovl], 0
    je .r
    cmp byte [rw_ovltxt], 0
    je .r
    mov bx, [rw_win]
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov cx, ax
    mov si, rw_ovltxt
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
.r:
    ret

; rw_blit - [rw_pseg]'s frame onto the content, the lock held and the clip armed:
; BLITP's four planes on a colour display, the dithered band through BLIT1 on
; a monochrome one (or where BLITP refuses). A buffer composed for the other
; kind of display - the window moved between two - paints white, and the
; worker's next draw puts the picture back.
rw_blit:
    mov bx, [rw_win]
    call OSAPI_WM_DISPLAY           ; DH = bits per pixel
    cmp dh, 1
    jbe .mono
    cmp byte [rw_pfmt], 4
    jne .colmono
    mov bx, [rw_win]
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov bx, dx
    mov es, [rw_pseg]
    xor si, si
    mov di, 4000h | 20              ; walk an armed clip; 20 bytes a plane
    mov bp, 80
    mov cx, RW_CONT_W
    mov dx, RW_CONT_H
    call OSAPI_GFX_BLITP
    jnc .r
    mov byte [rw_nobp], 1           ; refused (a straddle, kern_small): the
    jmp short .white                ;   next frame is dithered
.colmono:
    cmp byte [rw_pfmt], 1
    jne .white
    mov al, CWHITE                  ; the dither on a colour display, in two
    mov ah, CBLACK                  ;   colours
    call OSAPI_GFX_BLIT1_PEN
    jmp short .b1
.mono:
    cmp byte [rw_pfmt], 1
    jne .white
.b1:
    mov bx, [rw_win]
    call OSAPI_WM_CONTENT
    mov bx, dx
    mov es, [rw_pseg]
    xor si, si
    mov bp, 20
    mov cx, RW_CONT_W
    mov dx, RW_CONT_H
    call OSAPI_GFX_BLIT1
.r:
    push ds                         ; (ES as it came back before the frame
    pop es                          ;   had a segment of its own)
    ret
.white:
    jmp rw_fillwhite

; rw_dither - [rw_fseg]'s frame as one bit a pixel, 1 = lit: shade 0 all
; lit, 1 a quarter dark, 2 half (a checkerboard), 3 all dark - a 2x2 ordered
; dither
rw_dither:
    mov es, [rw_pseg]
    push ds
    mov ds, [rw_fseg]
    xor si, si
    xor di, di
    xor dx, dx                      ; DL = the row's parity
.row:
    call rw_dchg                    ; ZF = 1: this line did not change
    jnz .do
    add si, 40
    add di, 20
    jmp short .nx
.do:
    mov bl, 0AAh                    ; even rows: half on even columns, the
    mov bh, 0AAh                    ;   quarter on even columns
    test dl, 1
    jz .ev
    mov bl, 055h                    ; odd rows: the other half, no quarter
    xor bh, bh
.ev:
    mov cx, 20
.b:
    mov al, [si]                    ; bit 0
    mov ah, [si+20]                 ; bit 1
    mov dh, al
    and dh, ah                      ; shade 3: dark
    not al
    and al, ah                      ; shade 2: bit 1 alone
    and al, bl
    or dh, al
    mov al, [si]
    not ah
    and al, ah                      ; shade 1: bit 0 alone
    and al, bh
    or al, dh
    not al                          ; lit = not dark
    stosb
    inc si
    loop .b
    add si, 20
.nx:
    inc dl
    cmp dl, RW_CONT_H
    jb .row
    pop ds
    ret

; rw_dchg - DL = a line: ZF = 0 if the last rdg_renderd changed it. Any DS
rw_dchg:
    push bx
    mov bl, dl
    xor bh, bh
    cmp byte [cs:rw_chg + bx], 0
    pop bx
    ret

; rw_render - rdg_renderd into [rw_fseg], and its changed lines into rw_chg
; (every line, without the damage memory). AX = how many.
rw_render:
    push bx
    push cx
    push dx
    push si
    push di
    mov es, [rw_fseg]
    xor di, di
    mov word [rdg_dhook], rw_dscanhk
    call rw_clk                     ; (the log's: the scan and the lines)
    mov [rw_tscan], ax              ; (the scan's end, if there is none)
    mov [rw_tscan + 2], dx
    push dx
    push ax
    call rdg_renderd
    push ax
    call rw_clk
    mov bx, [rw_tscan]
    mov cx, [rw_tscan + 2]
    mov si, rw_acms                 ; the lines: the scan's end to now
    call rw_logacc
    mov ax, bx
    mov dx, cx
    pop si
    pop bx
    pop cx
    push si
    mov si, rw_asms                 ; the scan: the start to its end
    call rw_logacc
    pop ax
    push ax
    add [rw_achg], ax               ; (the log's counts)
    push ds                         ; (ES was the frame's)
    pop es
    mov di, rw_chg
    mov cx, 144 / 2
    mov ax, [rdg_dmgseg]
    or ax, ax
    jnz .cp
    add word [rw_acomp], 144
    mov ax, 0101h
    rep stosw
    jmp short .r
.cp:
    push ds
    mov ds, ax
    mov si, RDG_DCHG
    rep movsw
    xor ax, ax
    xchg ax, [RDG_DCOMP]
    pop ds
    add [rw_acomp], ax
.r:
    pop ax
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; rw_blitd - the lines rdg_renderd changed onto the content, a band a run of
; them, the lock held and the clip armed. Nothing changed, nothing drawn.
rw_blitd:
    mov bx, [rw_win]
    call OSAPI_WM_DISPLAY           ; DH = bits per pixel
    cmp dh, 1
    jbe .mono
    cmp byte [rw_pfmt], 4           ; (the planes: BLITP's bands below)
    je .mono
    mov al, CWHITE                  ; the dither on a colour display
    mov ah, CBLACK
    call OSAPI_GFX_BLIT1_PEN
.mono:
    xor dl, dl                      ; DL = the line
.find:
    call rw_dchg
    jnz .run
.nx:
    inc dl
    cmp dl, RW_CONT_H
    jb .find
    ret
.run:
    mov dh, dl                      ; DH = the run's first line
.ext:
    inc dl
    cmp dl, RW_CONT_H
    jae .put
    call rw_dchg
    jnz .ext
.put:                               ; DH = the run's first line, DL past it
    push dx
    mov bl, dh
    xor bh, bh
    mov ax, 20
    mul bx
    mov si, ax                      ; SI = the run in the dithered buffer
    push bx
    mov bx, [rw_win]
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    pop bx
    add bx, dx                      ; BX = the run's row on the screen
    pop dx
    push dx
    mov cl, dl
    sub cl, dh
    xor ch, ch
    mov dx, cx                      ; DX = its lines
    mov es, [rw_pseg]
    cmp byte [rw_pfmt], 4
    je .planes
    mov bp, 20
    mov cx, RW_CONT_W
    call OSAPI_GFX_BLIT1
    pop dx
    jmp short .nx
.planes:                            ; the four planes' band, from the same
    shl si, 1                       ;   first line: 80 bytes a line, not 20
    shl si, 1
    mov di, 4000h | 20              ; walk an armed clip; 20 bytes a plane
    mov bp, 80
    mov cx, RW_CONT_W
    call OSAPI_GFX_BLITP
    pop dx
    jnc .nx
    mov byte [rw_nobp], 1           ; refused (a straddle, kern_small): the
    mov byte [rw_pfmt], 0           ;   next draw is dithered, every line
    ret

; rw_planes - the lines rdg_renderd changed, from [rw_fseg]'s two shade planes
; into [rw_pseg] as BLITP's four: NOT bit 1 three times, then NOT bit 0 - the
; desktop's 15, 7, 8 and 0 for shades 0-3
rw_planes:
    mov es, [rw_pseg]
    push ds
    mov ds, [rw_fseg]
    xor dx, dx                      ; DL = the line
    xor si, si
    xor di, di
.l:
    call rw_dchg
    jz .n
    push si
    push di
    add si, 20                      ; bit 1, NOT, to planes 0-2
    mov cx, 10
.b1:
    lodsw
    not ax
    mov [es:di + 20], ax
    mov [es:di + 40], ax
    stosw
    loop .b1
    add di, 40                      ; bit 0, NOT, to plane 3
    sub si, 40
    mov cx, 10
.b0:
    lodsw
    not ax
    stosw
    loop .b0
    pop di
    pop si
.n:
    add si, 40
    add di, 80
    inc dl
    cmp dl, RW_CONT_H
    jb .l
    pop ds
    ret

; rw_dmgclaim - on the UI task, once: rdg_renderd's memory, when there is
; room (without it every frame is composed whole)
rw_dmgclaim:
    cmp word [rdg_dmgseg], 0
    jne .r
    mov ax, (RDG_DMG_SIZE + 1023) / 1024
    call OSAPI_MEM_CLAIM
    jc .r
    mov [rdg_dmgseg], dx
.r:
    ret

; rw_dmgfull - the next rdg_renderd composes and writes every line
; rw_spdset - [rw_speed] into the clocks: rw_spdk, the Game Boy's frames
; 256ths at a time, and rw_pertick, the window's frames a BIOS tick x 256.
; 100% is 256 and RW_PERTICK, as ever. Kept: all
rw_spdset:
    push ax
    push bx
    push dx
    mov bl, [rw_speed]
    xor bh, bh
    shl bx, 1
    mov ax, [rw_spdtab + bx]
    mov [rw_spdk], ax
    mov dx, RW_PERTICK
    mul dx
    mov al, ah
    mov ah, dl
    mov [rw_pertick], ax
    pop dx
    pop bx
    pop ax
    ret

; rw_spdnext - Machine > Speed's next (F4's too, on the worker or in the
; bracket): the clocks, the game's record, the menu. The file is the UI
; task's (rw_spdmenu, rw_onwake)
rw_spdnext:
    mov al, [rw_speed]
    inc ax
    cmp al, RW_SPEEDS
    jb .s
    xor al, al
.s:
    mov [rw_speed], al
    call rw_spdset
    call rw_cfgrec
    jmp rw_mgrey

; rw_spdmenu / rw_sndmenu - Machine > Speed and Sound, on the UI task: the
; next, and REALDMG.CFG written - each kept for the game it was chosen in
rw_spdmenu:
    call rw_spdnext
    jmp rw_cfgsave

; rw_spdpick - CPU > Speed's item AL (0-3): chosen, and kept, on the UI task
rw_spdpick:
    mov [rw_speed], al
    call rw_spdset
    call rw_cfgrec
    call rw_mgrey
    jmp rw_cfgsave
rw_sndmenu:
    call rw_sndnext
    jmp rw_cfgsave

; rw_sndnext - Machine > Sound's next (F9's too, on the worker or in the
; bracket): the game's record, the menu. The file is the UI task's
rw_sndnext:
    mov al, [rw_sndm]
    inc ax
    cmp al, 3
    jb .s
    xor al, al
.s:
    mov [rw_sndm], al
    call rw_cfgrec
    jmp rw_mgrey

; rw_spdgame - a game loaded: its header's key (014Dh, 014Eh, as its
; profile's name), and its Speed and Sound if REALDMG.CFG has them - a game
; it does not have keeps what was chosen before it, kept at its first change
rw_spdgame:
    push si
    push ds
    mov ds, [cs:rdg_viewseg]
    mov al, [014Dh]
    mov si, [014Eh]
    pop ds
    mov [rw_gkey], al
    mov [rw_gkey + 1], si
    mov byte [rw_gkon], 1
    call rw_cfgfind                 ; SI = its entry, CF = 1 none
    jc .r
%ifdef RDG_LOSSY
    mov al, [si + 3]                ; bits 5 and 6: Two Shades and Scroll by
    mov cl, 5                       ;   8/4/2, bits 6-7 (rdg_lossy) - patched here, before
    shr al, cl                      ;   the worker is hired
    and al, RDG_LBW | RDG_LCOARSE
    mov [rdg_lossy], al
    call rdg_lpatch
%endif
    mov al, [si + 3]
    mov ah, al
    and al, 7
    cmp al, RW_SPEEDS               ; (a file from when there were five:
    jb .sp                          ;   60% and 50%, 3 and 4, are 50% now)
    cmp al, 4
    ja .r
    mov al, RW_SPEEDS - 1
.sp:
    mov cl, 3
    shr ah, cl
    and ah, 3
    cmp ah, 3
    jae .r
    mov [rw_speed], al
    mov [rw_sndm], ah
    call rw_spdset
    call rw_mgrey
.r:
    pop si
    ret

; rw_cfgfind - SI = the game's entry in rw_cfg's table, or CF = 1
rw_cfgfind:
    push cx
    mov cl, [rw_cfg + 4]
    xor ch, ch
    mov si, rw_cfg + 8
    jcxz .no
.f:
    mov al, [si]
    cmp al, [rw_gkey]
    jne .n
    mov ax, [si + 1]
    cmp ax, [rw_gkey + 1]
    je .y
.n:
    add si, 4
    loop .f
.no:
    stc
    pop cx
    ret
.y:
    clc
    pop cx
    ret

; rw_cfgrec - the game's Speed and Sound to the front of rw_cfg's table (its
; old entry out, or the oldest when it is full). No game, nothing. Kept: all
rw_cfgrec:
    cmp byte [rw_gkon], 0
    je .r0
    push ax
    push cx
    push si
    push di
    push es
    push ds
    pop es
    call rw_cfgfind                 ; SI = its entry: the ones before it
    jnc .mv                         ;   move up over it
    mov al, [rw_cfg + 4]            ; none: every one moves, the last off the
    cmp al, RW_CFGGN                ;   end when it is full
    jae .full
    inc byte [rw_cfg + 4]
.full:
    mov si, rw_cfg + 8 + (RW_CFGGN - 1) * 4
.mv:
    mov cx, si                      ; CX = the bytes before it
    sub cx, rw_cfg + 8
    lea di, [si + 3]                ; backwards: each up one entry
    lea si, [si - 1]
    std
    rep movsb
    cld
    mov al, [rw_gkey]
    mov [rw_cfg + 8], al
    mov ax, [rw_gkey + 1]
    mov [rw_cfg + 9], ax
    mov al, [rw_sndm]
    mov cl, 3
    shl al, cl
    or al, [rw_speed]
%ifdef RDG_LOSSY
    mov ah, [rdg_lossy]             ; (bits 5-7: Screen's two)
    mov cl, 5
    shl ah, cl
    or al, ah
%endif
    mov [rw_cfg + 11], al
    pop es
    pop di
    pop si
    pop cx
    pop ax
.r0:
    ret

; rw_iltoggle - Screen > Interlace (REALDMG-SPEC 13.2), Off -> On ->
; Scrolling: a draw composes the lines of one parity, the next the other's,
; for twice the pictures a second at a price in looks - always, or only
; while the picture scrolls; off, every line again. Either way the next draw
; is whole
rw_iltoggle:
    mov al, [rdg_ilace]              ; Off -> On -> Scrolling -> Doubled
    inc ax                          ;   -> Off
    cmp al, 4
    jb .s
    xor al, al
.s:
    mov [rdg_ilace], al
    call rw_dmgfull
    jmp rw_mgrey

rw_dmgfull:
    push es
    push ax
    mov ax, [rdg_dmgseg]
    or ax, ax
    jz .r
    mov es, ax
    mov byte [es:RDG_DFULL], 1
.r:
    pop ax
    pop es
    ret

; =============================================================================
; The window
; =============================================================================

; rw_paint - SI = the window, the lock held, the clip armed
rw_paint:
    call rw_mgrey
    push si
    cmp byte [rw_ntc], 0            ; the notice, before the game starts
    jne .ntc
    mov byte [rw_nobp], 0           ; a paint asks BLITP again: the window
    cmp byte [rw_shown], 0          ;   may have moved off the straddle
    je .blank
    call rw_blit
    jmp short .about
.blank:
    call rw_fillwhite
    mov si, rw_s_open1
    cmp byte [rw_state], 2
    jne .t
    mov si, rw_s_stop1
.t:
    mov dx, 60
    call rw_line
    mov si, rw_s_open2
    cmp byte [rw_state], 2
    jne .t2
    mov si, rw_s_stop2
.t2:
    mov dx, 74
    call rw_line
    jmp short .about
.ntc:
    call rw_fillwhite
    mov si, rw_s_slow1
    mov dx, 52
    call rw_line
    mov si, rw_s_slow2
    mov dx, 66
    call rw_line
    mov si, rw_s_slow3
    mov dx, 80
    call rw_line
.about:
    pop si
    cmp byte [rw_abon], 0
    je .r
    push si
    mov bx, si
    mov si, rw_ablines
    call os88ui_about_d
    pop si
    cmp byte [rw_spawned], 0        ; a refused hire is asked again here
    jne .r
    cmp byte [rw_state], 1
    jne .r
    call rw_hire
.r:
    ret

; rw_line - SI = a string, DX = its row in the content, centred
rw_line:
    push ax
    push bx
    push cx
    push dx
    mov cx, dx
    mov bx, [rw_win]
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    add dx, cx                      ; DX = the row
    mov bx, ax
    call OSAPI_FONT_WIDTH           ; AX = the string's width
    mov cx, RW_CONT_W
    sub cx, ax
    sar cx, 1
    add cx, bx                      ; CX = x
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; rw_fillwhite - the content white
rw_fillwhite:
    push ax
    push bx
    push cx
    push dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov bx, [rw_win]
    call OSAPI_WM_CONTENT
    mov bx, dx
    mov cx, ax
    add cx, RW_CONT_W - 1
    add dx, RW_CONT_H - 1
    call OSAPI_GFX_FILL
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; rw_repaint - SI = the window, the lock held: everything, ourselves
rw_repaint:
    push bx
    mov bx, si
    call OSAPI_WM_CLIP_SET
    pop bx
    jc .r
    jmp rw_paint
.r:
    ret

; rw_oncmd - AH = the menu, AL = the item, SI = the window, the lock held
rw_oncmd:
    push ax
    call rw_abdismiss
    pop ax
    cmp ah, 1
    je .scr
    cmp ah, 2
    je .cpu
    ja .st
    cmp al, RW_MLOAD                ; MACHINE
    je rw_open
    cmp al, RW_MRESET
    je .reset
    cmp al, RW_MPAUSE
    je .p
    cmp al, RW_MFS
    je rw_fsenter
    cmp al, RW_MSND
    je rw_sndmenu
    cmp al, RW_MSTATS
    je rw_stats
    cmp al, RW_MLOG
    je rw_logtoggle
    cmp al, RW_MCTL
    je rw_ctlopen
    ret
.reset:
    cmp byte [rw_rpmode], 0         ; (a replay is one machine's from its
    jne .rpb                        ;   start)
    mov byte [rw_resetq], 1         ; Reset: the worker's to do
    ret
.rpb:
    mov si, rw_s_rpbusy
    jmp rw_note
.p:
    xor byte [rw_pause], 1
    mov al, RLE_PAUSE
    jnz .pe
    mov al, RLE_GO
.pe:
    call rw_logevt
    jmp rw_mgrey
.scr:
    cmp al, RW_SIL                  ; SCREEN
    je rw_iltoggle
%ifdef RDG_LOSSY
    mov ah, al
    mov al, RDG_LBW
    cmp ah, RW_SBW
    je rw_losspost
    mov al, RDG_LCOARSE
    cmp ah, RW_SCO
    je rw_losspost
%endif
    ret
.cpu:
    cmp al, RW_CJOFF                ; CPU
    jbe .mode
    cmp al, RW_CPRF
    je rw_prftoggle
    cmp al, RW_CBIG
    je rw_bigtoggle
    cmp al, RW_CSPD
    jb .r
    sub al, RW_CSPD                 ; a speed: as F4 would step to it
    jmp rw_spdpick
.mode:                              ; a mode: rw_jitto's
    mov bl, al
    xor bh, bh
    mov al, [rw_jmenu + bx]
    jmp rw_jitto
.r:
    ret
.st:
    cmp al, RW_SSLOTS               ; SAVE STATE: a slot...
    jb .slot
    cmp al, RW_SSV
    je rw_stsave
    cmp al, RW_SLD
    je rw_stload
    cmp al, RW_SREC
    je rw_rprec
    cmp al, RW_SPLY
    je rw_rpplay
    ret
.slot:
    mov ah, al                      ; ...as its key would choose it
    add ah, 2
    jmp rw_stkey

; rw_stats - Machine > Show Stats, F1's: the speed line on or off
rw_stats:
    xor byte [rw_ovl], 1
    mov byte [rw_ovlnew], 1
    mov al, RLE_F1
    call rw_logevt
    call rw_dmgfull                 ; the line it covered, or uncovered, is
    jmp rw_mgrey                    ;   not a change the picture made

; rw_onkey - AX = a typed key, SI = the window, the lock held: Alt+Enter is
; Machine > Full Screen (SPEC.md 11.2.1.1); the game reads the keys held,
; not the keys typed, so nothing else here is ours
rw_onkey:
    cmp ax, KEY_ALTENTER
    jne .r
    call rw_abdismiss
    jmp rw_fsenter
.r:
    jmp rw_klat                     ; a press a draw would hide (rw_keys)

rw_about:
    mov byte [rw_abon], 1
    mov bx, si
    mov si, rw_ablines
    jmp os88ui_about

rw_abdismiss:
    mov bx, rw_abon
    mov ax, rw_repaint
    jmp os88ui_abdismiss

rw_tpl:
    dw 160, 60, RW_CONT_W + 2, RW_CONT_H + TITLE_H + 2
    dw rw_ttl, rw_paint, rw_onkey, rw_abdismiss

    OS88_MENUSET rw_menus, rw_ttl, rw_oncmd
        OS88_MENU rw_m_mach, rw_i_mach, 10
        OS88_MENU rw_m_scr, rw_i_scr, RW_SCRN
        OS88_MENU rw_m_cpu, rw_i_cpu, RW_CPUN
        OS88_MENU rw_m_st, rw_i_st, 11
    OS88_MENUSET_END rw_menus

; THE MENUS. An item is RW_MI's: MENU_DIS, a MARK - '*' on the setting that
; is chosen, a space on the others (the kernel's menu has no check mark, and
; the font is printable ASCII) - the text, and its key right-aligned to
; RW_MW glyphs. The list names label+1 for the item and label itself to grey
; it; rw_mgrey writes the marks and the choice (rw_mitem). rw_note says an
; item without its mark or key.
RW_MW           equ MENU_MAXCH      ; glyphs an item, its mark and key with it
%macro RW_MI 3
%1: db MENU_DIS, ' ', %2
%strlen %%a %2
%strlen %%b %3
    times RW_MW - 1 - %%a - %%b db ' '  ; (fails the build when it is too long)
%if %%b
    db %3
%endif
    db 0
%endmacro

%ifdef PKG_MEGA
rw_unttl:       db '(Un)'           ; the title under CPU_F_PM runs on into
%endif                              ;   rw_ttl's: '(Un)RealDMG'
rw_ttl:         db 'RealDMG', 0
rw_m_mach:      db 'Machine', 0
rw_i_mach:      dw rw_it_load + 1, rw_it_reset + 1, rw_it_pause + 1, rw_it_fs
                dw rw_it_sep, rw_it_snd0 + 1, rw_it_stats + 1
                dw rw_it_log + 1, rw_it_sep, rw_it_ctl + 1
RW_MLOAD        equ 0               ; (Machine's items)
RW_MRESET       equ 1
RW_MPAUSE       equ 2
RW_MFS          equ 3
RW_MSND         equ 5
RW_MSTATS       equ 6
RW_MLOG         equ 7
RW_MCTL         equ 9
rw_m_scr:       db 'Screen', 0      ; SCREEN: what the picture may leave out
rw_i_scr:       dw rw_it_il0 + 1    ;   for speed (REALDMG-SPEC 13.2, 13.4)
%ifdef RDG_LOSSY
                dw rw_it_sep, rw_it_bw + 1, rw_it_co0 + 1
RW_SCRN         equ 4
%else
RW_SCRN         equ 1
%endif
RW_SIL          equ 0               ; (Screen's items)
RW_SBW          equ 2
RW_SCO          equ 3
rw_m_cpu:       db 'CPU', 0
%ifdef RDG_AOT                      ; a precompiled cartridge's CPU: the one
rw_i_cpu:       dw rw_it_jaot, rw_it_sep, rw_it_spd0 + 1 ;   mode, said, and
                dw rw_it_spd1 + 1, rw_it_spd2 + 1, rw_it_spd3 + 1 ; Speed
RW_CJOFF        equ 0               ; (no item chooses a mode: item 0 is
RW_CPRF         equ 0FEh            ;   greyed, and these two are none)
RW_CBIG         equ 0FFh
RW_CSPD         equ 2
RW_CPUN         equ 6               ; (the items: a count of its own, or the
                                    ;   menu reads five words past the list)
%else
rw_i_cpu:       dw rw_it_jon, rw_it_jst, rw_it_joff + 1, rw_it_sep
                dw rw_it_prf, rw_it_big, rw_it_sep, rw_it_spd0 + 1
                dw rw_it_spd1 + 1, rw_it_spd2 + 1, rw_it_spd3 + 1
RW_CJON         equ 0               ; (CPU's: the three modes...
RW_CJST         equ 1
RW_CJOFF        equ 2
RW_CPRF         equ 4               ;   ...Profile for Static and the cache,
RW_CBIG         equ 5
RW_CSPD         equ 7               ;   and Speed's four)
RW_CPUN         equ 11
%endif
rw_m_st:        db 'Save State', 0
rw_i_st:        dw rw_it_slot1 + 1, rw_it_slot2 + 1, rw_it_slot3 + 1
                dw rw_it_slot4 + 1, rw_it_slot5 + 1, rw_it_slot6 + 1, rw_it_sep
                dw rw_it_sv, rw_it_ld, rw_it_rec, rw_it_ply
RW_SSLOTS       equ 6               ; (Save State's: slots 1-6 - 7-9 by key
RW_SSV          equ 7               ;   only, a 14-item menu being taller
RW_SLD          equ 8               ;   than CGA's 200 lines - then Save,
RW_SREC         equ 9               ;   Load, and the replays)
RW_SPLY         equ 10
rw_it_sep:      db MENU_DIS, '----------------------', 0
    RW_MI rw_it_load, 'Load Game...', ''
    RW_MI rw_it_reset, 'Reset', ''
    RW_MI rw_it_pause, 'Pause', ''
    RW_MI rw_it_fs, 'Full Screen', 'Alt+Ent'
    RW_MI rw_it_snd0, 'Sound: On', 'F9'
    RW_MI rw_it_snd1, 'Sound: At 100% only', 'F9'
    RW_MI rw_it_snd2, 'Sound: Off', 'F9'
    RW_MI rw_it_stats, 'Show Stats', 'F1'
    RW_MI rw_it_il0, 'Interlace: Off', 'F2'
    RW_MI rw_it_il1, 'Interlace: On', 'F2'
    RW_MI rw_it_il2, 'Interlace: Scrolling', 'F2'
    RW_MI rw_it_il3, 'Interlace: Doubled', 'F2'
    RW_MI rw_it_log, 'Log', 'F12'
    RW_MI rw_it_bw, 'Two Shades', 'F10'
    RW_MI rw_it_co0, 'Scroll: Smooth', 'F11'
    RW_MI rw_it_co1, 'Scroll: By 8', 'F11'
    RW_MI rw_it_co2, 'Scroll: By 4', 'F11'
    RW_MI rw_it_co3, 'Scroll: By 2', 'F11'
    RW_MI rw_it_ctl, 'Controls...', ''
    RW_MI rw_it_jon, 'Recompiler', 'F3'
%ifdef RDG_AOT
    RW_MI rw_it_jaot, 'Precompiled', ''
%endif
    RW_MI rw_it_jst, 'Static Recompiler', 'F3'
    RW_MI rw_it_joff, 'Interpreter', 'F3'
    RW_MI rw_it_prf, 'Profile for Static', ''
%ifdef PKG_MEGA
    RW_MI rw_it_big, 'Cache: All', ''   ; (the arm: every tier the machine
%else                                   ;   affords - both 64KB segments in
    RW_MI rw_it_big, 'Cache: 128K', ''  ;   real mode, the pool's under
%endif                                  ;   CPU_F_PM, 16.2.6)
    RW_MI rw_it_spd0, 'Speed: 100%', 'F4'
    RW_MI rw_it_spd1, 'Speed: 90%', 'F4'
    RW_MI rw_it_spd2, 'Speed: 75%', 'F4'
    RW_MI rw_it_spd3, 'Speed: 50%', 'F4'
    RW_MI rw_it_slot1, 'Slot 1', '1'
    RW_MI rw_it_slot2, 'Slot 2', '2'
    RW_MI rw_it_slot3, 'Slot 3', '3'
    RW_MI rw_it_slot4, 'Slot 4', '4'
    RW_MI rw_it_slot5, 'Slot 5', '5'
    RW_MI rw_it_slot6, 'Slot 6', '6'
    RW_MI rw_it_sv, 'Save State', 'F5'
    RW_MI rw_it_ld, 'Load State', 'F8'
    RW_MI rw_it_rec, 'Record Replay', 'F6'
    RW_MI rw_it_recs, 'Stop Recording', 'F6'
    RW_MI rw_it_ply, 'Play Replay', 'F7'
    RW_MI rw_it_plys, 'Stop Replay', 'F7'
rw_s_prfst:     db 'Static reads the profile', 0
rw_s_jroom:     db 'No room: Interpreter', 0
rw_s_bignx:     db 'Cache: for the next game', 0
rw_s_cfg:       db 'REALDMG.CFG', 0
rw_spdit:       dw rw_it_spd0, rw_it_spd1, rw_it_spd2, rw_it_spd3
rw_sndit:       dw rw_it_snd0, rw_it_snd1, rw_it_snd2
rw_ilit:        dw rw_it_il0, rw_it_il1, rw_it_il2, rw_it_il3
%ifdef RDG_LOSSY
rw_coit:        dw rw_it_co0, rw_it_co1, rw_it_co2, rw_it_co3
%endif
rw_jit:         dw rw_it_jon, rw_it_joff, rw_it_jst  ; (by rw_jitmode - 1)
rw_jmenu:       db 1, 3, 2          ; CPU's items 0-2: the mode each chooses
rw_spdtab:      dw 256, 230, 192, 128   ; Speed, 256ths of the Game Boy's

rw_s_fps:       db 'fps', 0
rw_s_pfree:     db 'Pacing: Free', 0
rw_s_pstdy:     db 'Pacing: Steady', 0
rw_s_jit:       db ' JIT', 0
rw_s_aot:       db ' AOT', 0
rw_s_open1:     db 'Machine > Load Game', 0
rw_s_open2:     db 'a Game Boy .GB', 0
rw_s_stop1:     db 'The game stopped:', 0
rw_s_slow1:     db 'Windowed is slow:', 0
rw_s_slow2:     db 'Alt+Enter for', 0
rw_s_slow3:     db 'Full Screen', 0
rw_s_stop2:     db 'an invalid opcode', 0
rw_s_big:       db 'REALDMG: needs more XMS', 0
rw_s_notrom:    db 'REALDMG: not a GB ROM', 0
rw_s_cgb:       db 'REALDMG: needs GB Color', 0
rw_s_mbc:       db 'REALDMG: MBC '
rw_s_mbcx:      db 'h unknown', 0
rw_s_room:      db 'REALDMG: no room for ROM', 0
rw_s_read:      db 'REALDMG: read failed', 0
%ifdef RDG_AOT
rw_s_pm:        db 'PRE: not on kern_mega', 0
rw_s_other:     db 'REALDMG: not this game', 0
rw_s_ameta:     db 'REALDMG: bad precompile', 0
rw_s_ano:       db 'REALDMG: no ', 0
%endif
rw_s_noram:     db 'REALDMG: no room for RAM', 0
rw_s_nosave:    db 'REALDMG: save failed', 0

rw_ablines:
    dw rw_ab1, rw_ab2, rw_ab3, rw_ab4, rw_ab5, rw_ab7, 0
rw_ab1:         db 'RealDMG', 0
rw_ab2:         db 'a Game Boy for os8088', 0
rw_ab3:         db 0
rw_ab4:         db 'Keys: Machine >', 0     ; (they are the user's now)
rw_ab5:         db 'Controls...', 0
rw_ab7:         db 'F1 = speed', 0

%include "rdgdefs.inc"
%define RDG_CSEL                            ; (rw_csel: REALDMG-SPEC 16.2.4)
%include "rdgsys.inc"
%include "rdgppu.inc"
%include "rwfsx.inc"
%include "rwkeys.inc"
%include "rdgcore.inc"                    ; (the recompiler is a part: rdgjitp.asm)
rw_jdtab:                               ; (rw_jdoff's, from rdgcore.inc)
    RDGC_JDTAB

%include "os88alt.inc"
%include "os88pit.inc"
%include "rwlog.inc"
%include "rwbank.inc"
%include "rwstate.inc"
%include "rwreplay.inc"
%include "rwfm.inc"
%include "rwmove.inc"                     ; the claims' relocation (A1)
%ifdef RDG_AOT
%include "rdgaot.inc"                     ; the list's search (REALDMG-SPEC 20.9)
%include "rwaot.inc"                      ; ...and the images' load (20.10)
%endif
%include "os88data.inc"

%define OS88UI_ABOUT
%define OS88UI_NOBTN
%define OS88UI_ABDISMISS
%include "os88ui.inc"

; THE PART TABLE (SPEC.md 20.12.3): the core, compressed on the disk, and the
; view, a zeroed carve that costs no disk at all
%ifdef RDG_AOT
; ...and a precompiled cartridge's (REALDMG-SPEC 20.10): the core is the one
; the host wrote code into, the block table and the meta with its list are
; in the carve, and every code image is two OP_SELF halves rw_aread reads
; into a claim of its own. The table is laid out for RWA_MAX images and
; tools/gbaot.py's `package` sets the count to the images there are
OS88_PARTS_BEGIN RWA_ROWS
    OS88_PART OP_SEG, OP_COMP                   ; 0  the core's first half
    OS88_PART OP_ASSET, OP_COMP                 ; 1  ...and its second
    OS88_PART OP_ASSET, OP_ZERO, 64             ; 2  the view
    OS88_PART OP_ASSET, OP_COMP                 ; 3  the block table
    OS88_PART OP_ASSET, OP_COMP                 ; 4  the meta and the list
%rep RWA_ROWS - RWA_CODE
    OS88_PART OP_SEG, OP_LAZY | OP_COMP | OP_SELF ; 5..  the relocations' and
%endrep                                         ;   the list's (made eager into
                                                ;   the carve by the host), the
                                                ;   code images, then the
                                                ;   ROM's 32KB pieces
OS88_PARTS_END
%else
OS88_PARTS_BEGIN 4
    OS88_PART OP_SEG, OP_COMP                   ; 0  the core's first half
    OS88_PART OP_ASSET, OP_COMP                 ; 1  ...and its second: one
                                                ;    part is at most 65,024
                                                ;    bytes (SPEC.md 20.12.11)
    OS88_PART OP_ASSET, OP_ZERO, 64             ; 2  the view
    OS88_PART OP_SEG, OP_LAZY | OP_COMP | OP_SELF ; 3  the recompiler: its
                                                ;    code and templates, read
                                                ;    by rw_jpart into its own
                                                ;    claim (REALDMG-SPEC 16.9)
OS88_PARTS_END
%endif
    align 2, db 0
    OS88_BSS OP_BSS + RW_BSS
    OS88_IMAGE_END
