; =============================================================================
; os8088 - apps/realdmg/rdgdefs.inc
;
; RealDMG's shared layout: the VIEW segment, the state block inside it, and
; the core's exit codes. Included by the generated core (build/realdmg/
; rdgcore.asm, written by apps/realdmg/rdggen.py), by rdgsys.inc (the events,
; I/O, MBC and timers) and by every host of the two - the package and the
; boot-sector harness in tests/rdgcore/. docs/REALDMG-SPEC.md is the contract
; (this table is its list of fields) and docs/plans/REALDMG-PLAN.md section 4
; the design; this file is the numbers.
;
; THE VIEW is one 64KB claim whose OFFSET IS THE GAME BOY ADDRESS for every
; region that never moves:
;
;   0000-3FFF  ROM bank 0, copied once at load
;   4000-7FFF  NOT the switchable bank (that is ROMX, a segment of its own,
;              because a bank switch must be a pointer change and never a
;              copy - Super Mario Land switches 7.8 times a frame). This
;              window is RealDMG's own: the state block, the flag tables,
;              the register log, the serial buffer.
;   8000-FFFF  the Game Boy's RAM side as it is: VRAM, cartridge RAM, WRAM,
;              echo, OAM, I/O registers, HRAM and IE at FFFF
;
; While the core runs, SS = the view and SP = the Game Boy's SP, so PUSH,
; POP, CALL and RET are the 8086's own instructions on the Game Boy's stack,
; and every RAM access is [ss:reg] with the Game Boy address as the offset.
; =============================================================================

; --- the view's own window ---------------------------------------------------
RDG_STATE        equ 0x4000          ; the state block (below)
RDG_F2AH         equ 0x4100          ; 256: Game Boy F -> AH image
RDG_AH2F         equ 0x4200          ; 256: AH image -> Game Boy F
RDG_RLOG         equ 0x4300          ; the register log, RDG_RLOG_MAX x 4 bytes
RDG_RLOG_MAX     equ 192
RDG_VLN          equ 0x4600          ; word: entries on RDG_VLIST (vr_new)
RDG_VLIST        equ 0x4610          ; RDG_VLMAX: VRAM block PAIRS marked for
RDG_VLMAX        equ 64              ;   the first time since the damage scan
                                    ;   (REALDMG-SPEC 13.2.2); full, it sweeps
RDG_SERBUF       equ 0x4700          ; 256: bytes the game sent on its link port
RDG_SNAP         equ 0x4800          ; 16: LCDC..WX as the frame began (4.4)
S_JITSEGA       equ 0x4810          ; word: the cache's first segment, and
S_JITSEGB       equ 0x4812          ;   its second or 0 (REALDMG-SPEC 16.2.1):
                                    ;   S_JITSEG is the one being written
S_JITLIM        equ 0x4814          ; word: RT_SQUEEZE builds (the harness):
                                    ;   each segment's code ends here, or 0
S_JITSW         equ 0x4816          ; word: moves on into the second (stat)
S_VTMP          equ 0x4818          ; word: vr_wr's BX (REALDMG-SPEC 13.2.2)
RDG_VALL         equ 0x481A          ; byte: VRAM written past the marks (the
                                    ;   copy idiom, cpy_ff): compare it all
S_HMASK         equ 0x481C          ; byte: the block table's mask, entries /
                                    ;   256 - 1: 31 for 64KB, 7 for 16KB (16.3)
S_BTMP          equ 0x481E          ; word: AX while mbc_set stamps a bank's
                                    ;   frame (10.3) - the core never pushes,
                                    ;   SP being the Game Boy's (a game that
                                    ;   POPs data out of ROM had its bank 0
                                    ;   copy written under it), and S_TMP is
                                    ;   LD (nn),A's HL there
S_STATLN        equ 0x4820          ; byte: the line whose mode 0 S_TSTAT is,
                                    ;   or FFh - rdg_statsch's, for rdg_statev's
                                    ;   step to the next line (section 6)...
S_STATT         equ 0x4822          ; dword: ...trusted while S_TSTAT is still
                                    ;   this: anything else that moves it (a
                                    ;   state loaded, the LCD off) is a miss
S_JITGL         equ 0x4826          ; word: the gap list being written's offset
                                    ;   in the templates' table (rt_tab), and...
S_JITGN         equ 0x4828          ; word: ...its count (REALDMG-SPEC 16.2.2):
                                    ;   the cache segments' list, or the core's
