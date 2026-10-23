; gbfm - all four sound channels, started one at a time and then held, for
; RealDMG's gbfm row (docs/REALDMG-SPEC.md section 17.1): square 1 at
; 440 Hz (50% duty) at frame 30, square 2 at 660 Hz (12.5%) at 60, the wave
; at 220 Hz at 90, the noise (NR43 = 42h) at 120 - each at full volume with
; no envelope and no length, so all four sound from frame 120 on. Frames are
; counted at C000h.
;
; Assembled with rgbds by tools/gbours.py (tools/gbclock.py's pinned build).
; It ships nowhere; it is ours, so it may go on any disk.

SECTION "vblank", ROM0[$40]
    push af
    ld a, [$C000]
    inc a
    ld [$C000], a
    pop af
    reti

SECTION "header", ROM0[$100]
    nop
    jp Start
    ds $150 - @, 0

SECTION "main", ROM0[$150]
Start:
    di
    ld sp, $FFFE
    xor a
    ld [$C000], a
    ld a, $80
    ldh [$FF26], a                          ; NR52: sound on
    ld a, $77
    ldh [$FF24], a                          ; NR50: full both sides
    ld a, $FF
    ldh [$FF25], a                          ; NR51: every channel both sides
    ld hl, $FF30                            ; the wave: a triangle
    ld de, Wave
    ld b, 16
.w:
    ld a, [de]
    ld [hl+], a
    inc de
    dec b
    jr nz, .w
    xor a
    ldh [$FF0F], a
    ld a, $01
    ldh [$FFFF], a                          ; VBlank alone
    ei
    ld b, 30
    call Until
    ld a, $80                               ; square 1: 440 Hz, x = 1750
    ldh [$FF11], a
    ld a, $F0
    ldh [$FF12], a
    ld a, $D6
    ldh [$FF13], a
    ld a, $86
    ldh [$FF14], a
    ld b, 60
    call Until
    ld a, $00                               ; square 2: 660 Hz, x = 1849
    ldh [$FF16], a
    ld a, $F0
    ldh [$FF17], a
    ld a, $39
    ldh [$FF18], a
    ld a, $87
    ldh [$FF19], a
    ld b, 90
    call Until
    ld a, $80                               ; the wave: 220 Hz, x = 1750
    ldh [$FF1A], a
    ld a, $20
    ldh [$FF1C], a
    ld a, $D6
    ldh [$FF1D], a
    ld a, $86
    ldh [$FF1E], a
    ld b, 120
    call Until
    ld a, $F0                               ; the noise
    ldh [$FF21], a
    ld a, $42
    ldh [$FF22], a
    ld a, $80
    ldh [$FF23], a
.hold:
    halt
    nop
    jr .hold

Until:                                      ; sleep to frame B
    halt
    nop
    ld a, [$C000]
    cp b
    jr c, Until
    ret

Wave:
    db $01, $23, $45, $67, $89, $AB, $CD, $EF
    db $FE, $DC, $BA, $98, $76, $54, $32, $10
