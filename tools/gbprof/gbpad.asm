; gbpad - the joypad, a frame at a time, for RealDMG's gbreplay row
; (docs/REALDMG-SPEC.md section 10.5): every VBlank the game reads the
; buttons, as cartridges do, and folds them and the frame into a 16-bit sum
; at C000h - so one frame's buttons different anywhere in a play, or the
; same buttons a frame early or late, leave a different sum in WRAM, which
; is what a replay's end compares.
;
; Assembled with rgbds by tools/gbours.py (tools/gbclock.py's pinned build).
; It ships nowhere; it is ours, so it may go on any disk.

DEF rP1   EQU $FF00
DEF rIF   EQU $FF0F
DEF rLCDC EQU $FF40
DEF rIE   EQU $FFFF

SECTION "vblank", ROM0[$40]
    reti                                    ; HALT wakes; Main does the work

SECTION "header", ROM0[$100]
    nop
    jp Start
    ds $150 - @, 0

SECTION "main", ROM0[$150]
Start:
    di
    ld sp, $FFFE
    xor a
    ld [$C000], a                           ; the sum
    ld [$C001], a
    ld [$C002], a                           ; frames
    ld [$C003], a                           ; the last frame's buttons
    ld a, %10010001                         ; LCD on, BG on
    ldh [rLCDC], a
    ld a, 1                                 ; VBlank only
    ldh [rIE], a
    xor a
    ldh [rIF], a
    ei
Main:
    halt
    nop
    call Pad                                ; B = the buttons, 1 = held
    ld a, b
    ld [$C003], a
    ld a, [$C000]                           ; DE = the sum, rotated left
    ld e, a
    ld a, [$C001]
    ld d, a
    sla e
    rl d
    jr nc, .n
    set 0, e
.n:
    ld a, [$C002]                           ; + the frame + the buttons + 1
    inc a
    ld [$C002], a
    add b
    inc a
    add e
    ld e, a
    ld a, d
    adc 0
    ld d, a
    ld a, e
    ld [$C000], a
    ld a, d
    ld [$C001], a
    jr Main

; Pad - B = the eight buttons, 1 = held: the d-pad high, the others low
Pad:
    ld a, $20                               ; the d-pad
    ldh [rP1], a
    ldh a, [rP1]
    ldh a, [rP1]
    and $0F
    swap a
    ld b, a
    ld a, $10                               ; A, B, Select, Start
    ldh [rP1], a
    ldh a, [rP1]
    ldh a, [rP1]
    ldh a, [rP1]
    and $0F
    or b
    cpl
    ld b, a
    ld a, $30
    ldh [rP1], a
    ret
