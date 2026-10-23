; gbtone - a Game Boy ROM that plays three known sounds, for RealDMG's sound
; gate (docs/REALDMG-SPEC.md section 17, tests/gbsound.py).
;
; Assembled with rgbds by tools/gbours.py (tools/gbclock.py's pinned build).
; It ships nowhere; it is ours, so it may go on any disk.
;
; THE SCRIPT, counted in VBlanks:
;   frames   0-59   square channel 1 at 440 Hz (x = 1750), volume 15, held
;   frames  60-89   channel 1's DAC off (NR12 = 0): silence
;   frames  90-149  square channel 2 at 880 Hz (x = 1899) with LENGTH 64
;                   (a quarter of a second at 256 Hz), which must stop by
;                   itself
;   frames 150-     channel 1 at x = 7FFh, 131,072 Hz: silent, and a divide
;                   by 1 for a speaker that works its pitch out naively (the
;                   compatibility sweep's Alleyway hung REALDMG.O88 so) - the
;                   frames from here on are counted at C000h
; So the speaker should sound 440 Hz for ~1 s, nothing for ~0.5 s, 880 Hz for
; 0.25 s, then nothing - which checks a trigger, a DAC switched off and a
; length counter running out.

DEF rNR10 EQU $FF10
DEF rNR11 EQU $FF11
DEF rNR12 EQU $FF12
DEF rNR13 EQU $FF13
DEF rNR14 EQU $FF14
DEF rNR21 EQU $FF16
DEF rNR22 EQU $FF17
DEF rNR23 EQU $FF18
DEF rNR24 EQU $FF19
DEF rNR50 EQU $FF24
DEF rNR51 EQU $FF25
DEF rNR52 EQU $FF26
DEF rIF   EQU $FF0F
DEF rIE   EQU $FFFF

SECTION "vblank", ROM0[$40]
    reti                                    ; HALT wakes; the count is in main

SECTION "header", ROM0[$100]
    nop
    jp Start
    ds $150 - @, 0                          ; rgbfix writes logo and checksums

SECTION "main", ROM0[$150]
Start:
    di
    ld sp, $FFFE
    ld a, $80                               ; sound on, both sides, loud
    ldh [rNR52], a
    ld a, $77
    ldh [rNR50], a
    ld a, $FF
    ldh [rNR51], a
    xor a
    ldh [rNR10], a                          ; no sweep
    ld a, $80                               ; duty 50%
    ldh [rNR11], a
    ld a, $F0                               ; volume 15, no envelope
    ldh [rNR12], a
    ld a, LOW(1750)                         ; 131072 / (2048 - 1750) = 440 Hz
    ldh [rNR13], a
    ld a, $80 | HIGH(1750)                  ; trigger, no length
    ldh [rNR14], a
    ld a, 1                                 ; VBlank only
    ldh [rIE], a
    xor a
    ldh [rIF], a
    ei
    ld b, 60
    call Frames
    xor a                                   ; channel 1's DAC off: silence
    ldh [rNR12], a
    ld b, 30
    call Frames
    ld a, $80                               ; channel 2: duty 50%, length 64
    ldh [rNR21], a
    ld a, $F0
    ldh [rNR22], a
    ld a, LOW(1899)                         ; 131072 / (2048 - 1899) = 880 Hz
    ldh [rNR23], a
    ld a, $C0 | HIGH(1899)                  ; trigger, length enabled
    ldh [rNR24], a
    ld b, 60
    call Frames
    ld a, $F0                               ; channel 1 at x = 7FFh
    ldh [rNR12], a
    ld a, $FF
    ldh [rNR13], a
    ld a, $87
    ldh [rNR14], a
    xor a
    ld [$C000], a
.forever:
    halt
    ld a, [$C000]                           ; frames since
    inc a
    jr z, .forever                          ; (held at 255)
    ld [$C000], a
    jr .forever

; Frames - B VBlanks
Frames:
    halt
    dec b
    jr nz, Frames
    ret
