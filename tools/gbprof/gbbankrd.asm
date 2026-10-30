; gbbankrd.asm - banks switched to LAZILY (docs/REALDMG-SPEC.md section
; 10.3.2), for tools/gbours.py: translated code's LD (2000),A records a bank
; the cache has not got and fetches it only when something reads it. Each of
; banks 1-7, in turn and round and round with a VBlank interrupt counting
; underneath, is switched to and then READ every way the core has: a byte
; (rd_rom), a call into it (precompiled: no read), a POP with SP in it
; (pop_slow) and a computed JP HL to code the walk cannot find there (the
; interpreter, through tr_int) - each first in turn, so each is the read
; that finds the bank not in. A small cache - tests/gbaot.py gives it three
; slots - evicts on every round, so a read that missed the fetch reads
; another bank's bytes and the sums part. The sums go out on the link port
; as hex at the end.

DEF ROUNDS EQU 120

SECTION "vblank", ROM0[$40]
    push af
    ld a, [$C010]
    inc a
    ld [$C010], a
    pop af
    reti

SECTION "header", ROM0[$100]
    nop
    jp Start
    ds $150 - @, 0

SECTION "main", ROM0[$150]
Start:
    di
    ld sp, $DFF0
    xor a
    ld [$C000], a               ; the byte sum
    ld [$C001], a               ; the call sum
    ld [$C002], a               ; the POP sum, low and high
    ld [$C003], a
    ld [$C004], a               ; the JP HL sum
    ld [$C005], a               ; the bank-0 interpreted read's sum
    ld [$C010], a               ; VBlanks
    ld [$C011], a               ; (0: JP HL's H is 42h + this)
    ld a, ROUNDS
    ld [$C008], a
    call Add9                   ; (a block: the walk sees this call)
    ld a, 1
    ldh [$FFFF], a              ; IE: VBlank
    ei
.round:
    ld a, 1
    ld [$2000], a
    call ProbeB
    ld a, 5
    ld [$2000], a
    call ProbeP
    ld a, 2
    ld [$2000], a
    call ProbeZ
    ld a, 7
    ld [$2000], a
    call ProbeB
    ld a, 3
    ld [$2000], a
    call ProbeP
    ld a, 6
    ld [$2000], a
    call ProbeJ
    ld a, 4
    ld [$2000], a
    call ProbeB
    ld a, [$C008]
    dec a
    ld [$C008], a
    jr nz, .round
    di
    ld hl, $C000
    ld b, 6
.out:
    ld a, [hl+]
    call Hex
    dec b
    jr nz, .out
    ld a, 10
    call Put
.done:
    jr .done

; ProbeB, ProbeP, ProbeJ - the bank just switched to, read four ways, the
; first read a byte, a POP or the JP HL in turn: each is the one that finds
; the bank not yet in
ProbeB:
    call RdByte
    call RdCall
    call RdPop
    jp RdJp
ProbeP:
    call RdPop
    call RdByte
    call RdCall
    jp RdJp
ProbeJ:
    call RdJp
    call RdPop
    call RdByte
    jp RdCall
ProbeZ:
    call RdZero
    call RdJp
    call RdPop
    call RdByte
    jp RdCall

RdByte:
    ld a, [$4000]               ; a byte: the bank's number x 3
    ld hl, $C000
    add a, [hl]
    ld [hl], a
    ret
RdCall:
    call $4100                  ; a call: A = the bank's number x 5
    ld hl, $C001
    add a, [hl]
    ld [hl], a
    ret
RdPop:
    ld [$C00A], sp              ; a POP out of the bank: SP at 4002
    ld sp, $4002
    pop bc
    ld hl, $C00A
    ld a, [hl+]
    ld h, [hl]
    ld l, a
    ld sp, hl
    ld a, [$C002]
    add a, c
    ld [$C002], a
    ld a, [$C003]
    adc a, b
    ld [$C003], a
    ret
RdZero:
    ld a, [$C011]               ; a JP HL to bank-0 code nothing else names
    add a, $30                  ;   (Zero, at 3000h), so it is INTERPRETED,
    ld h, a                     ;   reading the bank from bank 0: the fetch
    ld l, 0                     ;   must leave the interpreter's DS the
    call JpHl                   ;   view (tests/gbaot.py's leg makes the
    ld hl, $C005                ;   last PC rdgsys kept the bank's)
    add a, [hl]
    ld [hl], a
    ret
RdJp:
    ld a, [$C011]               ; a JP HL the walk cannot follow: H 42h,
    add a, $42                  ;   made at run time, L 0
    ld h, a
    ld l, 0
    call JpHl                   ; A = the bank's number + 9
    ld hl, $C004
    add a, [hl]
    ld [hl], a
    ret

JpHl:
    jp hl

Add9:                           ; (the walk finds it from Start: a block in
    add a, 9                    ;   the table, which the interpreted 4200
    ret                         ;   jumps to through tr_ejf's filter)

Hex:
    push af
    swap a
    call .nib
    pop af
.nib:
    and $0F
    add a, $30
    cp $3A
    jr c, Put
    add a, 7
Put:
    ldh [$FF01], a
    ld a, $81
    ldh [$FF02], a
    ret

SECTION "zero", ROM0[$3000]
Zero:                           ; (RdZero's: interpreted, from bank 0)
    ld a, [$4000]
    ret

MACRO BANKBODY
SECTION "bank\1", ROMX[$4000], BANK[\1]
    db \1 * 3, 0                ; 4000: the byte
    dw \1 * $100 + $5A          ; 4002: the POP's word
    ds $4100 - @, 0
    ld a, \1 * 5                ; 4100: the call
    ret
    ds $4200 - @, 0
    ld a, \1                    ; 4200: JP HL's target, interpreted,
    jp Add9                     ;   jumping to a precompiled block (tr_ejf)
ENDM
    BANKBODY 1
    BANKBODY 2
    BANKBODY 3
    BANKBODY 4
    BANKBODY 5
    BANKBODY 6
    BANKBODY 7
