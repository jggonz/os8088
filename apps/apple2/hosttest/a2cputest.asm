; =============================================================================
; os8088 - apps/apple2/hosttest/a2cputest.asm       THE 6502 CORE'S GATE
;
; Part of APPLE2, an Apple II Plus emulator (docs/APPLE2-SPEC.md section 4.4).
; apps/apple2/a2cpu.inc is a DERIVED COPY of apps/c64/c64cpu.inc, which is
; GPL-2-or-later by way of VICE (Copyright (C) 1996-2025 the VICE team);
; apps/apple2/ is GPL-2-or-later and apps/apple2/COPYING is the licence text.
; This harness is apps/c64/hosttest/c64cputest.asm's shape with the C64's
; seven bank rows replaced by the Apple II memory model's own.
;
; A BOOT SECTOR THAT RUNS THE SHIPPING 6502 CORE ON A REAL x86, with nothing
; else running - `make a2cputest`, minutes, the rcz80test precedent. The core
; is %included, not copied, so what runs is the shipping text, under the one
; condition that makes this OS different from every other target it could be
; tested on: SS != DS. Here CS = DS = 0 stand in for the package's segment
; (a2cpu.inc reaches its statics through CS), SS = 0x1000 is the task stack,
; ES = 0x3000 stands in for KERNEL_SEG and is checked after every call out,
; the Apple's 64KB is the segment 0x4000 and the ROM PART 0x6000.
;
; IT IS NOT A DORMANN WRAPPER. APPLE2-SPEC section 4.4's twelve rows are the
; routines below, and EVERY ONE OF THEM HAS A NEGATIVE CONTROL: the row is run
; a second time with something deliberately broken - a stale fetch bias, an
; I/O stub that answers a constant, a clobbered ES, a scratch page made
; reachable, a missing branch penalty, an illegal opcode dispatched to NOP -
; and the harness FAILS if that run passes. A check that cannot fail is not a
; check (LESSONS.md 2).
;
; The perturbations reach the ENVIRONMENT or the core's own tables at runtime,
; never the source: what is assembled here is byte for byte what ships.
;
;     -DROWS      the eleven rows below (seconds)
;     -DDORMANN   Klaus Dormann's 6502 functional test, appended by the
;                 script at sector DORMSEC and read straight into the Apple's
;                 RAM; PC starts at $0400 and the run is judged by where the
;                 program counter settles (minutes)
;     -DNEG       ...and the same with the ADC handler replaced by a wrong
;                 one, which must NOT reach the success trap
;
; RUN IT:  apps/apple2/hosttest/a2cputest.sh   (or `make a2cputest`)
; =============================================================================

cpu 8086
bits 16
org 0x7C00

SEG_STACK   equ 0x1000
SEG_KERNEL  equ 0x3000              ; "KERNEL_SEG": must come back intact
SEG_RAM     equ 0x4000              ; the Apple's 64KB claim
SEG_ROM     equ 0x6000              ; ...and APPLE2.ROM's part, $D000-$FFFF at
                                    ; part offset 0 (APPLE2-SPEC section 1.4)

IMG_SECTORS equ 64                  ; 32KB of harness; the program follows
DORMSEC     equ 64                  ; ...at this sector, 128 sectors of it

%macro PUSHI 1
    mov ax, %1
    push ax
%endmacro

section .text
section .rodata follows=.text
section .data   follows=.rodata
section .bss    follows=.data nobits
section .text

; -----------------------------------------------------------------------------
; STAGE 1 - the boot sector
; -----------------------------------------------------------------------------
start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7000
    sti
    cld
    mov word [lba], 1
    mov bx, 0x7E00
.rd:
    mov ax, [lba]
    cmp ax, IMG_SECTORS
    jae .go
    call rdsec
    add bx, 512
    inc word [lba]
    jmp .rd
.go:
    jmp 0x0000:body
lba: dw 0

; rdsec - read the sector in [lba] to ES:BX (18 sectors a track, 2 heads)
rdsec:
    push ax
    push cx
    push dx
    mov ax, [lba]
    xor dx, dx
    mov cx, 18
    div cx
    mov cl, dl
    inc cl
    mov dh, al
    and dh, 1
    shr ax, 1
    mov ch, al
    mov dl, 0
    mov ax, 0x0201
    int 0x13
    jc .err
    pop dx
    pop cx
    pop ax
    ret
.err:
    mov al, 'D'
    call putc
    mov al, 7
    jmp exit

    times 510-($-$$) db 0
    dw 0xAA55

; -----------------------------------------------------------------------------
; STAGE 2
; -----------------------------------------------------------------------------
body:
    cli
    mov ax, SEG_STACK
    mov ss, ax
    mov sp, 0xFFF0
    xor ax, ax
    mov ds, ax                      ; DS = CS = 0: "the package"
    mov ax, SEG_KERNEL
    mov es, ax
    sti
    cld

    mov si, msg_hello
    call puts

    mov word [_a2_m+AM_RAMSEG], SEG_RAM
    mov word [_a2_m+AM_ROMSEG], SEG_ROM
    call mem_init

%ifdef DORMANN
    jmp dormann_main
%else
    jmp rows_main
%endif

; =============================================================================
; THE MACHINE THE ROWS RUN IN
; =============================================================================
; mem_init - a recognisable byte everywhere, so "which region answered" is a
; question the test can ASK rather than infer:
;   RAM   0x11 everywhere, $A000 = 0x1A, $BFFF = 0x1B
;         ...and $C000-$FFFF gets 0x99, which NOTHING may ever read: on a 48K
;         II+ that whole range is soft switches, slot space and ROM, so a 0x99
;         coming back means the read ladder fell through to the claim
;   ROM   0xEE, with $D000 = 0xDD and $FFFF = 0xDF
mem_init:
    push es
    push di
    push cx
    mov ax, SEG_RAM
    mov es, ax
    xor di, di
    mov cx, 0xC000
    mov al, 0x11
    rep stosb                       ; $0000-$BFFF, 49,152 bytes
    mov cx, 0x4000
    mov al, 0x99
    rep stosb                       ; $C000-$FFFF, 16,384 - the poison
    mov byte [es:0xA000], 0x1A
    mov byte [es:0xBFFF], 0x1B
    ; the ROM claim: the main ROM at part offset 0 covers $D000-$FFFF
    mov ax, SEG_ROM
    mov es, ax
    xor di, di
    mov cx, 0x3000
    mov al, 0xEE
    rep stosb
    mov byte [es:0x0000], 0xDD      ; $D000
    mov byte [es:0x2FFF], 0xDF      ; $FFFF
    pop cx
    pop di
    pop es
    ret

; scr_wr / scr_rd - the core's scratch, from the harness's side
; AL = value, BX = offset
scr_wr:
    push es
    push bx
    mov es, [_a2_m+AM_RAMSEG]
    add bx, A2_SCR_BASE
    mov [es:bx], al
    pop bx
    pop es
    ret
scr_rd:
    push es
    push bx
    mov es, [_a2_m+AM_RAMSEG]
    add bx, A2_SCR_BASE
    mov al, [es:bx]
    pop bx
    pop es
    ret

; ram_wr / ram_rd - BX = address, AL = value. THE CLAIM DIRECTLY, not through
; the core's ladder: this is how the harness reaches memory the emulated
; machine cannot, which is exactly what rows 4 and 7 are about.
ram_wr:
    push es
    mov es, [_a2_m+AM_RAMSEG]
    mov [es:bx], al
    pop es
    ret
ram_rd:
    push es
    mov es, [_a2_m+AM_RAMSEG]
    mov al, [es:bx]
    pop es
    ret

; rom_wr - BX = ROM PART OFFSET, AL = value
rom_wr:
    push es
    mov es, [_a2_m+AM_ROMSEG]
    mov [es:bx], al
    pop es
    ret

; rebias - empty the fetch region, so the next fetch re-asks. The Apple II's
; map never MOVES (there is no banking on a 48K II+), so this stands in for
; the C64's bank_set: what it protects is the case where the HARNESS changes
; PC between runs.
rebias:
    push ax
    push bx
    mov bx, A2_SCR_BOUND
    xor al, al
    call scr_wr
    inc bx
    call scr_wr
    mov bx, A2_SCR_BLO
    call scr_wr
    inc bx
    call scr_wr
    pop bx
    pop ax
    ret

; =============================================================================
; THE I/O STUBS - REAL RETURNS, NOT PARK-AT-FAIL (APPLE2-SPEC section 4.4 row 8)
;
; They are the compiled C in the package - a2io.c's a2_io_rd and a2_io_wr.
; Here they answer values the test then CHECKS, so the cdecl convention, the DS
; swap and the ES save and reload are exercised rather than merely forbidden -
; and they log every access, so "did the core call out at all?" is a question
; with an answer.
;
; The read answers `(a & 0xFF) ^ 0x5A`, which is a different byte for every
; switch and cannot be confused with RAM ($11/$99), ROM ($EE) or the slot
; ladder's $FF.
; =============================================================================
_a2_io_rd:
    push bp
    mov bp, sp
    push bx
    mov bx, [bp+4]
    mov [io_last_a], bx
    inc word [io_reads]
    mov al, bl
    xor al, 0x5A
    cmp byte [neg_io], 0            ; the negative control for row 8:
    je .out
    mov al, 0                       ; ...a stub that answers a constant
