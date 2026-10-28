; gblcdoff.asm - the LCD off and never on again, as Prehistorik Man keeps it
; while it waits for every button to be let go, for tools/gbaot.py's harness
; (tests/gbaot.py): with the LCD off no VBlank ends a frame, and the
; harness's frame entry ran until one did - 60 seconds of Unicorn and an
; exception. A frame's time ends one now (aothost.asm's h_frame). Says
; "Passed" on the link port first, then goes dark counting for ever.

SECTION "header", ROM0[$100]
    nop
    jp Start
    ds $150 - @, 0

SECTION "main", ROM0[$150]
Start:
    di
    ld sp, $DFF0
    ld hl, Passed
.s:
    ld a, [hl+]
    or a
    jr z, .dark
    ldh [$FF01], a
    ld a, $81
    ldh [$FF02], a
    jr .s
.dark:
    ldh a, [$FF44]
    cp 144
    jr c, .dark
    xor a
    ldh [$FF40], a              ; the LCD off, for good
.o:
    ld de, 0
.l:
    dec de
    ld a, d
    or e
    jr nz, .l
    inc c
    jr .o

Passed:
    db "Passed", 10, 0
