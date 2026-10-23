; gbhblank - Pokemon Gold's frame, for RealDMG's event costs (docs/REALDMG-
; SPEC.md section 6): the STAT interrupt on MODE 0 for good (STAT = 08h,
; IE = VBlank and STAT), with Gold's LCD handler - which does nothing while
; its effect pointer (hLCDCPointer, here FF80h) is 0, as it is for most of
; the game - and a main loop that HALTs until the VBlank handler has counted
; a frame (Gold's DelayFrame). So a frame is 144 STAT interrupts, each
; waking the HALT, and next to no work: what it costs an emulator is the
; event machinery alone. Frames are counted at C000h.
;
; Every eighth frame the pointer is SCX's (43h) for one frame and the
; handler writes wLYOverrides[LY] there - a wave down the screen - so the
; handler's working path is in the run as well.
;
; Assembled with rgbds by tools/gbours.py (tools/gbclock.py's pinned build).
; It ships nowhere; it is ours, so it may go on any disk.

SECTION "vblank", ROM0[$40]
    jp VBlank
SECTION "lcd", ROM0[$48]
    jp LCD

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
    ld [$C001], a
    ldh [$FF80], a                          ; hLCDCPointer: no effect
    ld hl, $C100                            ; wLYOverrides: a wave
    ld b, 144
.ov:
    ld a, b
    and 7
    ld [hl+], a
    dec b
    jr nz, .ov
    ld a, $08                               ; STAT: mode 0's interrupt
    ldh [$FF41], a
    xor a
    ldh [$FF0F], a
    ld a, $03                               ; VBlank and STAT
    ldh [$FFFF], a
    ei
.frame:
    call DelayFrame
    ld a, [$C000]
    and 7
    ld a, 0
    jr nz, .p
    ld a, $43
.p:
    ldh [$FF80], a
    jr .frame

DelayFrame:                                 ; Gold's: HALT until VBlank says
    ld a, 1                                 ;   it has run
    ld [$C001], a
.h:
    halt
    nop
    ld a, [$C001]
    and a
    jr nz, .h
    ret

VBlank:
    push af
    ld a, [$C000]
    inc a
    ld [$C000], a
    xor a
    ld [$C001], a
    pop af
    reti

LCD:                                        ; Gold's handler, line for line
    push af
    ldh a, [$FF80]
    and a
    jr z, .done
    push bc
    ldh a, [$FF44]
    ld c, a
    ld b, $C1
    ld a, [bc]
    ld b, a
    ldh a, [$FF80]
    ld c, a
    ld a, b
    ldh [c], a
    pop bc
.done:
    pop af
    reti