.out:
    xor ah, ah
    ; ES IS CALLER-CLOBBERED BY THE C ABI, so this stub clobbers it - always,
    ; not only under a control. The core is what has to put it back (section
    ; 3.2), and row 6's control is the one that stops it doing so.
    push ax
    mov ax, 0xBAD0
    mov es, ax
    pop ax
    pop bx
    pop bp
    ret

_a2_io_wr:
    push bp
    mov bp, sp
    push bx
    mov bx, [bp+4]
    mov [io_last_a], bx
    mov al, [bp+6]
    mov [io_last_v], al
    inc word [io_writes]
    pop bx
    pop bp
    ret

; =============================================================================
; THE ROWS
; =============================================================================
%ifndef DORMANN
rows_main:
    mov word [fails], 0

    mov si, msg_r3
    call puts
    call row_rd
    call verdict
    mov byte [neg_io], 1
    call row_rd
    call control
    mov byte [neg_io], 0

    mov si, msg_r4
    call puts
    call row_wr
    call verdict
    call patch_sta                  ; STA abs dispatched to STA zp, whose
    call row_wr                     ; one-byte operand lands the store
    call control                    ; SOMEWHERE ELSE ENTIRELY
    call unpatch_sta

    mov si, msg_r5
    call puts
    call row_fetch
    call verdict
    call patch_iofence              ; the $C000 fence moved to $D000: the
    call row_fetch                  ; region is biased to RAM
    call control
    call unpatch_iofence

    mov si, msg_r6
    call puts
    call row_seg
    call verdict
    call patch_esfix                ; the fetch segment is NOT reloaded after
    call row_seg                    ; the call out: a DELIBERATELY STALE ES
    call control
    call unpatch_esfix

    mov si, msg_r7
    call puts
    call row_scratch
    call verdict
    call patch_iofence              ; ...the same core, one page along: with
    call row_scratch                ; $CF00 biased to RAM the scratch becomes
    call control                    ; executable
    call unpatch_iofence

    mov si, msg_r8
    call puts
    call row_io
    call verdict
    mov byte [neg_io], 1
    call row_io
    call control
    mov byte [neg_io], 0

    mov si, msg_r9
    call puts
    call row_ill
    call verdict
    call patch_lax                  ; LAX dispatched to NOP
    call row_ill
    call control
    call unpatch_lax
    call patch_arr                  ; ...and ARR dispatched to AND, which is
    call row_ill                    ; the family a coarser row cannot see
    call control
    call unpatch_arr

    mov si, msg_r10
    call puts
    call row_cyc
    call verdict
    call patch_cyc                  ; one opcode's cost made wrong
    call row_cyc
    call control
    call unpatch_cyc

    mov si, msg_r2
    call puts
    call row_dec
    call verdict
    call patch_dec                  ; the decimal path is never entered
    call row_dec
    call control
    call unpatch_dec

    mov si, msg_r11
    call puts
    call row_sh
    call verdict
    call patch_sha                  ; SHA dispatched to a plain STA
    call row_sh
    call control
    call unpatch_sha

    mov si, msg_r12
    call puts
    call row_intstk
    call verdict
    call patch_brk                  ; BRK dispatched to NOP
    call row_intstk
    call control
    call unpatch_brk

    mov ax, [fails]
    or ax, ax
    jnz .bad
    mov si, msg_pass
    call puts
    mov al, 0
    jmp exit
.bad:
    mov si, msg_fail
    call puts
    mov al, 3
    jmp exit

; verdict - AX = 0 pass, 1 fail, for a row that must PASS
verdict:
    or ax, ax
    jnz .bad
    mov si, msg_ok
    call puts
    ret
.bad:
    mov si, msg_bad
    call puts
    inc word [fails]
    ret

; control - the same, for a row that must FAIL. A negative control that passes
; is a check proving nothing, and it is reported as loudly as a broken row.
control:
    or ax, ax
    jz .bad
    mov si, msg_nok
    call puts
    ret
.bad:
    mov si, msg_nbad
    call puts
    inc word [fails]
    ret

; -----------------------------------------------------------------------------
; runprog - assemble a little 6502 program at $0800 and run it.
;   SI = a cs: pointer to {length, bytes...}
; The core's answer is left in AX and a2_m is the machine afterwards.
; -----------------------------------------------------------------------------
runprog:
    mov bx, 0x0800
; runprog_at - the same, loaded at BX and started there, because a row that
; asks about a BRANCH ACROSS A PAGE cannot ask it at $0800: every branch in a
; program that begins there lands in the same page.
runprog_at:
    push es
    push di
    push bx
    mov es, [_a2_m+AM_RAMSEG]
    ; THE ANSWER BYTES ARE CLEARED FIRST. Without this a row's negative
    ; control reads the value the POSITIVE run left at the same address and
    ; passes - which is a control proving nothing, and it is exactly the
    ; failure this harness exists to make impossible.
    mov di, 0x0900
    mov cx, 0x0100
    xor al, al
    rep stosb
    pop di                          ; the load address
    push di
    xor ch, ch
    mov cl, [cs:si]
    inc si
.c: mov al, [cs:si]
    inc si
    stosb
    loop .c
    pop bx
    pop di
    pop es
    mov [_a2_m+AM_PC], bx
    mov word [_a2_m+AM_S], 0x00FD
    mov word [_a2_m+AM_P], 0x0024
    mov word [_a2_m+AM_A], 0
    mov word [_a2_m+AM_X], 0
    mov word [_a2_m+AM_Y], 0
    call rebias
    PUSHI 200
    call _a2_run
    add sp, 2
    ret

; -----------------------------------------------------------------------------
; ROW 3 - READS ACROSS EVERY APPLE II MAPPING BOUNDARY (APPLE2-SPEC section 3.2)
;
; Eight reads, one either side of each of the four boundaries, stored where the
; harness can read them back:
;
;   $BFFF  the top of RAM                              -> $1B
;   $C000  the first soft switch, through the call out  -> $00 ^ $5A = $5A
;   $C0FF  the last one                                 -> $FF ^ $5A = $A5
;   $C100  the first byte of slot space                 -> $FF
;   $CF00  THE CORE'S OWN SCRATCH PAGE                  -> $FF
;   $CFFF  the last byte of slot space                  -> $FF
;   $D000  the first byte of ROM                        -> $DD
;   $FFFF  the last                                     -> $DF
;
; **$99 IS THE POISON.** mem_init put it in the claim at every one of those
; addresses above $BFFF, so a $99 coming back is the read ladder falling
; through to RAM in a region that is not RAM on this machine.
; -----------------------------------------------------------------------------
row_rd:
    push si
    mov si, p_rd
    call runprog
    mov bx, 0x0940
    call ram_rd
    cmp al, 0x1B
    jne .bad
    mov bx, 0x0941
    call ram_rd
    cmp al, 0x5A
    jne .bad
    mov bx, 0x0942
    call ram_rd
    cmp al, 0xA5
    jne .bad
    mov bx, 0x0943
    call ram_rd
    cmp al, 0xFF
    jne .bad
    mov bx, 0x0944
    call ram_rd
    cmp al, 0xFF                    ; the SCRATCH answers the slot ladder
    jne .bad
    mov bx, 0x0945
    call ram_rd
    cmp al, 0xFF
    jne .bad
    mov bx, 0x0946
    call ram_rd
    cmp al, 0xDD
    jne .bad
    mov bx, 0x0947
    call ram_rd
    cmp al, 0xDF
    jne .bad
    xor ax, ax
    jmp short .out
.bad:
    mov ax, 1
.out:
    pop si
    ret

; LDA $BFFF / STA $0940 / LDA $C000 / STA $0941 / ... / JAM
p_rd:
    db 49
    db 0xAD, 0xFF, 0xBF
    db 0x8D, 0x40, 0x09
    db 0xAD, 0x00, 0xC0
    db 0x8D, 0x41, 0x09
    db 0xAD, 0xFF, 0xC0
    db 0x8D, 0x42, 0x09
    db 0xAD, 0x00, 0xC1
    db 0x8D, 0x43, 0x09
    db 0xAD, 0x00, 0xCF
    db 0x8D, 0x44, 0x09
    db 0xAD, 0xFF, 0xCF
    db 0x8D, 0x45, 0x09
    db 0xAD, 0x00, 0xD0
    db 0x8D, 0x46, 0x09
    db 0xAD, 0xFF, 0xFF
    db 0x8D, 0x47, 0x09
    db 0x02

