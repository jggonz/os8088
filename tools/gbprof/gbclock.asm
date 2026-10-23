; gbclock - a Game Boy ROM that is a CLOCK and a LOAD, for timing emulators.
;
; An instrument for docs/plans/REALDMG-PLAN.md, assembled with rgbds by
; tools/gbclock.py (which fetches and builds rgbds at a pinned commit). It
; ships nowhere; it is ours, so it may be put on any disk an emulator under
; test reads.
;
; WHAT IT MEASURES. Every VBlank interrupt adds one to a 24-bit counter of
; EMULATED frames, shown in hex on row 4 (an emulator's own status bar may
; cover row 0). After each VBlank the CPU runs BUSY percent of a frame of
; deliberate work (a 26-M-cycle loop of loads,
; stores, an ALU op, a call/ret and a branch - a game-shaped mix), then HALTs
; until the next one. So an emulator's speed on this ROM is
;
;     counter / (wall seconds x 59.7275)
;
; and the BUSY variants trace its cost against the very quantity
; docs/reports/GAMEBOY-WORKLOAD-2026-10-19.md measures for real games
; (Super Mario Land 23.3%, libbet 13.7%). Row 6 says which variant this is.
;
; Build-time define: BUSY (0..90), the percent of a frame to keep busy.

DEF rLCDC EQU $FF40                         ; the six registers it touches
DEF rSCY  EQU $FF42
DEF rSCX  EQU $FF43
DEF rLY   EQU $FF44
DEF rBGP  EQU $FF47
DEF rIF   EQU $FF0F
DEF rIE   EQU $FFFF
DEF LCDC_ON EQU %10010001                   ; LCD on, BG tiles at $8000, map $9800, BG on

IF !DEF(BUSY)
DEF BUSY EQU 0
ENDC
DEF ITER_M  EQU 26                          ; M-cycles per BusyLoop iteration
DEF ITERS   EQU BUSY * 17556 / 100 / ITER_M

SECTION "vblank", ROM0[$40]
    jp VBlank

SECTION "header", ROM0[$100]
    nop
    jp Start
    ds $150 - @, 0                          ; rgbfix writes logo and checksums

SECTION "main", ROM0[$150]
Start:
    di
    ld sp, $FFFE
.wait:                                      ; LCD off only in VBlank
    ldh a, [rLY]
    cp 144
    jr c, .wait
    xor a
    ldh [rLCDC], a
    ld hl, $8000                            ; the font, tiles 0..20
    ld de, Font
    ld bc, FontEnd - Font
.font:
    ld a, [de]
    ld [hl+], a
    inc de
    dec bc
    ld a, b
    or c
    jr nz, .font
    ld hl, $9800                            ; clear the map to tile 16 (space)
    ld bc, 32 * 32
.clr:
    ld a, 16
    ld [hl+], a
    dec bc
    ld a, b
    or c
    jr nz, .clr
    ld hl, $9800 + 32 * 6 + 1               ; row 6: "BUSY nn%"
    ld de, Label
.lab:
    ld a, [de]
    cp $FF
    jr z, .labok
    ld [hl+], a
    inc de
    jr .lab
.labok:
    xor a
    ldh [hCount], a
    ldh [hCount + 1], a
    ldh [hCount + 2], a
    ld a, %11100100
    ldh [rBGP], a
    xor a
    ldh [rSCX], a
    ldh [rSCY], a
    ld a, LCDC_ON
    ldh [rLCDC], a
    ld a, 1                                 ; VBlank only
    ldh [rIE], a
    xor a
    ldh [rIF], a
    ei
Main:
    halt
    nop
IF ITERS > 0
    ld bc, ITERS
    ld hl, $C000
    ld de, $C100
    call BusyLoop
ENDC
    jr Main

; 26 M-cycles an iteration: 2+1+2+1+2+1+(6+4)+2+1+1+3
BusyLoop:
    ld a, [hl+]
    add a, d
    ld [de], a
    ld a, l
    and $3F
    ld l, a
    call Leaf
    dec bc
    ld a, b
    or c
    jr nz, BusyLoop
    ret
Leaf:
    ret

VBlank:                                     ; +1 frame, six hex digits at row 4
    push af
    push hl
    ld hl, hCount
    inc [hl]
    jr nz, .show
    inc l
    inc [hl]
    jr nz, .show
    inc l
    inc [hl]
.show:
    ld hl, $9800 + 32 * 4 + 1               ; row 4, below any status bar
    ldh a, [hCount + 2]
    call Hex
    ldh a, [hCount + 1]
    call Hex
    ldh a, [hCount]
    call Hex
    pop hl
    pop af
    reti
Hex:                                        ; A -> two tiles at HL+
    push af
    swap a
    and $0F
    ld [hl+], a
    pop af
    and $0F
    ld [hl+], a
    ret

Label:
    db 11, 17, 18, 19, 16, BUSY / 10, BUSY % 10, 20, $FF

Font:
	dw `00333000, `03000300, `03003300, `03030300, `03300300, `03000300, `00333000, `00000000 ; '0'
	dw `00030000, `00330000, `00030000, `00030000, `00030000, `00030000, `00333000, `00000000 ; '1'
	dw `00333000, `03000300, `00000300, `00033000, `00300000, `03000000, `03333300, `00000000 ; '2'
	dw `03333000, `00000300, `00000300, `00333000, `00000300, `00000300, `03333000, `00000000 ; '3'
	dw `00003000, `00033000, `00303000, `03003000, `03333300, `00003000, `00003000, `00000000 ; '4'
	dw `03333300, `03000000, `03333000, `00000300, `00000300, `03000300, `00333000, `00000000 ; '5'
	dw `00033000, `00300000, `03000000, `03333000, `03000300, `03000300, `00333000, `00000000 ; '6'
	dw `03333300, `00000300, `00003000, `00030000, `00300000, `00300000, `00300000, `00000000 ; '7'
	dw `00333000, `03000300, `03000300, `00333000, `03000300, `03000300, `00333000, `00000000 ; '8'
	dw `00333000, `03000300, `03000300, `00333300, `00000300, `00003000, `00330000, `00000000 ; '9'
	dw `00333000, `03000300, `03000300, `03333300, `03000300, `03000300, `03000300, `00000000 ; 'A'
	dw `03333000, `03000300, `03000300, `03333000, `03000300, `03000300, `03333000, `00000000 ; 'B'
	dw `00333000, `03000300, `03000000, `03000000, `03000000, `03000300, `00333000, `00000000 ; 'C'
	dw `03330000, `03003000, `03000300, `03000300, `03000300, `03003000, `03330000, `00000000 ; 'D'
	dw `03333300, `03000000, `03000000, `03333000, `03000000, `03000000, `03333300, `00000000 ; 'E'
	dw `03333300, `03000000, `03000000, `03333000, `03000000, `03000000, `03000000, `00000000 ; 'F'
	dw `00000000, `00000000, `00000000, `00000000, `00000000, `00000000, `00000000, `00000000 ; ' '
	dw `03000300, `03000300, `03000300, `03000300, `03000300, `03000300, `00333000, `00000000 ; 'U'
	dw `00333300, `03000000, `03000000, `00333000, `00000300, `00000300, `03333000, `00000000 ; 'S'
	dw `03000300, `03000300, `00303000, `00030000, `00030000, `00030000, `00030000, `00000000 ; 'Y'
	dw `03300300, `03303000, `00003000, `00030000, `00300000, `00303300, `03003300, `00000000 ; '%'
FontEnd:

SECTION "hram", HRAM
hCount: ds 3