S_JITTIER       equ 0x482A          ; byte: the tier being written - 0 the
                                    ;   first cache segment, 1 the CORE's
                                    ;   holes, 2 the second cache segment
S_JITNOC        equ 0x482B          ; byte: the host's - 1 = no core tier
                                    ;   (the harness's A/B, JF_NOCORE)
S_JITUSED       equ 0x482C          ; byte: the tiers written since the last
                                    ;   flush, a bit each (16.2.3)
S_JITGNX        equ 0x482D          ; word: the last tier generation given -
                                    ;   a tier's own is the word at its
                                    ;   segment's FFF0h (16.2.3)
S_JITRCY        equ 0x482F          ; word: tiers recycled (stat; each one a
                                    ;   flush in S_JITFL as well)
S_JITLN         equ 0x4831          ; word: the link log's bytes used (16.2.3):
                                    ;   4 an entry, RT_LOGB at most
RDG_FREE         equ 0x4833          ; the rest of the window is unassigned,
                                    ; but for rdgppu.inc's 4900-66FF and:
S_SQ3           equ 0x48F0          ; 8: the WAVE channel as the FM chip plays
                                    ;   it (REALDMG-SPEC 17.1) - S_SQ1's
                                    ;   layout, its volume NR32's code and no
                                    ;   envelope - and...
S_SQ4           equ 0x48F8          ; 8: ...the NOISE channel, a square's
                                    ;   record exactly (NR41-NR44 sit as
                                    ;   NRx1-NRx4 do). Out of the state block,
                                    ;   which is full; a save state carries
                                    ;   both (ST_SQX)
RDG_VMARK        equ 0x7200          ; 512: a VRAM 16-byte block written since
                                    ;   the damage scan last looked (vr_wr)
RDG_STKEEP       equ 0x7400          ; 1,024: the Game Boy's 8000-83FF, kept
                                    ;   while a state's record is read in, in
                                    ;   case the file ran on past it
RDG_STREC        equ 0x7800          ; 2,048: a SAVE STATE's record, composed
                                    ;   here and read back here (rdgsys's
                                    ;   rdg_stcompose, REALDMG-SPEC 10.4) - it
                                    ;   ends where the Game Boy's 8000-FFFF
                                    ;   begins, the file's other part
JIT_BUF         equ 0x6700          ; 256: the Game Boy code being translated
RDG_BTAB         equ 0x6800          ; 512 words: a ROM bank's segment, or 0
RDG_BUSE         equ 0x6C00          ; 512 words: the frame (S_FRAMES) it was
                                    ;   last switched to, the cache's
                                    ;   eviction (REALDMG-SPEC 10.3)
                                    ; (7000-711F: rdgppu.inc's PPU_LSCR)
JIT_HASH        equ 0x0000          ; 16,384, in a SEGMENT OF ITS OWN
                                    ;   (S_JITHSEG): the block table (tr_enter,
                                    ;   REALDMG-SPEC 16.3). The cache's 64KB
                                    ;   is code but for the tails' slots
JIT_HASHN       equ 8192                ; the block table's entries AT MOST: 64KB
JIT_HMMAX       equ JIT_HASHN / 256 - 1 ; (a slot is PC-folded, AND this x 256:
                                        ;   S_HMASK the table's own, 31 or 7)
RDG_JITHOT       equ 64              ; entries before a PC is translated

; --- a save state's record, at RDG_STREC (REALDMG-SPEC 10.4) -----------------
ST_MAGIC        equ 0               ; 4: 'RDST'
ST_VER          equ 4               ; byte: ST_VERSION
ST_VERSION      equ 1
ST_RAMKB        equ 5               ; byte: the RAM part's KB - 0, 8 or 32
ST_ROMID        equ 8               ; 28: the ROM's header, 0134h-014Fh
ST_BLOCK        equ 30h             ; 256: the state block as it was - only
                                    ;   the Game Boy's fields come back
                                    ;   (rdg_stapply's table); the rest is the
                                    ;   host's and stays as it is
ST_SNAP         equ 130h            ; 16: RDG_SNAP
ST_RLOG         equ 140h            ; 768: RDG_RLOG
ST_SQX          equ 440h            ; 16: S_SQ3 and S_SQ4
ST_RECSZ        equ 2048            ; (the rest zero)

; --- the state block, at RDG_STATE --------------------------------------------
; Registers the core keeps in 8086 registers while it runs, saved here when
; it leaves (rdgsys's event entry) and loaded from here when it resumes.
S_AF            equ RDG_STATE+0x00   ; word: AL = A, AH = F as LAHF lays it out
S_BC            equ RDG_STATE+0x02   ; word: CH = B, CL = C
S_DE            equ RDG_STATE+0x04   ; word: D:E (DI while running)
S_HL            equ RDG_STATE+0x06   ; word: H:L (BX while running)
S_SP            equ RDG_STATE+0x08   ; word: the Game Boy's SP (SP while running)
S_PC            equ RDG_STATE+0x0A   ; word: the Game Boy's PC (SI while running)
S_BP            equ RDG_STATE+0x0C   ; word: M-cycles to the next event (BP)
S_HSP           equ RDG_STATE+0x0E   ; word: the HOST's SP, saved by rdg_run
S_HSS           equ RDG_STATE+0x10   ; word: the host's SS
S_VIEW          equ RDG_STATE+0x12   ; word: this segment
S_ROMX          equ RDG_STATE+0x14   ; word: segment whose 4000-7FFF is the bank
S_ROMSEG        equ RDG_STATE+0x16   ; word: the whole ROM image (bank n at +n*0x400)
; far vectors the core jumps through: offset, then segment
S_EVVEC         equ RDG_STATE+0x18   ; dword: rdgsys event entry
S_IORVEC        equ RDG_STATE+0x1C   ; dword: I/O register READ  (DX = address)
S_IOWVEC        equ RDG_STATE+0x20   ; dword: I/O register WRITE (DX = address)
S_MBCVEC        equ RDG_STATE+0x24   ; dword: cartridge WRITE    (DX = address)
S_RETFAR        equ RDG_STATE+0x28   ; dword: where an I/O read resumes: the
S_RET           equ S_RETFAR        ;   offset is stored per call, the
                                    ;   segment is the core's
S_DISP          equ RDG_STATE+0x2C   ; dword: the core's dispatch stub
S_WVAL          equ RDG_STATE+0x30   ; byte: the value a write helper stores
S_IME           equ RDG_STATE+0x31   ; byte: interrupt master enable
S_HALT          equ RDG_STATE+0x32   ; byte: set by HALT for the event entry
S_EXIT          equ RDG_STATE+0x33   ; byte: why rdg_run returned (RDGX_*)
S_IDLE          equ RDG_STATE+0x34   ; byte: set by a spin-wait the core skipped
S_BANK          equ RDG_STATE+0x35   ; byte: the ROM bank in 4000-7FFF (low)
S_BANKHI        equ RDG_STATE+0x36   ; byte: MBC5 bank bit 8 / MBC1 upper bits
S_MBC           equ RDG_STATE+0x37   ; byte: RDGM_* cartridge type
S_NBANKS        equ RDG_STATE+0x38   ; word: 16KB banks in the ROM (power of 2)
S_RAMEN         equ RDG_STATE+0x3A   ; byte: cartridge RAM enabled
S_RAMBANK       equ RDG_STATE+0x3B   ; byte: cartridge RAM bank
S_MBCMODE       equ RDG_STATE+0x3C   ; byte: MBC1 banking mode
S_JOY           equ RDG_STATE+0x3D   ; byte: buttons, 1 = held: Rt Lf Up Dn A B Se St
S_FRAMES        equ RDG_STATE+0x3E   ; word: VBlanks since load
; time: 32-bit counts of M-cycles since load
S_TNEXT         equ RDG_STATE+0x40   ; dword: when BP reaches zero
S_TFRAME        equ RDG_STATE+0x44   ; dword: when the current frame's line 0 began
S_TVBL          equ RDG_STATE+0x48   ; dword: the next VBlank
S_TSTAT         equ RDG_STATE+0x4C   ; dword: the next STAT interrupt, or far
S_TTIMA         equ RDG_STATE+0x50   ; dword: the next TIMA overflow, or far
S_TDIV          equ RDG_STATE+0x54   ; dword: when DIV last read zero
S_TTBASE        equ RDG_STATE+0x58   ; dword: when TIMA last held S_TIMA0
S_TSTOP         equ RDG_STATE+0x5C   ; dword: the host's budget ends here
S_TIMA0         equ RDG_STATE+0x60   ; byte: TIMA at S_TTBASE
S_LCDON         equ RDG_STATE+0x61   ; byte: LCDC bit 7 as the PPU last saw it
S_SNAPOK        equ RDG_STATE+0x62   ; byte: RDG_SNAP holds this frame's start
S_RLOGN         equ RDG_STATE+0x63   ; byte: entries in RDG_RLOG this frame
S_SERN          equ RDG_STATE+0x64   ; byte: bytes in RDG_SERBUF
S_BADOP         equ RDG_STATE+0x65   ; byte: the opcode that stopped the core
S_RAMSEG        equ RDG_STATE+0x66   ; word: cartridge RAM banks, or 0
S_RAMBANKS      equ RDG_STATE+0x68   ; byte: 8KB RAM banks the cartridge has
S_IDLECNT       equ RDG_STATE+0x69   ; byte: spin-waits skipped this frame (stat)
S_HOSTDS        equ RDG_STATE+0x6A   ; word: the host's DS, for rdgsys
S_TMP           equ RDG_STATE+0x6C   ; word: scratch for the core's slow paths
S_RAMCUR        equ RDG_STATE+0x6E   ; byte: what A000-BFFF holds - a RAM bank,
                                    ;   or 88h-8Ch an MBC3 clock register
S_RTC           equ RDG_STATE+0x70   ; 16: the MBC3 clock (REALDMG-SPEC 10.2)
RTC_LIVE        equ 0               ; 5: seconds, minutes, hours, day, DH
RTC_LAT         equ 5               ; 5: the same, as last latched
RTC_ACC         equ 10              ; word: a second's fraction, of 65,536
RTC_LST         equ 12              ; byte: the last byte written to 6000
RTC_ON          equ 13              ; byte: the cartridge has the clock
S_SKIPPED       equ RDG_STATE+0x80   ; dword: M-cycles HALT and spin-waits skipped
S_BUSY          equ RDG_STATE+0x84   ; dword: M-cycles the core executed
S_IDLEPC        equ RDG_STATE+0x88   ; word: the last spin-wait's loop head
S_EVCOUNT       equ RDG_STATE+0x8A   ; word: event entries (stat)
S_IDLENO        equ RDG_STATE+0x8C   ; word: a loop head known NOT to spin
S_TRACEVEC      equ RDG_STATE+0x90   ; dword: RDG_TRACE builds' per-instruction hook
; scratch for rdgsys's push-free helpers (they run on the Game Boy's stack)
S_SAVAX         equ RDG_STATE+0x94
S_SAVBX         equ RDG_STATE+0x96
S_SAVCX         equ RDG_STATE+0x98
S_SAVSI         equ RDG_STATE+0x9A
S_SAVDI         equ RDG_STATE+0x9C
S_SAVDS         equ RDG_STATE+0x9E
S_SAVES         equ RDG_STATE+0xA0
S_NOW           equ RDG_STATE+0xA4   ; dword: "now", as a helper computed it
S_T32           equ RDG_STATE+0xA8   ; dword: scratch
S_SNAPTAG       equ RDG_STATE+0xAC   ; byte: the frame RDG_SNAP was taken in
S_TAKEINT       equ RDG_STATE+0xAD   ; byte: an IF/IE write asks for an interrupt now
S_RESVEC        equ RDG_STATE+0xB0   ; dword: where rdg_resume enters the core -
                                    ; RDG_DISPATCH, or rdg_trace when tracing
S_TNF           equ RDG_STATE+0xB4   ; word: S_TNEXT - S_TFRAME, low 16 bits, so
                                    ; the core reads the PPU's place as
                                    ; S_TNF - BP (mod RDG_FRAME) with no host
S_BMASK         equ RDG_STATE+0xB6   ; byte: ROM banks - 1, for the core's MBC path
; the recompiler's (REALDMG-SPEC section 16)
S_JITON         equ RDG_STATE+0xB7   ; byte: translate (0: interpret only)
S_JITVEC        equ RDG_STATE+0xB8   ; dword: rdg_jit, the translator's entry
S_JITOUT        equ RDG_STATE+0xBC   ; dword: a block to jump to, or offset 0;
                                    ;   the segment is the cache's, always
S_JITPTR        equ RDG_STATE+0xC0   ; word: where the next code goes
S_JITEND        equ RDG_STATE+0xC2   ; word: ...the end of that free range
S_JITGI         equ RDG_STATE+0xC4   ; word: the range's index
S_JITBK         equ RDG_STATE+0xC6   ; byte: the bank key of the PC asked for
S_JITFL         equ RDG_STATE+0xC8   ; word: flushes (stat)
S_JITNB         equ RDG_STATE+0xCA   ; word: blocks translated (stat)
S_JITSEG        equ RDG_STATE+0xCC   ; word: the cache's segment (16.2)
S_JITBY         equ RDG_STATE+0xCE   ; word: code bytes written, / 16 (stat)
S_JITADV        equ RDG_STATE+0xD0   ; word: free ranges moved on to (stat)
S_LINK          equ RDG_STATE+0xD2   ; word: tr_link's patch site
; the two square channels as the speaker hears them (REALDMG-SPEC section
; 17), 8 bytes each: on, volume (0-15), the envelope's and the length's
; accumulators (words, in 1/256ths of a step) and the length's steps left
S_SQ1           equ RDG_STATE+0xD4
S_SQ2           equ RDG_STATE+0xDC
S_BUDLEN        equ RDG_STATE+0xE4   ; word: rdg_run's budget, M-cycles
S_JITSLOT       equ RDG_STATE+0xE6   ; word: the table slot tr_miss's PC counted
                                    ;   in (its own, or that one's neighbour)
S_ATN           equ RDG_STATE+0xE8   ; word: the M-cycle a translated access's
                                    ;   I/O path is at (rw_io, rdggen.py)
S_RET2          equ RDG_STATE+0xEA   ; word: its S_RET while io_rd/io_wr run
S_DX2           equ RDG_STATE+0xEC   ; word: DX, kept across that
S_JITHSEG       equ RDG_STATE+0xEE   ; word: the block table's segment (16.3)
S_TMP2          equ RDG_STATE+0xF0   ; word: scratch beside S_TMP (S_TMP is ONE
S_TMP4          equ RDG_STATE+0xF2   ;   word: LD (nn),SP's S_TMP+2 and +4 were
S_TMP6          equ RDG_STATE+0xF4   ;   S_RAMCUR and the clock's seconds)
; Static (REALDMG-SPEC 16.8)
S_JITFIX        equ RDG_STATE+0xF6   ; byte: the block table is fixed - nothing
                                    ;   translated, counted or evicted
S_JITPRE        equ RDG_STATE+0xF7   ; byte: 1 preloading, 2 the cache filled
S_PRFSEG        equ RDG_STATE+0xF8   ; word: the profile's segment, or 0
S_PRFMASK       equ RDG_STATE+0xFA   ; word: its entries - 1 (a power of two)
S_PRFNEW        equ RDG_STATE+0xFC   ; word: entries added or weighed since 0
S_JITNBR        equ RDG_STATE+0xFE   ; word: blocks translated from ROM (stat:
                                    ;   a fixed table's must not move)
SQ_ON           equ 0
SQ_VOL          equ 1
SQ_ENV          equ 2
SQ_LACC         equ 4
SQ_LEN          equ 6

; --- the PPU's clock, in M-cycles ------------------------------------------
RDG_FAR_HI       equ 0x7FFF              ; "never": the high word of a far time
RDG_LINE         equ 114                 ; M-cycles a scan line
RDG_FRAME        equ 154*114             ; 17,556 M-cycles a frame
RDG_VBL          equ 144*114             ; line 144's start, frame-relative
RDG_HBL          equ 63                  ; mode 0 begins 63 M-cycles into a line

; --- why rdg_run returned (S_EXIT, and AX) ------------------------------------
RDGX_FRAME       equ 1               ; a VBlank began: the frame is ready
RDGX_BUDGET      equ 2               ; the host's budget ran out
RDGX_BADOP       equ 3               ; an opcode the SM83 does not have
RDGX_STOP        equ 4               ; STOP

; --- cartridge types (S_MBC) -------------------------------------------------
RDGM_NONE        equ 0
RDGM_MBC1        equ 1
RDGM_MBC2        equ 2
RDGM_MBC3        equ 3
RDGM_MBC5        equ 5

; --- the core's register map, for whoever reads a dump ------------------------
; AL = A  AH = F (LAHF image: ZF bit 6 = Z, AF bit 4 = H, CF bit 0 = C,
; bit 5 = N)  CX = BC  DI = DE  BX = HL  SI = PC (DS = its segment)
; SP = SP (SS = the view)  BP = M-cycles left  DX = dispatch (DL = 0)
RDGF_Z           equ 0x40
RDGF_N           equ 0x20
RDGF_H           equ 0x10
RDGF_C           equ 0x01