; -----------------------------------------------------------------------------
; ROW 4 - WRITES ACROSS EVERY BOUNDARY, INCLUDING THE DROP ABOVE $C0FF
;
; $BFFF takes the byte. $C055 goes out through the call out and the stub logs
; it. **$C100, $CF00, $CFFF, $D000 and $FFFF ARE DROPPED** - slot space and ROM
; are not writable on this machine - so the claim still holds mem_init's poison
; at every one of them, and the io stub's write counter moved exactly once.
;
; THE DROP IS WHAT MAKES THE CORE'S SCRATCH SAFE WITH ZERO STATED DEVIATIONS
; (APPLE2-SPEC section 3.3), where the C64 needed two, so it is checked at the
; scratch page and not only at a convenient address above it.
; -----------------------------------------------------------------------------
row_wr:
    push si
    mov word [io_writes], 0
    mov si, p_wr
    call runprog
    mov bx, 0xBFFF
    call ram_rd
    cmp al, 0x5C                    ; RAM took it
    jne .bad
    cmp word [io_writes], 1         ; ...and exactly ONE write went out
    jne .bad
    cmp word [io_last_a], 0xC055
    jne .bad
    cmp byte [io_last_v], 0x5C
    jne .bad
    mov bx, 0xC100
    call ram_rd
    cmp al, 0x99                    ; the poison is untouched: DROPPED
    jne .bad
    mov bx, 0xCF00
    call ram_rd
    cmp al, 0x99
    jne .bad
    mov bx, 0xCFFF
    call ram_rd
    cmp al, 0x99
    jne .bad
    mov bx, 0xD000
    call ram_rd
    cmp al, 0x99
    jne .bad
    mov bx, 0xFFFF
    call ram_rd
    cmp al, 0x99
    jne .bad
    ; ...AND THE ROM ITSELF DID NOT MOVE, which is the other half of "a write
    ; to $D000-$FFFF is dropped": a core that biased the store into the ROM
    ; part would leave RAM's poison alone and still be wrong.
    push es
    mov es, [_a2_m+AM_ROMSEG]
    cmp byte [es:0x0000], 0xDD
    jne .badpop
    cmp byte [es:0x2FFF], 0xDF
    jne .badpop
    pop es
    xor ax, ax
    jmp short .out
.badpop:
    pop es
.bad:
    mov ax, 1
.out:
    pop si
    ret

; LDA #$5C / STA $BFFF / STA $C055 / STA $C100 / STA $CF00 / STA $CFFF
; STA $D000 / STA $FFFF / JAM
p_wr:
    db 24
    db 0xA9, 0x5C
    db 0x8D, 0xFF, 0xBF
    db 0x8D, 0x55, 0xC0
    db 0x8D, 0x00, 0xC1
    db 0x8D, 0x00, 0xCF
    db 0x8D, 0xFF, 0xCF
    db 0x8D, 0x00, 0xD0
    db 0x8D, 0xFF, 0xFF
    db 0x02

patch_sta:                          ; STA abs ($8D) -> STA zp ($85), which
    mov ax, [cs:a2_tab + 0x85*2]    ; consumes ONE operand byte and stores in
    mov [cs:a2_tab + 0x8D*2], ax    ; page zero: nothing this row asks about
    ret                             ; can then be true
unpatch_sta:
    mov word [cs:a2_tab + 0x8D*2], o_sta_abs
    ret

; -----------------------------------------------------------------------------
; ROW 5 - FETCHES ACROSS EVERY BOUNDARY, AN OPERAND STRADDLING ONE, AND A
;         BACKWARD JMP OUT OF ROM (APPLE2-SPEC section 4.1)
;
; (a) A JMP whose HIGH BYTE comes from $C000. The opcode is at $BFFE and the
;     low byte at $BFFF, so the third fetch crosses into the soft switches -
;     which no biased ES can read, so it has to come back through the same
;     call out the data path uses. The stub answers $C000 ^ $5A = $5A, so the
;     jump lands at $5A00.
; (b) A fetch WHOLLY INSIDE THE ROM, three instructions of it.
; (c) ...AND A BACKWARD JMP FROM THE ROM TO $0800. This is the LOW edge of
;     APPLE2-SPEC section 4.1's guard: a one-compare ceiling cannot see it and
;     would go on fetching RAM through the ROM's bias.
; (d) An IMMEDIATE STRADDLING $FFFF/$0000: the opcode is the last byte of the
;     address space and its operand the first. BOUND is $FFFF and not the
;     wrap, so the last byte re-biases and the operand comes from RAM.
;
; THE NEGATIVE CONTROL MOVES THE $C000 FENCE TO $D000 in the shipping text, so
; $C000-$CFFF is biased to RAM like everything below it. That is precisely the
; core that has lost the boundary this row is about: (a)'s high byte then comes
; from the claim's own poison rather than from the call out, and the jump lands
; somewhere else entirely. It cannot be done by poking the scratch, because
; _a2_run's first act is to empty BOUND so that the first fetch re-biases.
; -----------------------------------------------------------------------------
row_fetch:
    push si
    push di
    ; --- (a) the JMP whose high byte is a soft switch
    mov bx, 0xBFFE
    mov al, 0x4C                    ; JMP
    call ram_wr
    inc bx
    mov al, 0x00                    ; ...low byte
    call ram_wr
    push es
    mov es, [_a2_m+AM_RAMSEG]
    mov byte [es:0x5A00], 0xA9      ; LDA #$5A
    mov byte [es:0x5A01], 0x5A
    mov byte [es:0x5A02], 0x8D      ; STA $0950
    mov byte [es:0x5A03], 0x50
    mov byte [es:0x5A04], 0x09
    mov byte [es:0x5A05], 0x02      ; JAM
    mov byte [es:0x0950], 0
    mov byte [es:0x0951], 0
    mov byte [es:0x0952], 0
    pop es
    mov bx, 0xBFFE
    call bnd_run
    mov bx, 0x0950
    call ram_rd
    cmp al, 0x5A
    jne .bad

    ; --- (b) and (c): three instructions in the ROM, then a backward JMP out
    ; of it into RAM. The ROM image is the harness's, written through the part.
    mov bx, 0x2F00                  ; $FF00 - LDA #$5B
    mov al, 0xA9
    call rom_wr
    inc bx
    mov al, 0x5B
    call rom_wr
    inc bx
    mov al, 0x8D                    ; STA $0951
    call rom_wr
    inc bx
    mov al, 0x51
    call rom_wr
    inc bx
    mov al, 0x09
    call rom_wr
    inc bx
    mov al, 0x4C                    ; JMP $0800 - BACKWARD, out of the region
    call rom_wr
    inc bx
    mov al, 0x00
    call rom_wr
    inc bx
    mov al, 0x08
    call rom_wr
    push es
    mov es, [_a2_m+AM_RAMSEG]
    mov byte [es:0x0800], 0xA9      ; ...and what it lands on: LDA #$5D
    mov byte [es:0x0801], 0x5D
    mov byte [es:0x0802], 0x8D      ; STA $0952
    mov byte [es:0x0803], 0x52
    mov byte [es:0x0804], 0x09
    mov byte [es:0x0805], 0x02
    pop es
    mov bx, 0xFF00
    call bnd_run
    mov bx, 0x0951
    call ram_rd
    cmp al, 0x5B
    jne .bad
    mov bx, 0x0952
    call ram_rd
    cmp al, 0x5D                    ; the BACKWARD jump landed and ran
    jne .bad

    ; --- (d) an immediate straddling $FFFF / $0000
    mov bx, 0x2FFF                  ; $FFFF - LDA #
    mov al, 0xA9
    call rom_wr
    push es
    mov es, [_a2_m+AM_RAMSEG]
    mov byte [es:0x0000], 0x5E      ; ...its operand, the first byte of RAM
    mov byte [es:0x0001], 0x8D      ; STA $0953
    mov byte [es:0x0002], 0x53
    mov byte [es:0x0003], 0x09
    mov byte [es:0x0004], 0x02
    mov byte [es:0x0953], 0
    pop es
    mov bx, 0xFFFF
    call bnd_run
    mov bx, 0x0953
    call ram_rd
    cmp al, 0x5E
    jne .bad
    mov al, 0xDF                    ; ...and the ROM byte is put back, because
    mov bx, 0x2FFF                  ; row 3 reads it
    call rom_wr
    xor ax, ax
    jmp short .out
.bad:
    mov al, 0xDF
    mov bx, 0x2FFF
    call rom_wr
    mov ax, 1
.out:
    pop di
    pop si
    ret

; bnd_run - start the core at BX with a fresh machine and 200 cycles
bnd_run:
    mov [_a2_m+AM_PC], bx
    mov word [_a2_m+AM_S], 0x00FD
    mov word [_a2_m+AM_P], 0x0024
    mov word [_a2_m+AM_A], 0
    mov word [_a2_m+AM_X], 0
    mov word [_a2_m+AM_Y], 0
    call rebias
    PUSHI 200
    call _a2_run
    add sp, 2
    ret

