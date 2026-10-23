; gbdemo - RealDMG's own demo: a Game Boy ROM written for an IBM PC 5150
; (docs/plans/REALDMG-PLAN.md section 6 and wave 8, docs/REALDMG-SPEC.md
; section 18, tests/gbdemo.py).
;
; Assembled with rgbds by tools/gbours.py (tools/gbclock.py's pinned build).
; It is ours, so it may go on any disk - and it runs on a real Game Boy too.
;
; IT IS WRITTEN FOR THE MACHINE IT RUNS ON, the way WireBall is: a 4.77 MHz
; 8088 emulating a Game Boy has the CPU for a few hundred Game Boy
; instructions a frame and the drawing for a few dozen changed lines, so the
; scene is still and the motion is small.
; - A still background: the title, three lines of text, a band of the four
;   shades and a ground strip, written once with the LCD off.
; - One 8x8 sprite, a ball, bouncing on the ground (a 40-frame parabola) and
;   between the walls, a pixel a frame.
; - A clock, MM:SS, in four map bytes rewritten once a second.
; - A blip on square channel 2 (659 Hz, 31 ms, its length counter ending it)
;   every time the ball lands.
; - And HALT for the rest of every frame.
; RealDMG draws by damage (REALDMG-SPEC section 13.2), so a frame costs the
; lines the ball crossed and, once a second, the clock's.

DEF rIF   EQU $FF0F
DEF rNR21 EQU $FF16
DEF rNR22 EQU $FF17
DEF rNR23 EQU $FF18
DEF rNR24 EQU $FF19
DEF rNR50 EQU $FF24
DEF rNR51 EQU $FF25
DEF rNR52 EQU $FF26
DEF rLCDC EQU $FF40
DEF rSCY  EQU $FF42
DEF rSCX  EQU $FF43
DEF rLY   EQU $FF44
DEF rBGP  EQU $FF47
DEF rOBP0 EQU $FF48
DEF rIE   EQU $FFFF

DEF T_SH1    EQU 42                         ; tiles past the font
DEF T_SH2    EQU 43
DEF T_SH3    EQU 44
DEF T_GROUND EQU 45
DEF T_BALL   EQU 46
DEF T_BAR    EQU 47

DEF REST_Y    EQU 112                        ; the ball's top at rest (screen y)
DEF XMIN     EQU 8
DEF XMAX     EQU 152
DEF HOP      EQU 40                         ; frames a bounce

; the text is the font's tile numbers
CHARMAP " ", 0
CHARMAP "0", 1
CHARMAP "1", 2
CHARMAP "2", 3
CHARMAP "3", 4
CHARMAP "4", 5
CHARMAP "5", 6
CHARMAP "6", 7
CHARMAP "7", 8
CHARMAP "8", 9
CHARMAP "9", 10
CHARMAP "A", 11
CHARMAP "B", 12
CHARMAP "C", 13
CHARMAP "D", 14
CHARMAP "E", 15
CHARMAP "F", 16
CHARMAP "G", 17
CHARMAP "H", 18
CHARMAP "I", 19
CHARMAP "J", 20
CHARMAP "K", 21
CHARMAP "L", 22
CHARMAP "M", 23
CHARMAP "N", 24
CHARMAP "O", 25
CHARMAP "P", 26
CHARMAP "Q", 27
CHARMAP "R", 28
CHARMAP "S", 29
CHARMAP "T", 30
CHARMAP "U", 31
CHARMAP "V", 32
CHARMAP "W", 33
CHARMAP "X", 34
CHARMAP "Y", 35
CHARMAP "Z", 36
CHARMAP ":", 37
CHARMAP ".", 38
CHARMAP "-", 39
CHARMAP "!", 40
CHARMAP "/", 41

SECTION "vblank", ROM0[$40]
    reti                                    ; HALT wakes; main does the work

SECTION "header", ROM0[$100]
    nop
    jp Start
    ds $150 - @, 0                          ; rgbfix writes logo and checksums

SECTION "main", ROM0[$150]
Start:
    di
    ld sp, $FFFE
.wait:                                      ; the LCD off only in VBlank
    ldh a, [rLY]
    cp 144
    jr c, .wait
    xor a
    ldh [rLCDC], a
    ld hl, $8000                            ; the tiles
    ld de, Tiles
    ld bc, TilesEnd - Tiles
.tiles:
    ld a, [de]
    ld [hl+], a
    inc de
    dec bc
    ld a, b
    or c
    jr nz, .tiles
    ld hl, $9800                            ; the map, blank
    ld bc, 32 * 32
.clr:
    xor a
    ld [hl+], a
    dec bc
    ld a, b
    or c
    jr nz, .clr
    ld hl, Scene                            ; the scene: (row, column, text)
.scene:
    ld a, [hl+]
    cp $FF
    jr z, .done
    ld e, a                                 ; DE = 9800 + row x 32 + column
    ld d, 0
    REPT 5
    sla e
    rl d
    ENDR
    ld a, [hl+]
    add e
    ld e, a
    ld a, d
    adc $98
    ld d, a
.str:
    ld a, [hl+]
    cp $FF
    jr z, .scene
    ld [de], a
    inc de
    jr .str
.done:
    ld hl, $FE00                            ; OAM empty
    ld b, 160
    xor a
.oam:
    ld [hl+], a
    dec b
    jr nz, .oam
    ld a, %11100100
    ldh [rBGP], a
    ldh [rOBP0], a
    xor a
    ldh [rSCX], a
    ldh [rSCY], a
    ldh [hT], a
    ldh [hFrames], a
    ldh [hSec], a
    ldh [hMin], a
    ld a, XMIN + 30
    ldh [hX], a
    ld a, 1
    ldh [hDX], a
    ld a, $80                               ; sound on, both sides
    ldh [rNR52], a
    ld a, $77
    ldh [rNR50], a
    ld a, $FF
    ldh [rNR51], a
    ld a, %10010011                         ; LCD on, tiles 8000, BG, sprites
    ldh [rLCDC], a
    ld a, 1                                 ; VBlank only
    ldh [rIE], a
    xor a
    ldh [rIF], a
    ei
Main:
    halt
    nop
    call Ball                               ; in VBlank: OAM and the map are ours
    call Clock
    jr Main

; Ball - a frame of the bounce: the parabola's height, a pixel sideways, and
; a blip on landing
Ball:
    ldh a, [hT]
    ld e, a
    ld d, 0
    ld hl, Hop
    add hl, de
    ld a, REST_Y + 16                        ; OAM Y = screen y + 16
    sub [hl]
    ld [$FE00], a
    ldh a, [hX]
    ld b, a
    ldh a, [hDX]
    add b
    cp XMIN
    jr c, .turn
    cp XMAX + 1
    jr c, .x
.turn:
    ldh a, [hDX]                            ; at a wall: the other way
    cpl
    inc a
    ldh [hDX], a
    add b
.x:
    ldh [hX], a
    add 8                                   ; OAM X = screen x + 8
    ld [$FE01], a
    ld a, T_BALL
    ld [$FE02], a
    xor a
    ld [$FE03], a
    ldh a, [hT]
    inc a
    cp HOP
    jr c, .t
    ld a, $80 | 56                          ; landed: the blip - duty 50%,
    ldh [rNR21], a                          ;   length 64 - 56 = 8 steps
    ld a, $F0
    ldh [rNR22], a
    ld a, LOW(1849)                         ; 131072 / (2048 - 1849) = 659 Hz
    ldh [rNR23], a
    ld a, $C0 | HIGH(1849)                  ; trigger, length enabled
    ldh [rNR24], a
    xor a
.t:
    ldh [hT], a
    ret

; Clock - MM:SS on the bottom row, rewritten once a second (60 frames)
Clock:
    ldh a, [hFrames]
    inc a
    ldh [hFrames], a
    cp 60
    ret c
    xor a
    ldh [hFrames], a
    ldh a, [hSec]
    inc a
    cp 60
    jr c, .s
    ldh a, [hMin]
    inc a
    cp 100
    jr c, .m
    xor a
.m:
    ldh [hMin], a
    xor a
.s:
    ldh [hSec], a
    ld hl, $9800 + 32 * 17 + 6              ; "TIME MM:SS" from column 1
    ldh a, [hMin]
    call Dec2
    inc hl                                  ; past the colon
    ldh a, [hSec]
    ; (falls through)
Dec2:                                       ; A (0-99) -> two digit tiles at HL+
    ld b, 0
.tens:
    cp 10
    jr c, .ones
    sub 10
    inc b
    jr .tens
.ones:
    ld c, a
    ld a, b
    add '0'
    ld [hl+], a
    ld a, c
    add '0'
    ld [hl+], a
    ret

; the bounce: the height above the floor, frame by frame
Hop:
FOR T, 0, HOP
    db 144 * T * (HOP - T) / (HOP * HOP)
ENDR

; (row, column, text, FFh) ... FFh
Scene:
    db 1, 4
    db "REALDMG DEMO", $FF
    db 2, 0
    REPT 20
    db T_BAR
    ENDR
    db $FF
    db 4, 2
    db "A GAME BOY ON AN", $FF
    db 5, 0
    db "IBM PC 5150 AND ITS", $FF
    db 6, 3
    db "4.77 MHZ 8088!", $FF
    db 8, 6
    db " ", " ", T_SH1, T_SH1, T_SH2, T_SH2, T_SH3, T_SH3, $FF
    db 15, 0
    REPT 20
    db T_GROUND
    ENDR
    db $FF
    db 17, 1
    db "TIME 00:00  OS8088", $FF
    db $FF

Tiles:
	dw `00000000, `00000000, `00000000, `00000000, `00000000, `00000000, `00000000, `00000000 ; ' '
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
	dw `00333000, `03000300, `03000000, `03033300, `03000300, `03000300, `00333300, `00000000 ; 'G'
	dw `03000300, `03000300, `03000300, `03333300, `03000300, `03000300, `03000300, `00000000 ; 'H'
	dw `00333000, `00030000, `00030000, `00030000, `00030000, `00030000, `00333000, `00000000 ; 'I'
	dw `00033300, `00003000, `00003000, `00003000, `00003000, `03003000, `00330000, `00000000 ; 'J'
	dw `03000300, `03003000, `03030000, `03300000, `03030000, `03003000, `03000300, `00000000 ; 'K'
	dw `03000000, `03000000, `03000000, `03000000, `03000000, `03000000, `03333300, `00000000 ; 'L'
	dw `03000300, `03303300, `03030300, `03030300, `03000300, `03000300, `03000300, `00000000 ; 'M'
	dw `03000300, `03300300, `03030300, `03003300, `03000300, `03000300, `03000300, `00000000 ; 'N'
	dw `00333000, `03000300, `03000300, `03000300, `03000300, `03000300, `00333000, `00000000 ; 'O'
	dw `03333000, `03000300, `03000300, `03333000, `03000000, `03000000, `03000000, `00000000 ; 'P'
	dw `00333000, `03000300, `03000300, `03000300, `03030300, `03003000, `00330300, `00000000 ; 'Q'
	dw `03333000, `03000300, `03000300, `03333000, `03030000, `03003000, `03000300, `00000000 ; 'R'
	dw `00333300, `03000000, `03000000, `00333000, `00000300, `00000300, `03333000, `00000000 ; 'S'
	dw `03333300, `00030000, `00030000, `00030000, `00030000, `00030000, `00030000, `00000000 ; 'T'
	dw `03000300, `03000300, `03000300, `03000300, `03000300, `03000300, `00333000, `00000000 ; 'U'
	dw `03000300, `03000300, `03000300, `03000300, `03000300, `00303000, `00030000, `00000000 ; 'V'
	dw `03000300, `03000300, `03000300, `03030300, `03030300, `03030300, `00303000, `00000000 ; 'W'
	dw `03000300, `03000300, `00303000, `00030000, `00303000, `03000300, `03000300, `00000000 ; 'X'
	dw `03000300, `03000300, `00303000, `00030000, `00030000, `00030000, `00030000, `00000000 ; 'Y'
	dw `03333300, `00000300, `00003000, `00030000, `00300000, `03000000, `03333300, `00000000 ; 'Z'
	dw `00000000, `00030000, `00030000, `00000000, `00030000, `00030000, `00000000, `00000000 ; ':'
	dw `00000000, `00000000, `00000000, `00000000, `00000000, `00330000, `00330000, `00000000 ; '.'
	dw `00000000, `00000000, `00000000, `03333300, `00000000, `00000000, `00000000, `00000000 ; '-'
	dw `00030000, `00030000, `00030000, `00030000, `00030000, `00000000, `00030000, `00000000 ; '!'
	dw `00000300, `00000300, `00003000, `00030000, `00300000, `03000000, `03000000, `00000000 ; '/'
	dw `11111111, `11111111, `11111111, `11111111, `11111111, `11111111, `11111111, `11111111 ; shade 1
	dw `22222222, `22222222, `22222222, `22222222, `22222222, `22222222, `22222222, `22222222 ; shade 2
	dw `33333333, `33333333, `33333333, `33333333, `33333333, `33333333, `33333333, `33333333 ; shade 3
	dw `12121212, `21212121, `12121212, `21212121, `12121212, `21212121, `12121212, `21212121 ; ground
	dw `00333300, `03111130, `31122113, `31211113, `31111113, `31111113, `03111130, `00333300 ; ball
	dw `00000000, `00000000, `33333333, `22222222, `22222222, `33333333, `00000000, `00000000 ; bar
TilesEnd:

SECTION "hram", HRAM
hT:      ds 1                               ; the bounce's frame
hX:      ds 1                               ; the ball's screen x
hDX:     ds 1                               ; +1 or -1
hFrames: ds 1
hSec:    ds 1
hMin:    ds 1
