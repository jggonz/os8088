; gbbank - a 1MB MBC5 Game Boy ROM that switches among all 64 of its banks
; and checks each one is the bank it asked for, for RealDMG's bank cache gate
; (docs/REALDMG-SPEC.md section 10.3, tests/gbbank.py).
;
; Assembled with rgbds by tools/gbours.py (tools/gbclock.py's pinned build).
; It ships nowhere; it is ours, so it may go on any disk.
;
; Every bank n (1-63) holds n at 4000h, n XOR A5h at 4001h, and at 4010h a
; routine that answers n in A - so a switch is checked by a read AND by code
; run from 4000-7FFF, which is where the core's DS and the recompiler's bank
; keys meet the cache. The banks come from a 6-bit LFSR, so every one is
; visited, over and over, in an order no slot count favours. What it keeps
; in WRAM:
;   C000-C001  switches checked (a word, low byte first, wrapping)
;   C002       switches that read the wrong bank (stops counting at FFh)
;   C003       the bank of the first wrong one, or 0
;   C00F       A5h once the first switch has been checked

SECTION "header", ROM0[$100]
    nop
    jp Start
    ds $150 - @, 0

SECTION "main", ROM0[$150]
Start:
    di
    ld sp, $FFFE
    xor a
    ld hl, $C000
    ld [hl+], a
    ld [hl+], a
    ld [hl+], a
    ld [hl], a
    ld [$3000], a                           ; the ninth bank bit: 0
    ld e, 1                                 ; E = the LFSR, never 0
.next:
    ld a, e                                 ; x^6 + x^5 + 1: 63 states
    add a, a
    bit 6, a
    jr z, .b5
    xor $01
.b5:
    bit 4, e
    jr z, .b6
    xor $01
.b6:
    and $3F
    ld e, a                                 ; E = 1..63
    ld [$2000], a                           ; the bank
    ld a, [$4000]
    cp e
    jr nz, .bad
    ld a, [$4001]
    xor $A5
    cp e
    jr nz, .bad
    call $4010                              ; ...and its code
    cp e
    jr nz, .bad
.ok:
    ld hl, $C000
    inc [hl]
    jr nz, .done
    inc l
    inc [hl]
.done:
    ld a, $A5
    ld [$C00F], a
    jr .next
.bad:
    ld a, [$C002]
    inc a
    jr z, .ok                               ; (stays at FFh)
    ld [$C002], a
    ld a, [$C003]
    or a
    jr nz, .ok
    ld a, e
    ld [$C003], a
    jr .ok

FOR N, 1, 64
SECTION "bank{d:N}", ROMX[$4000], BANK[N]
    db N, N ^ $A5
    ds $4010 - @, 0
    ld a, N
    ret
ENDR
