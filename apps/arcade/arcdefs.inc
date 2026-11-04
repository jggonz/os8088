; =============================================================================
; os8088 - apps/arcade/arcdefs.inc - ARCADE's shared layout (ARCADE-SPEC 2)
;
; Included by the package (arcade.asm) and by every translated code part
; (arcaot.py's output), so the two can never disagree about where a cell is.
;
; THE VIEW is one segment holding the board's 8080 address space in place -
; ROM 0000-1FFF, RAM 2000-23FF, VRAM 2400-3FFF - and, above it at 4000h
; where the 8080 never reaches (MEASURED: no access above 3FFF in 6,000
; frames, tools/arcsim.py), the cells the core and the host share. DS = SS =
; the view while translated code runs, so every cell is a plain [disp16].
;
; THE AUX segment holds the VRAM write list: a word at 0 is the next free
; offset, and the words from 2 up are the 8080 addresses of VRAM stores
; since the host last drew (ARCADE-SPEC 4.1).
; =============================================================================

; --- the guest's registers between runs -------------------------------------
ST_AF       equ 0x6000          ; word: AL = A, AH = F in LAHF's layout
ST_BC       equ 0x6002
ST_DE       equ 0x6004
ST_HL       equ 0x6006
ST_SP       equ 0x6008
ST_PC       equ 0x600A          ; the 8080 PC to resume at
ST_BUD      equ 0x600C          ; in: poll points to run before returning
ST_LEFT     equ 0x600E          ; out: what was left of them
ST_WHY      equ 0x6010          ; out: ARCX_* below
ST_INTE     equ 0x6011          ; the 8080's interrupt enable: EI/DI write it
ST_PEND     equ 0x6016          ; the host has an interrupt waiting on EI:
                                ;   EI ends the run at once only then
ST_EIX      equ 0x6017          ; ...and EI did: the run proves nothing
                                ;   about the budget's length
ST_HSS      equ 0x6012          ; the host's SS:SP across a run
ST_HSP      equ 0x6014

; --- the board's cells (Midway 8080 B&W) ------------------------------------
V_IN0       equ 0x6018          ; IN 0
V_IN1       equ 0x6019          ; IN 1: coin, starts, P1 fire/left/right
V_IN2       equ 0x601A          ; IN 2: DIP switches, P2 controls
V_SHLO      equ 0x601C          ; the shift register, a word: OUT 4 shifts
V_SHHI      equ 0x601D          ;   a byte in at the top
V_SHOFF     equ 0x601E          ; OUT 2: the offset IN 3 reads at
V_SND3      equ 0x6020          ; OUT 3: UFO, shot, base hit, invader hit
V_SND5      equ 0x6021          ; OUT 5: the four fleet notes, UFO hit
V_LAMP1     equ 0x6022          ; Sea Wolf's OUT 1: the explosion lamps
V_LAMP2     equ 0x6023          ; ...OUT 2: the torpedo, ready, reload lamps

; --- the Z80's own (ARCADE-SPEC 7.1): what the 8086 has no register for ---
ST_IX       equ 0x6024
ST_IY       equ 0x6026
ST_AF2      equ 0x6028          ; AF' in ST_AF's layout, BC' DE' HL'
ST_BC2      equ 0x602A
ST_DE2      equ 0x602C
ST_HL2      equ 0x602E
ST_I        equ 0x6030
ST_IM       equ 0x6031
ST_VEC      equ 0x6032          ; Pac-Man: OUT (00),A - the IM 2 vector's low byte
ST_IRQEN    equ 0x6033          ; Pac-Man: 5000 bit 0 - VBlank may interrupt
ST_R        equ 0x6034
ST_IFF2     equ 0x6035
ST_CYF      equ 0x6036          ; scratch: a block op's entry CY
ST_RET      equ 0x6038          ; a helper's way back: the Z80 translation
ST_RETIO    equ 0x603A          ;   CALLs nothing, because a CALL writes
ST_SAVE     equ 0x603C          ;   below the guest's SP and Pac-Man's RAM
                                ;   test reads there (ARCADE-SPEC 7.5)

; --- Pac-Man's board in the view (ARCADE-SPEC 7.4) ------------------------
; The inputs are read where the board puts them, 5000/5040/5080/50C0, so a
; Z80 load reads them with no help; what the game WRITES there goes through
; arc_iow and lands beside them, where the host reads it.
PM_IN0      equ 0x5000
PM_IN1      equ 0x5040
PM_DSW1     equ 0x5080
PM_DSW2     equ 0x50C0
PM_LATCH    equ 0x5100          ; 5000-5007 as written: 0 IRQ, 1 sound, 3 flip
PM_SOUND    equ 0x5140          ; 5040-505F: the three voices' nibbles
PM_SPRXY    equ 0x5060          ; 5060-506F: the eight sprites' x, y
PM_TFLAG    equ 0x9000          ; aux: a byte a tile, at 9000h + its index

; --- the host's counters, for the instruments (tests/arcspeed.py) ----------
V_SIG       equ 0x6040          ; 'ARCV': where a harness finds the view
V_NFRAME    equ 0x6044          ; RST 2s delivered
V_NDROP     equ 0x6046          ; half frames dropped to catch up
V_NRUN      equ 0x6048          ; runs of the translated code
V_NBUD      equ 0x604A          ; the poll budget now
V_NCELL     equ 0x604C          ; cells drawn
V_SROUTE    equ 0x604E          ; the sound's route (ARCADE-SPEC 8): 0 the
                                ;   speaker, 1 FM, 2 the stream...
V_SWHY      equ 0x604F          ; ...and the last step that declined one
V_SBTOT     equ 0x6050          ; the stream: its total, written each frame,
V_SBSEG     equ 0x6052          ;   and its ring's segment (0: none)
V_PROBE     equ 0x6060          ; words: arcaot.py --probe's counts

ARC_VIEW_KB equ 25              ; 0000-63FF: the boards' address spaces
                                ;   (Pac-Man's reaches 50FF) and the cells

ARCX_POLL   equ 0               ; the budget ran out at a poll point
ARCX_MISS   equ 1               ; a jump to a PC with no translation
ARCX_HALT   equ 2               ; HLT: wait for an interrupt

; --- the code part's header, at its offset 0 (ARCADE-SPEC 2.3) -------------
ARC_HDR     equ 0
ARC_MAGIC   equ 0x3341          ; 'A3': the PC table is in aux, the ROM a part
AH_MAGIC    equ 0               ; dw ARC_MAGIC
AH_ENTER    equ 2               ; dw arc_enter: far-call CS:this
AH_END      equ 4               ; dw the end of the translated code

; --- AUX (ARCADE-SPEC 2.2): the PC table, the write list, the flags --------
; The PC TABLE is aux's first 32KB, or 40KB - a word for every guest PC,
; 0000-3FFF, or to 4FFF on a Taito colour board whose ROM goes on at 4000h
; - so RET and JP (HL) are `jmp [es:si]`, and the code part holds code
; alone: Pac-Man's is 57KB of it. The list sits above everything else.
AUX_TABLE   equ 0
AUX_FLAGS   equ 0x8000          ; the 8080 boards: a cell's flag at 8000h +
                                ;   its key (the address AND FF1Fh: A400-BF1F)
AUX_LPTR    equ 0xC000          ; the write list's next free offset
AUX_LIST    equ 0xC002          ; ...the list: a CELL's key (a tile's index on
                                ;   Pac-Man) a word, each at most once until
                                ;   drawn: at most 0C702h
ARC_AUX_KB  equ 50