; patch_iofence - move a2_rebias_go's $C000 compare to $D000 in the SHIPPING
; text, so the fetch biases $C000-$CFFF to RAM. `cmp si, imm16` is four bytes
; and the immediate is the last two of them.
patch_iofence:
    mov word [cs:a2_rebias_go.iofence+2], 0xD000
    ret
unpatch_iofence:
    mov word [cs:a2_rebias_go.iofence+2], A2_IO_BASE
    ret

; -----------------------------------------------------------------------------
; ROW 6 - ES AND DS COME BACK (APPLE2-SPEC section 3.2)
;
; ES is caller-clobbered by the C ABI, so the core reloads it from the cached
; fetch segment rather than trusting it. The proof is that the instruction
; AFTER a call out still fetches: the program reads a soft switch and then
; executes six more bytes out of RAM. The harness's own ES sentinel is checked
; too, because a2_run has to give KERNEL_SEG back.
; -----------------------------------------------------------------------------
row_seg:
    push si
    mov si, p_seg
    call runprog
    mov bx, 0x0960
    call ram_rd
    cmp al, 0x99
    jne .bad
    mov ax, es
    cmp ax, SEG_KERNEL
    jne .bad
    xor ax, ax
    jmp short .out
.bad:
    mov ax, 1
.out:
    pop si
    ret

; LDA $C011 / LDA #$99 / STA $0960 / JAM  - the LDA #$99 is fetched AFTER the
; call out, which is the whole question
p_seg:
    db 9
    db 0xAD, 0x11, 0xC0
    db 0xA9, 0x99
    db 0x8D, 0x60, 0x09
    db 0x02

; -----------------------------------------------------------------------------
; ROW 7 - THE SCRATCH PAGE IS NOT REACHABLE FROM THE EMULATED MACHINE
;         (APPLE2-SPEC section 3.3)
;
; The core keeps its countdown, its dirty bitmap and its fetch bias in 256
; bytes at $CF00 OF THE CLAIM, and the whole claim of "ZERO stated deviations"
; rests on nothing in the emulated machine being able to see them. Three
; questions:
;
;   (a) a READ of $CF24 - the countdown's own address - answers the slot
;       ladder's $FF and not the counter;
;   (b) a WRITE of $00 to $CF24 does not stop the machine: the program runs on
;       and reaches its store;
;   (c) EXECUTING at $CF10 runs $FF (ISC $FFFF), not the bytes the harness put
;       there behind the ladder's back - so a program cannot jump into the
;       scratch and find code.
;
; The NEGATIVE CONTROL moves the $C000 fence to $D000 (see row 5), which biases
; $CF00 to RAM - precisely the core that WOULD execute the harness's bytes.
; -----------------------------------------------------------------------------
row_scratch:
    push si
    ; (c)'s bytes, written straight into the claim - the emulated machine has
    ; no way to put them there
    mov bx, 0xCF10
    mov al, 0xA9                    ; LDA #$C5
    call ram_wr
    inc bx
    mov al, 0xC5
    call ram_wr
    inc bx
    mov al, 0x8D                    ; STA $0972
    call ram_wr
    inc bx
    mov al, 0x72
    call ram_wr
    inc bx
    mov al, 0x09
    call ram_wr
    inc bx
    mov al, 0x02                    ; JAM
    call ram_wr

    ; (a) and (b)
    mov si, p_scr
    call runprog
    mov bx, 0x0970
    call ram_rd
    cmp al, 0xFF                    ; the countdown's address reads $FF
    jne .bad
    mov bx, 0x0971
    call ram_rd
    cmp al, 0xC4                    ; ...and the machine ran on past the write
    jne .bad

    ; (c) - and it runs under the control's moved fence too, which is what
    ; makes the control fail
    push es
    mov es, [_a2_m+AM_RAMSEG]
    mov byte [es:0x0972], 0
    pop es
    mov bx, 0xCF10
    call bnd_run
    mov bx, 0x0972
    call ram_rd
    or al, al                       ; NOTHING was executed out of the scratch
    jnz .bad
    xor ax, ax
    jmp short .out
.bad:
    mov ax, 1
.out:
    pop si
    ret

; LDA $CF24 / STA $0970 / LDA #$00 / STA $CF24 / LDA #$C4 / STA $0971 / JAM
p_scr:
    db 17
    db 0xAD, 0x24, 0xCF
    db 0x8D, 0x70, 0x09
    db 0xA9, 0x00
    db 0x8D, 0x24, 0xCF
    db 0xA9, 0xC4
    db 0x8D, 0x71, 0x09
    db 0x02

; -----------------------------------------------------------------------------
; ROW 8 - THE SOFT-SWITCH STUB RETURNS FOR READS AS WELL AS WRITES
;         (APPLE2-SPEC section 5.3)
;
; $C000-$C0FF goes out to the C in BOTH directions, and what comes back from a
; READ is what the C said - not RAM, not ROM, not zero. **A C side that treats
; a read as pure gives a machine that boots to `]` and then never changes video
; mode**, because the ROM sets TEXT with `LDA $C051`. That is what this row is
; for and why it is not optional.
; -----------------------------------------------------------------------------
row_io:
    push si
    mov word [io_reads], 0
    mov word [io_writes], 0
    mov si, p_io
    call runprog
    mov bx, 0x0930
    call ram_rd
    cmp al, 0x51 ^ 0x5A             ; $C051's stub answer, off a READ
    jne .bad
    cmp word [io_reads], 0
    je .bad
    cmp word [io_writes], 0
    je .bad
    cmp word [io_last_a], 0xC030
    jne .bad
    cmp byte [io_last_v], 0x0E
    jne .bad
    xor ax, ax
    jmp short .out
.bad:
    mov ax, 1
.out:
    pop si
    ret

; LDA $C051 / STA $0930 / LDA #$0E / STA $C030 / JAM
p_io:
    db 12
    db 0xAD, 0x51, 0xC0
    db 0x8D, 0x30, 0x09
    db 0xA9, 0x0E
    db 0x8D, 0x30, 0xC0
    db 0x02

