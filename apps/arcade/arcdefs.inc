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
ST_AF       equ 0x4000          ; word: AL = A, AH = F in LAHF's layout
ST_BC       equ 0x4002
ST_DE       equ 0x4004
ST_HL       equ 0x4006
ST_SP       equ 0x4008
ST_PC       equ 0x400A          ; the 8080 PC to resume at
ST_BUD      equ 0x400C          ; in: poll points to run before returning
ST_LEFT     equ 0x400E          ; out: what was left of them
ST_WHY      equ 0x4010          ; out: ARCX_* below
ST_INTE     equ 0x4011          ; the 8080's interrupt enable: EI/DI write it
ST_PEND     equ 0x4016          ; the host has an interrupt waiting on EI:
                                ;   EI ends the run at once only then
ST_EIX      equ 0x4017          ; ...and EI did: the run proves nothing
                                ;   about the budget's length
ST_HSS      equ 0x4012          ; the host's SS:SP across a run
ST_HSP      equ 0x4014

; --- the board's cells (Midway 8080 B&W) ------------------------------------
V_IN0       equ 0x4018          ; IN 0
V_IN1       equ 0x4019          ; IN 1: coin, starts, P1 fire/left/right
V_IN2       equ 0x401A          ; IN 2: DIP switches, P2 controls
V_SHLO      equ 0x401C          ; the shift register, a word: OUT 4 shifts
V_SHHI      equ 0x401D          ;   a byte in at the top
V_SHOFF     equ 0x401E          ; OUT 2: the offset IN 3 reads at
V_SND3      equ 0x4020          ; OUT 3: UFO, shot, base hit, invader hit
V_SND5      equ 0x4021          ; OUT 5: the four fleet notes, UFO hit

; --- the host's counters, for the instruments (tests/arcspeed.py) ----------
V_SIG       equ 0x4040          ; 'ARCV': where a harness finds the view
V_NFRAME    equ 0x4044          ; RST 2s delivered
V_NDROP     equ 0x4046          ; half frames dropped to catch up
V_NRUN      equ 0x4048          ; runs of the translated code
V_NBUD      equ 0x404A          ; the poll budget now
V_NCELL     equ 0x404C          ; cells drawn
V_PROBE     equ 0x4060          ; words: arcaot.py --probe's counts

ARC_VIEW_KB equ 17              ; 0000-43FF: the board and the cells

ARCX_POLL   equ 0               ; the budget ran out at a poll point
ARCX_MISS   equ 1               ; a jump to a PC with no translation
ARCX_HALT   equ 2               ; HLT: wait for an interrupt

; --- the code part's header, at ARC_HDR (after the 32KB PC table) -----------
ARC_HDR     equ 0x8000
ARC_MAGIC   equ 0x4341          ; 'AC'
AH_MAGIC    equ 0               ; dw ARC_MAGIC
AH_ENTER    equ 2               ; dw arc_enter: far-call CS:this
AH_ROM      equ 4               ; dw the ROM image's offset in the part
AH_ROMLEN   equ 6               ; dw its length
AH_END      equ 8               ; dw the end of the translated code

AUX_LPTR    equ 0               ; the write list's next free offset
AUX_LIST    equ 0x4000          ; ...the list: the CELLS stores changed, a
                                ;   word each - the cell's key, its first
                                ;   byte's address AND FF1Fh - and each at
                                ;   most once until drawn: 896 at the most
                                ; ...and a key IS its flag byte's offset in
                                ;   aux: 2400-3F1F, the low 32 of each 256
                                ;   (ARCADE-SPEC 4.1)
ARC_AUX_KB  equ 18

