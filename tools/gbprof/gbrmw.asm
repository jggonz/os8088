; gbrmw - the read-modify-writes of (HL), each run on an address the
; recompiler's form of them takes directly (WRAM) and on one it hands to the
; interpreter (TMA, FF06), for RealDMG's gbcpu row (docs/REALDMG-SPEC.md
; section 16.6, apps/realdmg/rdggen.py rmw_lines).
;
; Assembled with rgbds by tools/gbours.py (tools/gbclock.py's pinned build).
; It ships nowhere; it is ours, so it may go on any disk.
;
; Ops runs INC, DEC, every rotate and shift, SWAP, SET and RES on [HL], and
; folds the flags after each into a running sum; the next RL/RR takes the
; op's own carry, flipped. The two addresses start equal and must stay
; equal, and so must their sums: run 300 times, so that Ops is translated (a
; PC is on its 64th entry, RDG_JITHOT) and then runs translated. It prints
; "Passed" or "Failed" on the link port, as Blargg's ROMs do. Irq does the
; same for a store through (HL) that requests an interrupt, which a store
; that guards itself (rdggen.self_guard) must leave its block for, and
; HramCode rewrites a routine in HRAM once it has been translated. Rows,
; Copies, Fills and W16s hold the loop idioms (add_ff, cpy_ff, fill_ff,
; w16_ff) to the same loops closed by JP NZ.

SECTION "timer", ROM0[$50]
    push af                                 ; the timer interrupt: counts
    ld a, [$C020]
    inc a
    ld [$C020], a
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
    ld a, $5A
    ld [$C000], a
    ldh [$FF06], a                            ; TMA: an I/O register, 8 bits
    xor a
    ld [$C010], a                           ; the WRAM side's flag sum
    ld [$C011], a                           ; ...and TMA's
    ld [$C020], a                           ; timer interrupts taken
    ld [$C022], a                           ; ...and not taken at once
    ld [$C023], a                           ; HRAM code that did not change
    ld [$C024], a                           ; map-row loops that parted
    ld [$C030], a                           ; ...and their count
    ld [$C025], a                           ; copies that parted
    ld [$C031], a                           ; ...and their count
    ld [$C026], a                           ; PPU waits that parted
    ld [$C027], a                           ; fills that parted
    ld [$C033], a                           ; ...and their count
    ld [$C028], a                           ; 16-bit countdowns that parted
    ld [$C035], a                           ; ...and their count
    call HramCode
    call Waits
    ld b, 128                               ; (each case 32 times: translated)
.copies:
    push bc
    call Copies
    call Fills
    pop bc
    push bc
    bit 0, b                                ; (W16s on every other pass: 64
    call z, W16s                            ;   cases, the large among them)
    pop bc
    dec b
    jr nz, .copies
    ld bc, 300
.loop:
    push bc
    call Rows
    call Irq
    ld hl, $C000
    ld de, $C010
    call Ops
    ld hl, $FF06
    ld de, $C011
    call Ops
    pop bc
    dec bc
    ld a, b
    or c
    jr nz, .loop
    ld a, [$C000]
    ld b, a
    ldh a, [$FF06]
    cp b
    ld hl, FailValue
    jr nz, .print
    ld a, [$C010]
    ld b, a
    ld a, [$C011]
    cp b
    ld hl, FailFlags
    jr nz, .print
    ld a, [$C022]
    or a
    ld hl, FailLate
    jr nz, .print
    ld a, [$C020]
    cp 300 & $FF
    ld hl, FailCount
    jr nz, .print
    ld a, [$C023]
    or a
    ld hl, FailHram
    jr nz, .print
    ld a, [$C024]
    or a
    ld hl, FailRows
    jr nz, .print
    ld a, [$C025]
    or a
    ld hl, FailCopy
    jr nz, .print
    ld a, [$C026]
    or a
    ld hl, FailWait
    jr nz, .print
    ld a, [$C027]
    or a
    ld hl, FailFill
    jr nz, .print
    ld a, [$C028]
    or a
    ld hl, FailW16
    jr nz, .print
    ld hl, Passed
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

; [HL] through every read-modify-write, the flags into [DE] after each
Ops:
    inc [hl]
    call Fold
    dec [hl]
    dec [hl]
    call Fold
    rlc [hl]
    call Fold
    rrc [hl]
    call Fold
    rl [hl]
    call Fold
    rr [hl]
    call Fold
    sla [hl]
    call Fold
    sra [hl]
    call Fold
    swap [hl]
    call Fold
    srl [hl]
    call Fold
    set 6, [hl]
    res 1, [hl]
    set 0, [hl]
    call Fold
    inc [hl]
    inc [hl]
    swap [hl]
    dec [hl]
    ret

; A store through (HL) to IF that requests the timer interrupt must take it
; before the next instruction: the store's slow path leaves the block for it
Irq:
    ld hl, $FFFF
    ld [hl], $04                            ; IE: the timer
    ld a, [$C020]
    ld d, a
    ei
    ld hl, $FF0F
    ld [hl], $04                            ; IF: the timer - taken here
    ld a, [$C020]
    di
    sub d
    dec a
    ret z
    ld a, 1
    ld [$C022], a
    ret

; HRAM code is translated behind a guard of its own bytes (REALDMG-SPEC
; 16.3): a routine there, called until it is, then rewritten and called
; again, must run as rewritten
HramCode:
    ld hl, $FFC0
    ld [hl], $04                            ; INC B
    inc l
    ld [hl], $C9                            ; RET
    ld b, 0
    ld c, 200                               ; (64 entries to translate Calls,
    call Calls                              ;   then 64 for FFC0: B = 200)
    ld a, $05                               ; DEC B
    ldh [$FFC0], a
    ld c, 30
    call Calls                              ; translated: B = 170
    ld a, b
    cp 170
    ret z
    ld a, 1
    ld [$C023], a
    ret
Calls:
    call $FFC0
    dec c
    jr nz, Calls
    ret

; ADD HL,DE / DEC B (or C) / JR NZ - the recompiler's add_ff - against the
; same loop closed by JP NZ, which it does not take: HL and F must agree,
; for every count (0 is 256) and carries out of HL
Rows:
    ld a, [$C030]
    inc a
    ld [$C030], a
    ld b, a
    ld c, a
    ld d, a
    ld e, $37
    ld h, $F0
    ld l, a
    push hl
.ib:
    add hl, de
    dec b
    jr nz, .ib
    push af
    pop bc                                  ; C = F, B = A
    ld a, c
    ld [$C040], a
    ld a, l
    ld [$C041], a
    ld a, h
    ld [$C042], a
    pop hl
    push hl
    ld a, [$C030]
    ld b, a
.rb:
    add hl, de
    dec b
    jp nz, .rb
    call .same
    ld a, [$C030]
    ld c, a
    pop hl
    push hl
.ic:
    add hl, de
    dec c
    jr nz, .ic
    push af
    pop bc
    ld a, c
    ld [$C040], a
    ld a, l
    ld [$C041], a
    ld a, h
    ld [$C042], a
    pop hl
    ld a, [$C030]
    ld c, a
.rc:
    add hl, de
    dec c
    jp nz, .rc
.same:                                      ; HL and F against [$C040..]
    push af
    pop bc
    ld a, [$C040]
    cp c
    jr nz, .bad
    ld a, [$C041]
    cp l
    jr nz, .bad
    ld a, [$C042]
    cp h
    ret z
.bad:
    ld a, 1
    ld [$C024], a
    ret

; LD A,(HL+) / LD (DE),A / INC DE / DEC B / JR NZ and LD A,(DE) / LD (HL+),A
; / ... - the recompiler's cpy_ff - against the same loops closed by JP NZ:
; A, F, HL, DE, BC and what the copy left in C100-C1FF and FFC0-FFD3 must
; agree. Four cases by the count: WRAM to HRAM, HRAM to WRAM, an overlap
; (and one of 0, 256 passes, through it), and a ROM source, which cpy_ff
; must decline
Copies:
    ld a, [$C031]
    inc a
    ld [$C031], a
    call Fill
    call Setup
    ld a, [$C032]
    bit 0, a                                ; (C is Setup's)
    jr nz, .i1
    call C2I
    jr .is
.i1:
    call C1I
.is:
    call Save
    ld hl, $C050                            ; to C040: the reference's room
    ld de, $C040
    ld c, 10
.mv:
    ld a, [hl+]
    ld [de], a
    inc de
    dec c
    jr nz, .mv
    call Fill
    call Setup
    ld a, [$C032]
    bit 0, a
    jr nz, .r1
    call C2R
    jr .rs
.r1:
    call C1R
.rs:
    call Save
    ld hl, $C040
    ld de, $C050
    ld c, 10
.cmp:
    ld a, [de]
    cp [hl]
    jr nz, .bad
    inc hl
    inc de
    dec c
    jr nz, .cmp
    ret
.bad:
    ld a, 1
    ld [$C025], a
    ret

; LD (HL+),A / DEC B (or C) / JR NZ - the recompiler's fill_ff - against the
; same loop closed by JP NZ, which it does not take: A, F, HL, DE, BC and
; what the fill left in C100-C1FF and FFC0-FFD3 must agree. Counts 1-16 and
; 256 (0) into WRAM and HRAM, the carry set and clear, and a range from FF7C
; across I/O into HRAM, which fill_ff must decline (its stores are io_wr's)
Fills:
    ld a, [$C033]
    inc a
    ld [$C033], a
    call Fill
    call FSetup
    call FI
    call Save
    ld hl, $C050                            ; to C040: the reference's room
    ld de, $C040
    ld c, 10
.mv:
    ld a, [hl+]
    ld [de], a
    inc de
    dec c
    jr nz, .mv
    call Fill
    call FSetup
    call FR
    call Save
    ld hl, $C040
    ld de, $C050
    ld c, 10
.cmp:
    ld a, [de]
    cp [hl]
    jr nz, .bad
    inc hl
    inc de
    dec c
    jr nz, .cmp
    ret
.bad:
    ld a, 1
    ld [$C027], a
    ret

; The recompiler's w16_ff: a 16-bit countdown closed by LD A,r / OR r /
; JR NZ - the delay (NOP x3 / DEC DE / LD A,D / OR E), the copy (LD A,(HL+)
; / LD (DE),A / INC DE / DEC BC / LD A,B / OR C) and three fills (XOR A, LD
; A,D or LD A,E / LD (HL+),A / DEC BC / LD A,B / OR C, or LD A,C / OR B) -
; against the same loops closed by JP NZ: A, F, BC, DE, HL and a sum of
; C200-C9FF must agree. Counts 1-16, and 2048 passes (2000 for the delay),
; more than a frame's budget, so the loop is taken in pieces; copies from
; bank 0 and from the bank at 4000; a fill from FF7C across I/O to decline
W16s:
    ld a, [$C035]
    inc a
    ld [$C035], a
    call W16Init
    call W16Set
    call W16I
    call W16Save
    ld hl, $C050                            ; to C040: the reference's room
    ld de, $C040
    ld c, 10
.mv:
    ld a, [hl+]
    ld [de], a
    inc de
    dec c
    jr nz, .mv
    call W16Init
    call W16Set
    call W16R
    call W16Save
    ld hl, $C040
    ld de, $C050
    ld c, 10
.cmp:
    ld a, [de]
    cp [hl]
    jr nz, .bad
    inc hl
    inc de
    dec c
    jr nz, .cmp
    ret
.bad:
    ld a, 1
    ld [$C028], a
    ret

; C200-C9FF to a pattern, by a loop no idiom takes
W16Init:
    ld hl, $C200
.f:
    ld a, l
    xor h
    ld [hl+], a
    ld a, h
    cp $CA
    jr nz, .f
    ret

; the case: [$C036] the shape 0-4, BC (or DE) the count, HL and DE
W16Set:
    ld a, [$C035]
    ld c, a
    ld b, 0
.m5:                                        ; the shape: the case mod 5
    cp 5
    jr c, .s
    sub 5
    jr .m5
.s:
    ld [$C036], a
    ld a, c
    and 56
    cp 8                                    ; (one case in eight: large)
    jr z, .big
    ld a, c
    swap a
    and 15
    inc a
    ld c, a                                 ; 1-16
    jr .regs
.big:
    ld a, [$C036]
    or a
    ld bc, 2000                             ; (the delay's: in pieces)
    jr z, .regs
    ld bc, 2048
.regs:
    ld a, [$C036]
    or a
    jr nz, .notd
    ld d, b                                 ; the delay: DE the count
    ld e, c
    ld bc, $1234
    ld hl, $5678
    ret
.notd:
    dec a
    jr nz, .notc
    ld de, $C200                            ; the copy: to C200, from bank 0
    ld hl, $0150                            ;   or the bank at 4000
    ld a, [$C035]
    bit 4, a
    ret z
    ld hl, $4000
    ret
.notc:
    ld de, $5A37                            ; a fill: D and E the values
    ld hl, $C200
    ld a, [$C035]
    and $28
    cp $20                                  ; (now and then a small one
    ret nz                                  ;   from FF7C, through I/O:
    ld hl, $FF7C                            ;   declined)
    ld a, c
    and 7
    add a, 5
    ld c, a
    ld b, 0
    ret

W16I:
    ld a, [$C036]
    or a
    jr z, D1I
    dec a
    jr z, C2I16
    dec a
    jr z, F3I
    dec a
    jr z, F4I
    jr F5I
W16R:
    ld a, [$C036]
    or a
    jr z, D1R
    dec a
    jr z, C2R16
    dec a
    jr z, F3R
    dec a
    jr z, F4R
    jr F5R
D1I:
    nop
    nop
    nop
    dec de
    ld a, d
    or e
    jr nz, D1I
    ret
D1R:
    nop
    nop
    nop
    dec de
    ld a, d
    or e
    jp nz, D1R
    ret
C2I16:
    ld a, [hl+]
    ld [de], a
    inc de
    dec bc
    ld a, b
    or c
    jr nz, C2I16
    ret
C2R16:
    ld a, [hl+]
    ld [de], a
    inc de
    dec bc
    ld a, b
    or c
    jp nz, C2R16
    ret
F3I:
    xor a
    ld [hl+], a
    dec bc
    ld a, b
    or c
    jr nz, F3I
    ret
F3R:
    xor a
    ld [hl+], a
    dec bc
    ld a, b
    or c
    jp nz, F3R
    ret
F4I:
    ld a, d
    ld [hl+], a
    dec bc
    ld a, b
    or c
    jr nz, F4I
    ret
F4R:
    ld a, d
    ld [hl+], a
    dec bc
    ld a, b
    or c
    jp nz, F4R
    ret
F5I:
    ld a, e
    ld [hl+], a
    dec bc
    ld a, c
    or b
    jr nz, F5I
    ret
F5R:
    ld a, e
    ld [hl+], a
    dec bc
    ld a, c
    or b
    jp nz, F5R
    ret

; the registers and a sum of C200-C9FF, into C050-C059
W16Save:
    push af
    ld a, l
    ld [$C051], a
    ld a, h
    ld [$C052], a
    ld a, e
    ld [$C053], a
    ld a, d
    ld [$C054], a
    ld a, b
    ld [$C055], a
    ld a, c
    ld [$C056], a
    pop hl                                  ; L = F, H = A
    ld a, l
    ld [$C050], a
    ld a, h
    ld [$C057], a
    ld hl, $C200
    ld de, 0
.s:
    ld a, [hl+]
    add a, e
    ld e, a
    ld a, d
    adc a, 0
    rlca
    ld d, a
    ld a, h
    cp $CA
    jr nz, .s
    ld a, e
    ld [$C058], a
    ld a, d
    ld [$C059], a
    ret

; HL, B or C the count and the other a constant, DE a constant, E the
; value; [$C034] 0 the count in B, 1 in C
FSetup:
    ld a, [$C033]
    ld c, a
    and 15
    inc a
    ld b, a                                 ; 1-16
    xor a
    ld [$C034], a
    ld a, c
    and 3
    jr z, .w
    dec a
    jr z, .h
    dec a
    jr z, .z
    ld hl, $FF7C                            ; 3: into I/O, then HRAM: 5-12
    ld a, b
    and 7
    add a, 5
    ld b, a
    jr .v
.w:
    ld hl, $C110                            ; 0: WRAM
    jr .v
.h:
    ld hl, $FFC2                            ; 1: HRAM, to FFD1 at most
    jr .v
.z:
    ld a, c                                 ; 2: 256 passes over C100-C1FF,
    and 63                                  ;   or 1-16 to its end
    jr nz, .z2
    ld b, a
    ld hl, $C100
    jr .v
.z2:
    ld hl, $C1E0
.v:
    ld a, c
    xor $A5
    ld e, a                                 ; E = the value
    ld d, $5A
    bit 2, c
    jr z, .b
    ld a, 1
    ld [$C034], a
    ld c, b                                 ; the count in C, B a constant
    ld b, $37
    ret
.b:
    ld c, $59                               ; the count in B, C a constant
    ret

FI:
    ld a, [$C034]
    or a
    jr nz, FIC
FIB:
    ld a, [$C033]
    rra                                     ; C = the count's bit 0
    ld a, e
.l:
    ld [hl+], a
    dec b
    jr nz, .l
    ret
FIC:
    ld a, [$C033]
    rra
    ld a, e
.l:
    ld [hl+], a
    dec c
    jr nz, .l
    ret
FR:
    ld a, [$C034]
    or a
    jr nz, FRC
FRB:
    ld a, [$C033]
    rra
    ld a, e
.l:
    ld [hl+], a
    dec b
    jp nz, .l
    ret
FRC:
    ld a, [$C033]
    rra
    ld a, e
.l:
    ld [hl+], a
    dec c
    jp nz, .l
    ret

; HL, DE, B and C for the case, [$C032] the loop (0 HL to DE), C the
; count's bit 0
Setup:
    ld a, [$C031]
    ld c, a
    and 15
    inc a
    ld b, a                                 ; 1-16
    xor a
    ld [$C032], a
    ld a, c
    and 3
    jr z, .c0
    dec a
    jr z, .c1
    dec a
    jr z, .c2
    ld hl, $0150                            ; 3: from ROM
    ld de, $C180
    jr .cy
.c0:
    ld a, c
    and 63
    jr z, .c256
    ld hl, $C108                            ; 0: WRAM to HRAM
    ld de, $FFC0
    jr .cy
.c256:
    ld b, a                                 ; (0: 256 passes)
    ld hl, $C100
    ld de, $C101
    jr .cy
.c1:
    ld a, 1                                 ; 1: HRAM to WRAM
    ld [$C032], a
    ld de, $FFC2
    ld hl, $C150
    jr .cy
.c2:
    sla b                                   ; 2: an overlap, 2-32
    ld hl, $C120
    ld de, $C121
.cy:
    ld a, c
    rra                                     ; C = the count's bit 0
    ret

C2I:
    ld a, [hl+]
    ld [de], a
    inc de
    dec b
    jr nz, C2I
    ret
C2R:
    ld a, [hl+]
    ld [de], a
    inc de
    dec b
    jp nz, C2R
    ret
C1I:
    ld a, [de]
    ld [hl+], a
    inc de
    dec b
    jr nz, C1I
    ret
C1R:
    ld a, [de]
    ld [hl+], a
    inc de
    dec b
    jp nz, C1R
    ret

; the registers and a sum of the two areas, into C050-C059
Save:
    push af
    ld a, l
    ld [$C051], a
    ld a, h
    ld [$C052], a
    ld a, e
    ld [$C053], a
    ld a, d
    ld [$C054], a
    ld a, b
    ld [$C055], a
    ld a, c
    ld [$C056], a
    pop hl                                  ; L = F, H = A
    ld a, l
    ld [$C050], a
    ld a, h
    ld [$C057], a
    ld hl, $C100
    ld de, 0
    ld c, 0
.s:
    ld a, [hl+]
    add a, e
    ld e, a
    ld a, d
    adc a, 0
    rlca                                    ; (an order the sum feels)
    ld d, a
    dec c
    jr nz, .s
    ld hl, $FFC0
    ld c, 20
.t:
    ld a, [hl+]
    add a, e
    ld e, a
    ld a, d
    adc a, 0
    rlca
    ld d, a
    dec c
    jr nz, .t
    ld a, e
    ld [$C058], a
    ld a, d
    ld [$C059], a
    ret

; C100-C1FF and FFC0-FFD3 to a pattern
Fill:
    ld hl, $C100
    ld c, 0
.f:
    ld a, l
    xor $5A
    ld [hl+], a
    dec c
    jr nz, .f
    ld hl, $FFC0
    ld c, 20
.g:
    ld a, l
    ld [hl+], a
    dec c
    jr nz, .g
    ret

; Waits on the PPU - the recompiler's stat_ff, which takes whole passes off
; (and cnt_cph's CP (HL) on LY, in fives)
; the clock - run from ROM, translated, and from a copy in WRAM, which is
; never translated: each waits after a delay that moves its phase, then
; counts passes to LY's next change, which places the exit to within a pass.
; The two must agree for every phase
Waits:
    ld hl, WaitBlock                        ; (ROM to WRAM: cpy_ff declines)
    ld de, $C800
    ld b, WaitEnd - WaitBlock
.cp:
    ld a, [hl+]
    ld [de], a
    inc de
    dec b
    jr nz, .cp
    xor a
    ld [$C033], a
.trial:
    call Sync
    call WaitM0
    ld e, a
    call Sync
    call $C800 + WaitM0 - WaitBlock
    cp e
    jr nz, .bad
    call Sync
    call WaitM01
    ld e, a
    call Sync
    call $C800 + WaitM01 - WaitBlock
    cp e
    jr nz, .bad
    call Sync
    call WaitLY
    ld e, a
    call Sync
    call $C800 + WaitLY - WaitBlock
    cp e
    jr nz, .bad
    call Sync
    call WaitLYH
    ld e, a
    call Sync
    call $C800 + WaitLYH - WaitBlock
    cp e
    jr nz, .bad
    ld a, [$C033]
    inc a
    ld [$C033], a
    cp 128
    jr nz, .trial
    xor a
    ldh [$FFFF], a
    ret
.bad:
    xor a
    ldh [$FFFF], a
    inc a
    ld [$C026], a
    ret

; to VBlank's start, exactly: HALT with only VBlank enabled and IME off
; resumes the moment it is requested
Sync:
    xor a
    ldh [$FF0F], a
    inc a
    ldh [$FFFF], a
    halt
    ret

WaitBlock:                                  ; position-independent: JRs only
WaitM0:
    ld a, [$C033]
    ld c, a
    inc c
.d:
    dec c
    jr nz, .d
.w:
    ldh a, [$FF41]
    and 3
    jr nz, .w
    ldh a, [$FF44]
    ld d, a
    ld b, 0
.c:
    inc b
    ldh a, [$FF44]
    cp d
    jr z, .c
    ld a, b
    ret
WaitM01:
    ld a, [$C033]
    ld c, a
    inc c
.d:
    dec c
    jr nz, .d
.w:
    ldh a, [$FF41]
    bit 1, a
    jr nz, .w
    ldh a, [$FF44]
    ld d, a
    ld b, 0
.c:
    inc b
    ldh a, [$FF44]
    cp d
    jr z, .c
    ld a, b
    ret
WaitLY:
    ld a, [$C033]
    ld c, a
    inc c
.d:
    dec c
    jr nz, .d
.w:                                         ; (VBlank and a delay of up
    ldh a, [$FF44]                          ;   to five lines: line 2 is
    cp 2                                    ;   the next frame's)
    jr nz, .w
    ld d, 2
    ld b, 0
.c:
    inc b
    ldh a, [$FF44]
    cp d
    jr z, .c
    ld a, b
    ret
WaitLYH:                                    ; Donkey Kong Land's form: CP (HL)
    push hl                                 ;   on LY, a 5-M-cycle pass
    ld a, [$C033]                           ;   (cnt_cph)
    ld c, a
    inc c
.d:
    dec c
    jr nz, .d
    ld hl, $FF44
    ld a, 2
.w:
    cp [hl]
    jr nz, .w
    ld b, 0                                 ; passes of 6 M-cycles to line
.c:                                         ;   3 - fine enough to see an exit
    inc b                                   ;   a cycle or two off, which the
    cp [hl]                                 ;   8-M-cycle count above is not
    jr z, .c                                ;   (no skip: HL is not RAM)
    ld a, b
    pop hl
    ret
WaitEnd:

; F into the sum at [DE], with the carry of the add left for the next op
Fold:
    push af
    push hl
    push af
    pop hl                                  ; L = F
    ld a, [de]
    add a, l
    ld [de], a
    pop hl
    pop af
    ccf                                     ; (the carry the next RL/RR takes
    ret                                     ;   is still the op's own, flipped)

Passed:
    db "Passed", 10, 0
FailValue:
    db "Failed: the two values parted", 10, 0
FailFlags:
    db "Failed: the two flag sums parted", 10, 0
FailLate:
    db "Failed: an interrupt was taken late", 10, 0
FailCount:
    db "Failed: interrupts lost", 10, 0
FailHram:
    db "Failed: HRAM code ran as it was", 10, 0
FailRows:
    db "Failed: a map-row loop parted", 10, 0
FailCopy:
    db "Failed: a copy loop parted", 10, 0
FailFill:
    db "Failed: a fill loop parted", 10, 0
FailW16:
    db "Failed: a 16-bit countdown parted", 10, 0
FailWait:
    db "Failed: a wait on the PPU parted", 10, 0