; -----------------------------------------------------------------------------
; ROW 9 - THE ILLEGAL OPCODES (VICE's src/6510core.c)
; LAX, SAX, DCP, ARR (binary AND decimal), ANE, ALR, ANC and SBX, each with an
; answer only that opcode produces.
; -----------------------------------------------------------------------------
row_ill:
    push si
    mov si, p_ill
    call runprog
    mov bx, 0x0960
    call ram_rd
    cmp al, 0x3C                    ; LAX $0970 -> A = X = $3C
    jne .bad
    mov bx, 0x0961
    call ram_rd
    cmp al, 0x3C
    jne .bad
    mov bx, 0x0962
    call ram_rd
    cmp al, 0x18                    ; SAX: A & X with A = $18 after the AND
    jne .bad
    mov bx, 0x0963
    call ram_rd
    cmp al, 0x3B                    ; DCP decremented $0970
    jne .bad
    mov si, p_ill2
    call runprog
    mov bx, 0x0964
    call ram_rd
    cmp al, 0x78                    ; ARR #$F0 on $FF, C clear: $F0 >> 1
    jne .bad
    mov bx, 0x0965
    call ram_rd
    cmp al, 0x35                    ; ...N=0 V=0 Z=0 C=1 (B and I and bit 5)
    jne .bad
    mov bx, 0x0966
    call ram_rd
    cmp al, 0x55                    ; ARR in DECIMAL: both BCD fixups, and
    jne .bad                        ; the high one's ninth-bit carry
    mov bx, 0x0967
    call ram_rd
    cmp al, 0xBD                    ; ...N = the INCOMING carry, C from the
    jne .bad                        ; high fixup, D still set
    mov bx, 0x0968
    call ram_rd
    cmp al, 0xEF                    ; ANE: (A | $EF) & X & imm - $EF is
    jne .bad                        ; ANE_MAGIC, $EE is the RDY one
    mov bx, 0x0969
    call ram_rd
    cmp al, 0x01                    ; ALR #$03 on $FF: ($FF & 3) >> 1
    jne .bad
    mov bx, 0x096A
    call ram_rd
    cmp al, 0x80                    ; ANC #$80 on $FF
    jne .bad
    mov bx, 0x096B
    call ram_rd
    cmp al, 0xB5                    ; ...and ANC puts bit 7 into the CARRY
    jne .bad
    mov bx, 0x096C
    call ram_rd
    cmp al, 0xE0                    ; SBX: (A & X) - imm, with a CMP's carry
    jne .bad
    xor ax, ax
    jmp short .out
.bad:
    mov ax, 1
.out:
    pop si
    ret

; LDA #$3C / STA $0970 / LDA #0 / LDX #0
; LAX $0970          (A = X = $3C)
; STA $0960 / STX $0961
; AND #$18           (A = $18, X still $3C)
; SAX $0962          ($18 & $3C = $18)
; DCP $0970          ($0970 -> $3B)
; LDA $0970 / STA $0963 / JAM
p_ill:
    db 33
    db 0xA9, 0x3C
    db 0x8D, 0x70, 0x09
    db 0xA9, 0x00
    db 0xA2, 0x00
    db 0xAF, 0x70, 0x09
    db 0x8D, 0x60, 0x09
    db 0x8E, 0x61, 0x09
    db 0x29, 0x18
    db 0x8F, 0x62, 0x09
    db 0xCF, 0x70, 0x09
    db 0xAD, 0x70, 0x09
    db 0x8D, 0x63, 0x09
    db 0x02

; CLC / LDA #$FF / ARR #$F0 / STA $0964 / PHP / PLA / STA $0965
; SED / SEC / LDA #$FF / ARR #$FF / STA $0966 / PHP / PLA / STA $0967 / CLD
; LDA #$00 / LDX #$FF / ANE #$FF / STA $0968
; LDA #$FF / ALR #$03 / STA $0969
; LDA #$FF / ANC #$80 / STA $096A / PHP / PLA / STA $096B
; LDA #$FF / LDX #$F0 / SBX #$10 / STX $096C / JAM
p_ill2:
    db 66
    db 0x18
    db 0xA9, 0xFF
    db 0x6B, 0xF0
    db 0x8D, 0x64, 0x09
    db 0x08, 0x68
    db 0x8D, 0x65, 0x09
    db 0xF8
    db 0x38
    db 0xA9, 0xFF
    db 0x6B, 0xFF
    db 0x8D, 0x66, 0x09
    db 0x08, 0x68
    db 0x8D, 0x67, 0x09
    db 0xD8
    db 0xA9, 0x00
    db 0xA2, 0xFF
    db 0x8B, 0xFF
    db 0x8D, 0x68, 0x09
    db 0xA9, 0xFF
    db 0x4B, 0x03
    db 0x8D, 0x69, 0x09
    db 0xA9, 0xFF
    db 0x0B, 0x80
    db 0x8D, 0x6A, 0x09
    db 0x08, 0x68
    db 0x8D, 0x6B, 0x09
    db 0xA9, 0xFF
    db 0xA2, 0xF0
    db 0xCB, 0x10
    db 0x8E, 0x6C, 0x09
    db 0x02

patch_lax:                          ; LAX abs ($AF) -> NOP implied
    mov ax, [cs:a2_tab + 0xEA*2]
    mov [cs:a2_tab + 0xAF*2], ax
    ret
unpatch_lax:
    mov word [cs:a2_tab + 0xAF*2], o_lax_abs
    ret

patch_arr:                          ; ARR # ($6B) -> AND # , which has the
    mov ax, [cs:a2_tab + 0x29*2]    ; same operand and a plausible answer
    mov [cs:a2_tab + 0x6B*2], ax
    ret
unpatch_arr:
    mov word [cs:a2_tab + 0x6B*2], o_arr
    ret

; patch_esfix - four NOPs over `mov es, [FES]` at the end of a2_io_rd_bx, so
; the core comes back from a call out with the stub's garbage ES still in it.
; That is the ONLY honest way to make a stale fetch segment: the C ABI says ES
; is the callee's to lose, so a stub that clobbers it is behaving correctly and
; the reload is the thing under test (APPLE2-SPEC section 3.2).
patch_esfix:
    mov ax, [cs:a2_io_rd_esfix]
    mov [esfix_save], ax
    mov ax, [cs:a2_io_rd_esfix+2]
    mov [esfix_save+2], ax
    mov word [cs:a2_io_rd_esfix], 0x9090
    mov word [cs:a2_io_rd_esfix+2], 0x9090
    ret
unpatch_esfix:
    mov ax, [esfix_save]
    mov [cs:a2_io_rd_esfix], ax
    mov ax, [esfix_save+2]
    mov [cs:a2_io_rd_esfix+2], ax
    ret

; -----------------------------------------------------------------------------
; ROW 10 - CYCLE TOTALS PER OPCODE FAMILY (APPLE2-SPEC section 4.2)
;
; The core is given a budget and the leftover says exactly how much it spent.
; IT IS TABLE-DRIVEN, because three shapes let a cycle table be wrong in almost
; any other family and still pass: each entry is {program, expected cycles},
; the expectations are VICE's own per-family costs (src/6510core.c's tables and
; its branch macro at :978, which charges the taken and the crossing cycles
; separately), and every program ends in JAM, which is 2.
; -----------------------------------------------------------------------------
row_cyc:
    push si
    push di
    push es
    mov es, [_a2_m+AM_RAMSEG]
    mov byte [es:0x1100], 0x77
    pop es
    mov di, p_cyc_tab
.next:
    mov si, [cs:di]
    or si, si
    jz .special
    call runprog
    mov ax, [_a2_m+AM_CNT]
    neg ax
    add ax, 200                     ; cycles spent
    cmp ax, [cs:di+2]
    jne .bad
    add di, 4
    jmp short .next

.special:
    ; --- A TAKEN BRANCH THAT REALLY CROSSES A PAGE. Loaded at $08FB, so the
    ; BEQ's own bytes are at $08FD-$08FE, its next PC is $08FF and its target
    ; $0900: one page on.
    mov si, p_cyc_cross
    mov bx, 0x08FB
    call runprog_at
    mov ax, [_a2_m+AM_CNT]
    neg ax
    add ax, 200
    cmp ax, 2 + 4 + 2               ; LDA #0, BEQ taken ACROSS a page, JAM
    jne .bad

    ; --- BRK IS SEVEN CYCLES, ONCE (6510core.c:998). It is an OPCODE, so the
    ; dispatch charges its table entry; entering the interrupt vector path
    ; would charge seven MORE, and 14 is what that defect costs.
    mov bx, 0x2FFE                  ; the IRQ vector, IN THE ROM - on this
    mov al, 0x00                    ; machine $FFFA-$FFFF are ROM and not RAM,
    call rom_wr                     ; which is the one place this row differs
    inc bx                          ; from the C64's
    mov al, 0x0B
    call rom_wr
    push es
    mov es, [_a2_m+AM_RAMSEG]
    mov byte [es:0x0B00], 0x02      ; the handler is one JAM
    pop es
    mov si, p_cyc_brk
    call runprog
    mov ax, [_a2_m+AM_CNT]
    neg ax
    add ax, 200
    cmp ax, 7 + 2                   ; BRK (7) + the handler's JAM (2)
    jne .bad

    ; --- ...AND AN IRQ ENTRY IS SEVEN, which is where the seven belongs.
    mov word [_a2_m+AM_PC], 0x0800
    mov word [_a2_m+AM_S], 0x00FD
    mov word [_a2_m+AM_P], 0x0020   ; I clear
    push es
    mov es, [_a2_m+AM_RAMSEG]
    mov byte [es:0x0800], 0x02
    pop es
    call rebias
    mov bx, A2_SCR_IRQ
    mov al, 1
    call scr_wr
    PUSHI 200
    call _a2_run
    add sp, 2
    mov ax, [_a2_m+AM_CNT]
    neg ax
    add ax, 200
    cmp ax, 7 + 2                   ; the vector (7) + the handler's JAM
    jne .bad
    mov bx, A2_SCR_IRQ
    xor al, al
    call scr_wr

    xor ax, ax
    jmp short .out
.bad:
    mov ax, 1
.out:
    pop di
    pop si
    ret

; --- the table: {program, cycles}, and every program ends in JAM (2) --------
p_cyc_tab:
    dw p_cyc_a,    2 + 2 + 5 + 2    ; LDA abs,X ACROSS a page: +1
    dw p_cyc_b,    2 + 2 + 4 + 2    ; ...and not across it
    dw p_cyc_c,    2 + 3 + 2        ; BEQ taken, same page
    dw p_cyc_nt,   2 + 2 + 2        ; BEQ not taken
    dw p_cyc_zp,   3 + 2            ; LDA zp
    dw p_cyc_zpx,  2 + 4 + 2        ; LDA zp,X
    dw p_cyc_abs,  4 + 2            ; LDA abs
    dw p_cyc_stax, 2 + 5 + 2        ; STA abs,X - a WRITE never pays the cross
    dw p_cyc_aslz, 5 + 2            ; ASL zp
    dw p_cyc_asla, 6 + 2            ; ASL abs
    dw p_cyc_incx, 2 + 7 + 2        ; INC abs,X
    dw p_cyc_stk,  2 + 3 + 4 + 2    ; PHA / PLA
    dw p_cyc_jsr,  6 + 6 + 2        ; JSR / RTS
    dw p_cyc_jmp,  3 + 2            ; JMP abs
    dw p_cyc_jmpi, 2 + 3 + 2 + 3 + 5 + 2    ; JMP (ind)
    dw p_cyc_indy, 2 + 3 + 2 + 3 + 2 + 5 + 2    ; LDA (zp),Y, no cross
    dw p_cyc_indyp,2 + 3 + 2 + 3 + 2 + 6 + 2    ; ...and across a page
    dw p_cyc_indx, 2 + 3 + 2 + 3 + 2 + 6 + 2    ; LDA (zp,X)
    dw 0

