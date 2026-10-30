; gbret.asm - one RET that returns to eight places, for tools/gbours.py:
; the recompiler's MEGAMORPHIC return (docs/REALDMG-SPEC.md section 16.4.1). A RET's
; site caches the last two places it went (rt_ret), so a routine every
; caller shares - Link's Awakening's bank switcher at 07B9 is one - patched
; its site at every return; past RT_RMEGN patches tr_rmeg makes it a lookup.
; Shared is called from four sites in bank 0 and four in bank 1 (a return
; into 4000-7FFF is keyed by its bank), round and round; each site adds
; its own number to HL after the return and Shared counts in DE, so a return
; to the wrong place or a lost one shows in the sums. The ROM checks both
; and says "Passed" or "Failed" on the link port.

DEF ROUNDS EQU 200

SECTION "header", ROM0[$100]
    nop
    jp Start
    ds $150 - @, 0

SECTION "main", ROM0[$150]
Start:
    di
    ld sp, $DFF0
    ld a, 1
    ld [$2000], a               ; MBC1: bank 1 at 4000-7FFF
    ld de, 0
    ld hl, 0
    ld a, ROUNDS
    ld [$C000], a
.round:
    call Shared
    ld bc, 1
    add hl, bc
    call Shared
    ld bc, 2
    add hl, bc
    call Shared
    ld bc, 3
    add hl, bc
    call Shared
    ld bc, 4
    add hl, bc
    call Banked
    ld a, [$C000]
    dec a
    ld [$C000], a
    jr nz, .round
    ld a, d                     ; DE = 8 x ROUNDS
    cp HIGH(8 * ROUNDS)
    jr nz, Bad
    ld a, e
    cp LOW(8 * ROUNDS)
    jr nz, Bad
    ld a, h                     ; HL = 36 x ROUNDS (1 + 2 + ... + 8)
    cp HIGH(36 * ROUNDS)
    jr nz, Bad
    ld a, l
    cp LOW(36 * ROUNDS)
    jr nz, Bad
    ld hl, Passed
    jr Say
Bad:
    ld hl, Failed
Say:
    ld a, [hl+]
    or a
    jr z, .done
    ldh [$FF01], a
    ld a, $81
    ldh [$FF02], a
    jr Say
.done:
    jr .done

Shared:
    inc de
    ret

Passed:
    db "Passed", 10, 0
Failed:
    db "Failed", 10, 0

SECTION "banked", ROMX[$4000], BANK[1]
Banked:
    call Shared
    ld bc, 5
    add hl, bc
    call Shared
    ld bc, 6
    add hl, bc
    call Shared
    ld bc, 7
    add hl, bc
    call Shared
    ld bc, 8
    add hl, bc
    ret
