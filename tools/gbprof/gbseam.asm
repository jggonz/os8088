; gbseam.asm - RealDMG's gate for code that runs from bank 0 across 3FFF
; into the switchable bank (docs/REALDMG-SPEC.md section 3.1), for
; tools/gbours.py. The view's 0000-3FFF is bank 0 and its 4000 rdgsys's own
; state block, so an instruction there reads the bank only because the core
; makes it.
;
; SEAMKIND 1 (gbseam): CALL NZ,nn at 3FFE, its high byte the BANK's first
; byte - taken, into a routine at (40h + bank) x 100h + 80h, and not taken,
; on into 4001. SEAMKIND 2 (gbseam2): INC D at 3FFF, and on into 4000 with
; no instruction across. SEAMKIND 3 (gbseam3): JR Z at 3FF0 from bank 0 to
; the bank's 4010, Looney Tunes' own shape, and not taken. SEAMKIND 4
; (gbseam4): JR from the bank's 4002 back to bank 0's 3FF8. SEAMKIND 5
; (gbseam5): JR at 3FFF, its displacement the bank's first byte - 10h + 4
; x the bank, so each bank's lands on its own routine. Each of banks 1-4, 120 times over so that the
; recompiler translates what it can; the ROM checks D and E itself and
; says "Passed" or "Failed" on the link port.

IF !DEF(SEAMKIND)
DEF SEAMKIND EQU 1
ENDC

SECTION "header", ROM0[$100]
    nop
    jp Start
    ds $150 - @, 0

SECTION "main", ROM0[$150]
Start:
    di
    ld sp, $DFF0
    ld a, 120
    ld [$C000], a
.outer:
    ld c, 1
.bank:
    ld a, c
    ld [$2000], a               ; MBC1: the bank at 4000-7FFF
IF SEAMKIND == 1
    ld d, 0
    ld e, 0
    xor a                       ; Z: the CALL not taken, on into 4001
    call Seam
    ld a, d
    or a
    jr nz, Bad
    ld a, c
    add a, $E0
    cp e
    jr nz, Bad
    ld d, 0
    ld e, 0
    ld a, 1
    or a                        ; NZ: into the bank's routine, back to 4001
    call Seam
    ld a, d
    cp c
    jr nz, Bad
    ld a, c
    add a, $E0
    cp e
    jr nz, Bad
ELIF SEAMKIND == 2
    ld d, 0
    ld e, 0
    call Seam                   ; INC D at 3FFF, and on at 4000
    ld a, d
    cp 1
    jr nz, Bad
    ld a, c
    add a, $E0
    cp e
    jr nz, Bad
ELSE
    ld d, 0
    ld e, 0
IF SEAMKIND == 3
    ld a, 1
    or a                        ; NZ: the JR not taken, D = 77h
    call Seam
    ld a, d
    cp $77
    jr nz, Bad
    ld d, 0
    xor a                       ; Z: taken, into the bank's 4010
ENDC
    call Seam
    ld a, d
IF SEAMKIND == 4
    cp $5A                      ; back in bank 0's 3FF8
ELSE
    or a
ENDC
    jr nz, Bad
    ld a, c
    add a, $E0
    cp e
    jr nz, Bad
ENDC
    inc c
    ld a, c
    cp 5
    jr nz, .bank
    ld a, [$C000]
    dec a
    ld [$C000], a
    jr nz, .outer
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

Passed:
    db "Passed", 10, 0
Failed:
    db "Failed", 10, 0

IF SEAMKIND == 1
SECTION "seam", ROM0[$3FFC]
Seam:
    nop
    nop
    db $C4, $80                 ; CALL NZ,xx80 - xx is 4000h, the bank's
ELIF SEAMKIND == 2
SECTION "seam", ROM0[$3FFE]
Seam:
    nop
    inc d                       ; ...and the next opcode is the bank's 4000
ELIF SEAMKIND == 3
SECTION "seam", ROM0[$3FF0]
Seam:
    db $28, $1E                 ; JR Z,4010: into the bank
    ld d, $77                   ; (not taken)
    ret
ELIF SEAMKIND == 4
SECTION "seam", ROM0[$3FF8]
Back:
    ld d, $5A                   ; from the bank's JR
    ret
Seam:
    jp $4000
ELSE
SECTION "seam", ROM0[$3FFE]
Seam:
    nop
    db $18                      ; JR, the bank's 4000 its displacement
ENDC

FOR N, 1, 5
SECTION "bank{d:N}", ROMX[$4000], BANK[N]
IF SEAMKIND <= 2
IF SEAMKIND == 1
    db $40 + N                  ; the CALL's high byte
ENDC
    ld e, $E0 + N               ; 4001 (or 4000): on from bank 0
    ret
IF SEAMKIND == 1
    ds ($40 + N) * $100 + $80 - @, 0
    ld d, N                     ; the CALL's routine
    ret
ENDC
ELIF SEAMKIND == 3
    ds $4010 - @, 0
    ld e, $E0 + N               ; the JR's target
    ret
ELIF SEAMKIND == 4
    ld e, $E0 + N               ; from Seam's JP
    jr Back                     ; back across 4000 into bank 0
ELSE
    db $10 + 4 * N              ; the JR's displacement: 4011 + 4N
    ds $4011 + 4 * N - @, 0
    ld e, $E0 + N
    ret
ENDC
ENDR