; LDA #$00 / LDX #$01 / LDA $10FF,X / JAM
p_cyc_a:
    db 8
    db 0xA9, 0x00
    db 0xA2, 0x01
    db 0xBD, 0xFF, 0x10
    db 0x02
; LDA #$00 / LDX #$01 / LDA $1000,X / JAM
p_cyc_b:
    db 8
    db 0xA9, 0x00
    db 0xA2, 0x01
    db 0xBD, 0x00, 0x10
    db 0x02
; LDA #$00 / BEQ +1 / JAM / JAM
p_cyc_c:
    db 6
    db 0xA9, 0x00
    db 0xF0, 0x01
    db 0x02
    db 0x02
; LDA #$01 / BEQ +1 / JAM / JAM   - NOT taken, so the branch is 2
p_cyc_nt:
    db 6
    db 0xA9, 0x01
    db 0xF0, 0x01
    db 0x02
    db 0x02
; LDA $10 / JAM
p_cyc_zp:
    db 3
    db 0xA5, 0x10
    db 0x02
; LDX #$01 / LDA $10,X / JAM
p_cyc_zpx:
    db 5
    db 0xA2, 0x01
    db 0xB5, 0x10
    db 0x02
; LDA $1234 / JAM
p_cyc_abs:
    db 4
    db 0xAD, 0x34, 0x12
    db 0x02
; LDX #$01 / STA $12FF,X / JAM  - a STORE pays no page-cross cycle
p_cyc_stax:
    db 6
    db 0xA2, 0x01
    db 0x9D, 0xFF, 0x12
    db 0x02
; ASL $10 / JAM
p_cyc_aslz:
    db 3
    db 0x06, 0x10
    db 0x02
; ASL $1234 / JAM
p_cyc_asla:
    db 4
    db 0x0E, 0x34, 0x12
    db 0x02
; LDX #$01 / INC $1200,X / JAM
p_cyc_incx:
    db 6
    db 0xA2, 0x01
    db 0xFE, 0x00, 0x12
    db 0x02
; LDA #$12 / PHA / PLA / JAM
p_cyc_stk:
    db 5
    db 0xA9, 0x12
    db 0x48
    db 0x68
    db 0x02
; JSR $0806 / JAM / pad / RTS
p_cyc_jsr:
    db 7
    db 0x20, 0x06, 0x08
    db 0x02
    db 0xEA, 0xEA
    db 0x60
; JMP $0803 / JAM
p_cyc_jmp:
    db 4
    db 0x4C, 0x03, 0x08
    db 0x02
; LDA #$0B / STA $10 / LDA #$08 / STA $11 / JMP ($0010) / JAM
p_cyc_jmpi:
    db 12
    db 0xA9, 0x0B
    db 0x85, 0x10
    db 0xA9, 0x08
    db 0x85, 0x11
    db 0x6C, 0x10, 0x00
    db 0x02
; LDA #$00 / STA $10 / LDA #$12 / STA $11 / LDY #$01 / LDA ($10),Y / JAM
p_cyc_indy:
    db 13
    db 0xA9, 0x00
    db 0x85, 0x10
    db 0xA9, 0x12
    db 0x85, 0x11
    db 0xA0, 0x01
    db 0xB1, 0x10
    db 0x02
; ...and the same with the pointer at $12FF, so the index carries
p_cyc_indyp:
    db 13
    db 0xA9, 0xFF
    db 0x85, 0x10
    db 0xA9, 0x12
    db 0x85, 0x11
    db 0xA0, 0x01
    db 0xB1, 0x10
    db 0x02
; LDA #$00 / STA $12 / LDA #$12 / STA $13 / LDX #$02 / LDA ($10,X) / JAM
p_cyc_indx:
    db 13
    db 0xA9, 0x00
    db 0x85, 0x12
    db 0xA9, 0x12
    db 0x85, 0x13
    db 0xA2, 0x02
    db 0xA1, 0x10
    db 0x02
; LDA #$00 / BEQ +1 / JAM / JAM  - LOADED AT $08FB, so the branch crosses
p_cyc_cross:
    db 6
    db 0xA9, 0x00
    db 0xF0, 0x01
    db 0x02
    db 0x02
; BRK / pad - the handler is a JAM at the IRQ vector
p_cyc_brk:
    db 2
    db 0x00, 0xEA

patch_cyc:                          ; LDA abs,X costs one cycle too few
    mov byte [cs:a2_cyc + 0xBD], 3
    ret
unpatch_cyc:
    mov byte [cs:a2_cyc + 0xBD], 4
    ret

; -----------------------------------------------------------------------------
; ROW 2 - DECIMAL MODE, AGAINST AN INDEPENDENT REFERENCE
;
; Klaus Dormann's 6502_decimal_test is published as SOURCE only - there is no
; built binary in bin_files, which is why APPLE2-SPEC section 4.4's decimal row
; is this instead, and says so: tools/c64dec.py computes, from the documented
; NMOS rules and in Python, a 16-bit checksum over ALL 262,144 cases (ADC and
; SBC, every A, every operand, carry in both ways), and the core is run over
; the same 262,144 cases here and must produce the same four checksums.
;
; IT IS THE SAME ORACLE THE C64 CORE RUNS, which is APPLE2-SPEC section 4.1's
; whole mitigation for the derived copy: a bug in the shared 93% has to be
; fixed twice, and both cores are held to one reference so that the second
; core's copy of it cannot drift unnoticed.
; -----------------------------------------------------------------------------
row_dec:
    push si
    mov byte [decsw], 0             ; 0 ADC C=0, 1 ADC C=1, 2 SBC C=0, 3 SBC C=1
.sweep:
    call dec_sweep                  ; -> AX = the checksum
    push ax
    mov al, [decsw]
    xor ah, ah
    mov si, ax
    shl si, 1
    pop ax
    cmp ax, [cs:si+dec_want]
    jne .bad
    inc byte [decsw]
    cmp byte [decsw], 4
    jb .sweep
    xor ax, ax
    jmp short .out
.bad:
    mov ax, 1
.out:
    pop si
    ret

; dec_sweep - run 65,536 cases and fold A and P into a 16-bit checksum. The
; program is four bytes: SED, the operation, JAM - and the harness sets A, the
; operand and C directly, which is what makes 65,536 runs of it affordable.
dec_sweep:
    push bx
    push cx
    push dx
    mov word [dec_sum], 0
    mov word [dec_am], 0            ; the accumulator and the operand, IN
.a:                                 ; MEMORY: _a2_run is cdecl and AX BX CX DX
    push es                         ; SI DI are all the caller's to lose
    mov es, [_a2_m+AM_RAMSEG]
    mov byte [es:0x0800], 0xF8      ; SED
    mov al, 0x69                    ; ADC #
    test byte [decsw], 2
    jz .isadc
    mov al, 0xE9                    ; SBC #
.isadc:
    mov [es:0x0801], al
    mov al, [dec_am]                ; the operand
    mov [es:0x0802], al
    mov byte [es:0x0803], 0x02      ; JAM
    pop es
    mov word [_a2_m+AM_PC], 0x0800
    mov word [_a2_m+AM_S], 0x00FD
    mov al, 0x20
    test byte [decsw], 1
    jz .noc
    or al, 0x01                     ; C set going in
.noc:
    xor ah, ah
    mov [_a2_m+AM_P], ax
    mov al, [dec_am+1]              ; the accumulator
    xor ah, ah
    mov [_a2_m+AM_A], ax
    call rebias
    PUSHI 40
    call _a2_run
    add sp, 2
    ; fold A and the N, V, Z and C bits of P into the checksum
    mov ax, [_a2_m+AM_A]
    mov bx, [_a2_m+AM_P]
    and bl, 0xC3                    ; N V Z C
    mov bh, al
    mov ax, [dec_sum]
    rol ax, 1
    add ax, bx                      ; an ADD and not an XOR: an exclusive-or
    mov [dec_sum], ax               ; fold over a symmetric sweep of exactly
                                    ; 65,536 cases cancels to zero, and all
                                    ; four checksums would have been 0x0000
    inc byte [dec_am]
    jnz .a
    inc byte [dec_am+1]
    jnz .a
    mov ax, [dec_sum]
    pop dx
    pop cx
    pop bx
    ret

patch_dec:                          ; SED dispatched to CLD: the decimal path
    mov ax, [cs:a2_tab + 0xD8*2]    ; is never taken and every answer is binary
    mov [cs:a2_tab + 0xF8*2], ax
    ret
unpatch_dec:
    mov word [cs:a2_tab + 0xF8*2], o_sed
    ret

