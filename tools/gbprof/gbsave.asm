; gbsave - a Game Boy ROM that counts its own boots in battery RAM and reads
; the MBC3 clock, for RealDMG's save gate (docs/REALDMG-SPEC.md section 10.1,
; tests/gbsave.py).
;
; Assembled with rgbds by tools/gbours.py (tools/gbclock.py's pinned build).
; It ships nowhere; it is ours, so it may go on any disk.
;
; The cartridge is an MBC3 with the timer, 32KB of RAM in four banks and a
; battery (type 10h, RAM code 3). What it leaves in WRAM:
;   C000-C003  bank n's counter at A000, after this boot added one to it -
;              FFh + 1 = 00h on a cartridge never saved, 01h after one save
;   C004-C007  bank n's A001 read back after all four were written 50h+n:
;              four different banks, not one window
;   C008       the clock's seconds, latched at the start
;   C009       ...and again 120 frames later
;   C00A       ...and every frame since
;   C00F       A5h when all of the above is written

DEF rIF   EQU $FF0F
DEF rIE   EQU $FFFF

SECTION "vblank", ROM0[$40]
    reti

SECTION "header", ROM0[$100]
    nop
    jp Start
    ds $150 - @, 0

SECTION "main", ROM0[$150]
Start:
    di
    ld sp, $FFFE
    ld a, $0A                               ; RAM and the clock on
    ld [$0000], a
    ld b, 0
.count:
    ld a, b
    ld [$4000], a
    ld a, [$A000]
    inc a
    ld [$A000], a
    ld h, $C0
    ld l, b
    ld [hl], a
    ld a, b
    or $50
    ld [$A001], a
    inc b
    ld a, b
    cp 4
    jr nz, .count
    ld b, 0
.check:
    ld h, $C0
    ld a, b
    add 4
    ld l, a
    ld a, b
    ld [$4000], a
    ld a, [$A001]
    ld [hl], a
    inc b
    ld a, b
    cp 4
    jr nz, .check
    ld a, $08                               ; the seconds register
    ld [$4000], a
    call Latch
    ld [$C008], a
    ld a, 1
    ldh [rIE], a
    xor a
    ldh [rIF], a
    ei
    ld b, 120
.wait:
    halt
    dec b
    jr nz, .wait
    call Latch
    ld [$C009], a
    ld a, $A5
    ld [$C00F], a
.forever:
    halt
    call Latch
    ld [$C00A], a
    jr .forever

; Latch - latch the clock, A = the selected register
Latch:
    xor a
    ld [$6000], a
    inc a
    ld [$6000], a
    ld a, [$A000]
    ret
