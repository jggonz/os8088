; gbpop - POPs out of a ROM bank, for RealDMG's gbcpu row
; (docs/REALDMG-SPEC.md section 5): the game points SP into 4000-7FFF and
; POPs the words there, as Pokemon's VBlank copy reads tiles out of a bank.
; RealDMG's core runs with SP as the Game Boy's inside its view, whose
; 4000-7FFF is RealDMG's own state; pop_slow reads the BANK there.
;
; Assembled with rgbds by tools/gbours.py (tools/gbclock.py's pinned build).
; It ships nowhere; it is ours, so it may go on any disk.
;
; Bank 2 holds four known words at 4000h and bank 3 four others. ReadBank is
; handed one mapped, points SP at them, POPs BC, DE, HL and AF and checks
; them all; it runs 300 times on each bank, so that it is translated (a PC
; is on its 64th entry, RDG_JITHOT) and then runs translated. It prints
; "Passed" or "Failed" on the link port, as Blargg's ROMs do.

SECTION "header", ROM0[$100]
    nop
    jp Start
    ds $150 - @, 0

SECTION "main", ROM0[$150]
Start:
    di
    ld sp, $FFFE
    xor a
    ld [$C000], a                           ; failures
    ld bc, 300
.again:
    push bc
    ld a, 2
    ld [$2000], a
    ld de, Want2
    call ReadBank
    ld a, 3
    ld [$2000], a
    ld de, Want3
    call ReadBank
    pop bc
    dec bc
    ld a, b
    or c
    jr nz, .again
    ld a, [$C000]
    or a
    ld hl, Passed
    jr z, .print
    ld hl, Failed
.print:
    ld a, [hl+]
    or a
    jr z, .done
    ldh [$FF01], a
    ld a, $81
    ldh [$FF02], a
    jr .print
.done:
    jr .done

; the bank in 4000-7FFF against the eight bytes at DE: B C D E H L A and
; F's Z and C as bits 0 and 1. Nothing is pushed or called while SP is in
; the bank (a write there is a write to ROM); each register is put by in
; C020-C027 first, then SP comes back and the eight are compared.
ReadBank:
    ld [$C002], sp                          ; LD (nn),SP: the way back
    ld hl, $4000
    ld sp, hl
    pop bc
    pop hl                                  ; DE's word
    ld a, h
    ld [$C022], a
    ld a, l
    ld [$C023], a
    pop hl                                  ; HL's word
    ld a, h
    ld [$C024], a
    ld a, l
    ld [$C025], a
    pop af
    ld [$C026], a
    ld a, 0                                 ; (LD leaves the flags)
    jr nc, .nc
    ld a, 2                                 ; C
.nc:
    jr nz, .nz
    inc a                                   ; Z
.nz:
    ld [$C027], a
    ld a, b
    ld [$C020], a
    ld a, c
    ld [$C021], a
    ld hl, $C002
    ld a, [hl+]
    ld h, [hl]
    ld l, a
    ld sp, hl                               ; SP back
    ld hl, $C020
    ld b, 8
.cmp:
    ld a, [de]
    cp [hl]
    jr nz, .bad
    inc de
    inc hl
    dec b
    jr nz, .cmp
    ret
.bad:
    ld a, 1
    ld [$C000], a
    ret

Want2:                                      ; B C D E H L A, then Z|C:
    db $11, $22, $33, $44, $55, $66, $77, 3 ;   F = F0h, Z N H C all set
Want3:
    db $55, $66, $77, $88, $99, $AA, $BB, 1 ;   F = C0h, Z and N

Passed:
    db "Passed", 10, 0
Failed:
    db "Failed: a POP out of a ROM bank read something else", 10, 0

SECTION "bank2", ROMX[$4000], BANK[2]
    dw $1122, $3344, $5566, $77F0           ; BC, DE, HL, AF (POP's order)
SECTION "bank3", ROMX[$4000], BANK[3]
    dw $5566, $7788, $99AA, $BBC0