; -----------------------------------------------------------------------------
; ROW 11 - THE FOUR UNSTABLE STORES (6510core.c:1769-1822)
;
; SHA, SHX, SHY and SHS mask the register with the UNINDEXED base's high byte
; plus one, and on a page cross the target's own high byte becomes the value
; (STORE_ABS_SH_Y, :699-711).
;
;   SHA $1FF0,Y  Y=$10  A=X=$FF   value = $FF & ($1F+1) = $20  at $2000
;   SHX $1EF0,Y  Y=$10  X=$FF     value = $FF & $1F = $1F      at $1F00
;   SHY $1DF0,X  X=$10  Y=$FF     value = $FF & $1E = $1E      at $1E00
;   SHS $1CF0,Y  Y=$10  A=X=$FF   value = $1D at $1D00, and S = A & X = $FF
;   SHA $1FF8,Y  Y=$10  A=$FF X=$0F
;                value = $0F & $20 = 0, and the cross puts the VALUE in the
;                high byte: the store lands at $0008 and NOT at $2008
; -----------------------------------------------------------------------------
row_sh:
    push si
    push es
    mov es, [_a2_m+AM_RAMSEG]
    mov byte [es:0x2000], 0
    mov byte [es:0x1F00], 0
    mov byte [es:0x1E00], 0
    mov byte [es:0x1D00], 0
    mov byte [es:0x0008], 0xAA      ; the corrupted target...
    mov byte [es:0x2008], 0xBB      ; ...and the one it must NOT reach
    pop es
    mov si, p_sh
    call runprog
    mov bx, 0x2000
    call ram_rd
    cmp al, 0x20
    jne .bad
    mov bx, 0x1F00
    call ram_rd
    cmp al, 0x1F
    jne .bad
    mov bx, 0x1E00
    call ram_rd
    cmp al, 0x1E
    jne .bad
    mov bx, 0x1D00
    call ram_rd
    cmp al, 0x1D
    jne .bad
    mov ax, [_a2_m+AM_S]
    cmp al, 0xFF                    ; SHS also loads S with A & X
    jne .bad
    mov bx, 0x0008
    call ram_rd
    or al, al                       ; the page cross put the store HERE...
    jne .bad
    mov bx, 0x2008
    call ram_rd
    cmp al, 0xBB                    ; ...and not at the uncorrupted address
    jne .bad
    xor ax, ax
    jmp short .out
.bad:
    mov ax, 1
.out:
    pop si
    ret

p_sh:
    db 34
    db 0xA9, 0xFF                   ; LDA #$FF
    db 0xA2, 0xFF                   ; LDX #$FF
    db 0xA0, 0x10                   ; LDY #$10
    db 0x9F, 0xF0, 0x1F             ; SHA $1FF0,Y
    db 0x9E, 0xF0, 0x1E             ; SHX $1EF0,Y
    db 0xA2, 0x10                   ; LDX #$10
    db 0xA0, 0xFF                   ; LDY #$FF
    db 0x9C, 0xF0, 0x1D             ; SHY $1DF0,X
    db 0xA9, 0xFF                   ; LDA #$FF
    db 0xA2, 0xFF                   ; LDX #$FF
    db 0xA0, 0x10                   ; LDY #$10
    db 0x9B, 0xF0, 0x1C             ; SHS $1CF0,Y
    db 0xA2, 0x0F                   ; LDX #$0F
    db 0x9F, 0xF8, 0x1F             ; SHA $1FF8,Y - and it CROSSES
    db 0x02

patch_sha:                          ; SHA abs,Y ($9F) -> STA abs,Y, which
    mov ax, [cs:a2_tab + 0x99*2]    ; stores the WHOLE accumulator
    mov [cs:a2_tab + 0x9F*2], ax
    ret
unpatch_sha:
    mov word [cs:a2_tab + 0x9F*2], o_sha_absy
    ret

