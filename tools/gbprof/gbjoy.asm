; gbjoy - the JOYPAD interrupt, for RealDMG's gbheader row
; (docs/REALDMG-SPEC.md section 7): the game sleeps in HALT with only that
; interrupt enabled, and counts each wake at C000h - so a button press is
; the only thing that can move it, as on the cartridges that wait for one
; at a title or pause screen.
;
; Assembled with rgbds by tools/gbours.py (tools/gbclock.py's pinned build).
; It ships nowhere; it is ours, so it may go on any disk.

SECTION "joypad", ROM0[$60]
    push af                                 ; a press: counted
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
    ld [$C000], a                           ; wakes
    ld [$C001], a                           ; ...and laps of the loop
    ldh [$FF0F], a
    ld a, $10                               ; the joypad's alone
    ldh [$FFFF], a
    ei
.sleep:
    halt
    nop
    ld a, [$C001]
    inc a
    ld [$C001], a
    jr .sleep