; -----------------------------------------------------------------------------
; ROW 12 - WHAT AN INTERRUPT PUTS ON THE STACK, AND WHAT RTI TAKES OFF
;
; VICE pushes PC high, PC low, then the status byte with B CLEAR for a hardware
; interrupt (6510core.c:456's LOCAL_SET_BREAK(0) then the three PUSHes) and B
; SET for BRK (:1000). So this row reads $01FD, $01FC and $01FB by name, checks
; S landed at $FA, and then makes BRK come back through RTI.
;
; **THE VECTORS ARE IN THE ROM AND NOT IN RAM**, which is where this row parts
; company with the C64's: on a 48K II+ $FFFA-$FFFF are the Autostart Monitor's
; and a write there is dropped. They are written through the ROM PART.
;
; NOTHING IN THIS PORT RAISES AN IRQ OR AN NMI (APPLE2-SPEC section 4.2) - a
; bare II+ has no timer and no interrupting card - so the line is raised HERE,
; in the scratch, exactly as an alarm would have. The machinery is kept because
; BRK and RTI are opcodes and a program uses them.
; -----------------------------------------------------------------------------
row_intstk:
    push si
    mov bx, 0x2FFA                  ; $FFFA, the NMI vector -> $0A00
    mov al, 0x00
    call rom_wr
    inc bx
    mov al, 0x0A
    call rom_wr
    mov bx, 0x2FFE                  ; $FFFE, the IRQ vector -> $0B00
    mov al, 0x00
    call rom_wr
    inc bx
    mov al, 0x0B
    call rom_wr
    push es
    mov es, [_a2_m+AM_RAMSEG]
    mov byte [es:0x0A00], 0x02      ; both handlers are one JAM, so the stack
    mov byte [es:0x0B00], 0x02      ; is read exactly as the entry left it
    mov byte [es:0x01FD], 0
    mov byte [es:0x01FC], 0
    mov byte [es:0x01FB], 0
    pop es
    ; --- THE IRQ: PC high, PC low, then P with B CLEAR
    mov word [_a2_m+AM_PC], 0x0800
    mov word [_a2_m+AM_S], 0x00FD
    mov word [_a2_m+AM_P], 0x0020   ; I clear, bit 5 as always
    call rebias
    mov bx, A2_SCR_IRQ
    mov al, 1
    call scr_wr
    PUSHI 200
    call _a2_run
    add sp, 2
    mov bx, A2_SCR_IRQ
    xor al, al
    call scr_wr
    mov bx, 0x01FD
    call ram_rd
    cmp al, 0x08                    ; PC high FIRST
    jne .bad
    mov bx, 0x01FC
    call ram_rd
    cmp al, 0x00                    ; ...then PC low
    jne .bad
    mov bx, 0x01FB
    call ram_rd
    cmp al, 0x20                    ; ...then P, B CLEAR (6510core.c:456)
    jne .bad
    mov ax, [_a2_m+AM_S]
    cmp al, 0xFA                    ; three bytes off a stack that began $FD
    jne .bad
    ; --- THE NMI pushes the same shape, with I set in the P it pushes
    push es
    mov es, [_a2_m+AM_RAMSEG]
    mov byte [es:0x01FB], 0
    pop es
    mov word [_a2_m+AM_PC], 0x0800
    mov word [_a2_m+AM_S], 0x00FD
    mov word [_a2_m+AM_P], 0x0024   ; I SET: the NMI is not masked by it
    call rebias
    mov bx, A2_SCR_IRQ
    mov al, 2
    call scr_wr
    PUSHI 200
    call _a2_run
    add sp, 2
    mov bx, 0x01FB
    call ram_rd
    cmp al, 0x24                    ; B CLEAR on an NMI too
    jne .bad
    ; ...and the EDGE was consumed
    mov bx, A2_SCR_IRQ
    call scr_rd
    test al, 2
    jnz .bad
    ; --- BRK: B SET, the return address is PC + 2, and RTI takes it back
    push es
    mov es, [_a2_m+AM_RAMSEG]
    mov byte [es:0x0800], 0x00      ; BRK
    mov byte [es:0x0801], 0xEA      ; ...its padding byte
    mov byte [es:0x0802], 0x02      ; ...and where RTI must come back to
    mov byte [es:0x0B00], 0x40      ; the handler is RTI
    mov byte [es:0x01FB], 0
    pop es
    mov word [_a2_m+AM_PC], 0x0800
    mov word [_a2_m+AM_S], 0x00FD
    mov word [_a2_m+AM_P], 0x0020
    call rebias
    PUSHI 200
    call _a2_run
    add sp, 2
    mov bx, 0x01FD
    call ram_rd
    cmp al, 0x08
    jne .bad
    mov bx, 0x01FC
    call ram_rd
    cmp al, 0x02                    ; BRK pushes PC + 2, not PC + 1
    jne .bad
    mov bx, 0x01FB
    call ram_rd
    cmp al, 0x30                    ; ...and B is SET (6510core.c:1000)
    jne .bad
    mov ax, [_a2_m+AM_PC]
    cmp ax, 0x0802                  ; RTI came back to the padding's byte
    jne .bad
    mov ax, [_a2_m+AM_S]
    cmp al, 0xFD                    ; ...and put all three bytes back
    jne .bad
    xor ax, ax
    jmp short .out
.bad:
    mov ax, 1
.out:
    pop si
    ret

patch_brk:                          ; BRK ($00) -> NOP implied: nothing is
    mov ax, [cs:a2_tab + 0xEA*2]    ; pushed and RTI has nothing to take back
    mov [cs:a2_tab + 0x00*2], ax
    ret
unpatch_brk:
    mov word [cs:a2_tab + 0x00*2], o_brk
    ret
%endif

; =============================================================================
; ROW 1 - KLAUS DORMANN'S 6502 FUNCTIONAL TEST
;
; The 64KB image is appended to the floppy by the script and read straight into
; the Apple's RAM, and its top 12KB is ALSO copied into the ROM PART - because
; on a 48K II+ $D000-$FFFF is not RAM, so a core that read the test's own
; high memory out of the claim would be the very defect rows 3 and 5 exist to
; forbid. The test starts at $0400 and ends by looping on itself: the success
; trap is a `jmp *` at the address the script passes in, and any other trap is
; a `jmp *` somewhere else. So the judgement is where the program counter
; SETTLES, which is exactly how the test is documented to be read.
; =============================================================================
%ifdef DORMANN
dormann_main:
    mov si, msg_dorm
    call puts
    ; read DORMSEC.. into the RAM claim, 128 sectors = 64KB
    push es
    mov ax, SEG_RAM
    mov es, ax
    xor bx, bx
    mov word [lba], DORMSEC
    mov cx, 128
.rd:
    call rdsec
    add bx, 512
    inc word [lba]
    loop .rd
    pop es
    ; ...and $D000-$FFFF of it into the ROM PART, which is what answers those
    ; addresses on this machine
    push ds
    push es
    push si
    push di
    mov ax, SEG_RAM
    mov ds, ax
    mov ax, SEG_ROM
    mov es, ax
    mov si, 0xD000
    xor di, di
    mov cx, 0x1800
    rep movsw
    pop di
    pop si
    pop es
    pop ds
    ; ...AND THE CORE'S SCRATCH IS CLEARED AFTER THE LOAD. The fixture is a
    ; whole 64KB image and it lands ON TOP of $CF00-$CFFF, so whatever the test
    ; has at those addresses becomes the pending-interrupt byte, the countdown
    ; and the dirty bitmap. apple2.c does the same thing at launch
    ; (a2_scratch_clear).
    push es
    mov es, [_a2_m+AM_RAMSEG]
    mov di, A2_SCR_BASE
    mov cx, A2_SCR_END
    xor al, al
    rep stosb
    pop es

    mov word [_a2_m+AM_PC], 0x0400
    mov word [_a2_m+AM_S], 0x00FD
    mov word [_a2_m+AM_P], 0x0024
    mov word [_a2_m+AM_A], 0
    mov word [_a2_m+AM_X], 0
    mov word [_a2_m+AM_Y], 0
    call rebias
%ifdef NEG
    ; THE NEGATIVE CONTROL: ADC # dispatched to ORA #, which Dormann's test
    ; notices within its first few hundred instructions. A run that reaches
    ; the success trap with this in place would mean the test was not being
    ; run at all.
    mov ax, [cs:a2_tab + 0x09*2]
    mov [cs:a2_tab + 0x69*2], ax
    mov si, msg_neg
    call puts
%endif
    ; 30,000 AND NOT 60,000: the countdown is a SIGNED word (APPLE2-SPEC
    ; section 4.2), so a budget over 32,767 arrives negative and the core
    ; expires before its first fetch - which reads exactly like a test that
    ; will not start. The package's own cap is 16,384 for the same reason.
    mov cx, 20000                   ; outer passes of 30,000 cycles each
.run:
    push cx
    PUSHI 30000
    call _a2_run
    add sp, 2
    pop cx
    cmp ax, A2_RUN_JAM
    je .stuck
    ; ...has the program counter settled on a `jmp *`?
    mov ax, [_a2_m+AM_PC]
    cmp ax, [cs:last_pc]
    jne .moved
    inc word [cs:same_n]
    cmp word [cs:same_n], 3
    jae .settled
    jmp short .cont
.moved:
    mov [cs:last_pc], ax
    mov word [cs:same_n], 0
.cont:
    loop .run
    mov si, msg_slow
    call puts
    mov al, 5
    jmp exit
.stuck:
    mov si, msg_jam
    call puts
    mov ax, [_a2_m+AM_PC]
    call puthex
    jmp short .verdict
.settled:
    mov si, msg_settle
    call puts
    mov ax, [_a2_m+AM_PC]
    call puthex
.verdict:
    ; ...and the machine it settled with, because "it stopped at $XXXX" names
    ; the trap and the registers name the reason
    mov si, msg_regs
    call puts
    mov ax, [_a2_m+AM_A]
    call puthex
    mov al, ' '
    call putc
    mov ax, [_a2_m+AM_X]
    call puthex
    mov al, ' '
    call putc
    mov ax, [_a2_m+AM_Y]
    call puthex
    mov al, ' '
    call putc
    mov ax, [_a2_m+AM_S]
    call puthex
    mov al, ' '
    call putc
    mov ax, [_a2_m+AM_P]
    call puthex
    mov si, msg_nl
    call puts
    mov ax, [_a2_m+AM_PC]
    cmp ax, DORMOK
    jne .bad
%ifdef NEG
    mov si, msg_nbad
    call puts
    mov al, 3
    jmp exit
%else
    mov si, msg_pass
    call puts
    mov al, 0
    jmp exit
%endif
.bad:
%ifdef NEG
    mov si, msg_nok
    call puts
    mov al, 0
    jmp exit
%else
    mov si, msg_fail
    call puts
    mov al, 3
    jmp exit
%endif

last_pc: dw 0xFFFF
same_n:  dw 0
%endif

; =============================================================================
; THE ROUTINE UNDER TEST - THE SHIPPING TEXT
; =============================================================================
%include "a2cpu.inc"

section .text

putc:
    push ax
    push dx
.wait:
    mov dx, 0x3FD
    in al, dx
    test al, 0x20
    jz .wait
    pop dx
    pop ax
    push dx
    mov dx, 0x3F8
    out dx, al
    pop dx
    ret

puts:
    push ax
.l: mov al, [cs:si]
    inc si
    or al, al
    jz .e
    call putc
    jmp .l
.e: pop ax
    ret

puthex:
    push cx
    mov cx, 4
.h: rol ax, 1
    rol ax, 1
    rol ax, 1
    rol ax, 1
    push ax
    and al, 15
    cmp al, 10
    jb .d
    add al, 7
.d: add al, '0'
    call putc
    pop ax
    loop .h
    pop cx
    ret

exit:                               ; AL -> isa-debug-exit: code = AL*2+1
    mov dx, 0xF4
    out dx, al
    cli
    hlt
    jmp exit

section .data
fails:      dw 0
neg_io:     db 0
esfix_save: dw 0, 0
io_reads:   dw 0
io_writes:  dw 0
io_last_a:  dw 0
io_last_v:  db 0
dec_sum:    dw 0
dec_am:     dw 0                    ; low = the operand, high = the accumulator
decsw:      db 0                    ; which of the four sweeps
%ifndef DORMANN
; the four checksums tools/c64dec.py computes from the documented NMOS rules
dec_want:   dw DECADC0, DECADC1, DECSBC0, DECSBC1
%endif

msg_hello:  db 13, 10, 'a2cputest: the 6502 core on a real x86, SS != DS', 13, 10, 0
msg_r2:     db 'row 2  decimal ADC/SBC, all 262144    ', 0
msg_r3:     db 'row 3  reads across every boundary    ', 0
msg_r4:     db 'row 4  writes, and the drop above $C0FF', 0
msg_r5:     db 'row 5  fetches across every boundary  ', 0
msg_r6:     db 'row 6  ES and DS come back            ', 0
msg_r7:     db 'row 7  the scratch is out of reach    ', 0
msg_r8:     db 'row 8  soft-switch READS call out too ', 0
msg_r9:     db 'row 9  the illegal opcodes            ', 0
msg_r10:    db 'row 10 cycle totals and the penalties ', 0
msg_r11:    db 'row 11 the four unstable stores       ', 0
msg_r12:    db 'row 12 the interrupt stack and RTI    ', 0
msg_ok:     db 'ok ', 0
msg_bad:    db 'FAIL ', 0
msg_nok:    db '(control fails: ok)', 13, 10, 0
msg_nbad:   db 'CONTROL PASSED - the check proves nothing', 13, 10, 0
msg_pass:   db 13, 10, 'a2cputest: PASS', 13, 10, 0
msg_fail:   db 13, 10, 'a2cputest: FAIL', 13, 10, 0
msg_dorm:   db 'Dormann 6502 functional test, 64KB at $0400', 13, 10, 0
msg_neg:    db 'NEGATIVE CONTROL: ADC # is dispatched to ORA #', 13, 10, 0
msg_settle: db 'settled at $', 0
msg_regs:   db '  A X Y S P = ', 0
msg_jam:    db 'JAM at $', 0
msg_slow:   db 'never settled (the budget ran out)', 13, 10, 0
msg_nl:     db 13, 10, 0
