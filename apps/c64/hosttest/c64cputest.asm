; =============================================================================
; os8088 - apps/c64/hosttest/c64cputest.asm       THE 6510 CORE'S GATE
;
; Part of C64, a reimplementation of VICE 3.10's `x64` (docs/C64-SPEC.md).
; VICE is Copyright (C) 1996-2025 the VICE team, GPL-2-or-later; apps/c64/ is
; GPL-2-or-later and apps/c64/COPYING is the licence text.
;
; A BOOT SECTOR THAT RUNS THE SHIPPING 6510 CORE ON A REAL x86, with nothing
; else running - `make c64cputest`, minutes, the rcz80test precedent. The core
; is %included, not copied, so what runs is the shipping text, under the one
; condition that makes this OS different from every other target it could be
; tested on: SS != DS. Here CS = DS = 0 stand in for the package's segment
; (c64cpu.inc reaches its statics through CS and c64mem.inc through DS, and in
; a package the two are equal), SS = 0x1000 is the task stack, ES = 0x3000
; stands in for KERNEL_SEG and is checked after every call out, the C64's 64KB
; is the segment 0x4000 and its 20KB of ROM 0x6000.
;
; IT IS NOT A DORMANN WRAPPER. C64-SPEC §4.6's rows are eleven routines
; below, and EVERY ONE OF THEM HAS A NEGATIVE CONTROL: the row is run
; a second time with something deliberately broken - a stale fetch bias, a
; wrong bank byte, an I/O stub that answers a constant, a clobbered ES, a
; missing branch penalty, an illegal opcode dispatched to NOP - and the
; harness FAILS if that run passes. A check that cannot fail is not a check
; (LESSONS.md 2).
;
; The perturbations reach the ENVIRONMENT or the core's own tables at runtime,
; never the source: what is assembled here is byte for byte what ships.
;
;     -DROWS      the eleven rows below (seconds)
;     -DDORMANN   Klaus Dormann's 6502 functional test, appended by the
;                 script at sector DORMSEC and read straight into the C64's
;                 RAM; PC starts at $0400 and the run is judged by where the
;                 program counter settles (minutes)
;     -DNEG       ...and the same with the ADC handler replaced by a wrong
;                 one, which must NOT reach the success trap
;
; RUN IT:  apps/c64/hosttest/c64cputest.sh   (or `make c64cputest`)
; =============================================================================

cpu 8086
bits 16
org 0x7C00

SEG_STACK   equ 0x1000
SEG_KERNEL  equ 0x3000              ; "KERNEL_SEG": must come back intact
SEG_RAM     equ 0x4000              ; the C64's 64KB claim
SEG_ROM     equ 0x6000              ; ...and C64.ROM's 20KB

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

    mov word [_c64_m+CM_RAMSEG], SEG_RAM
    mov word [_c64_m+CM_ROMSEG], SEG_ROM
    call mem_init

%ifdef DORMANN
    jmp dormann_main
%else
    jmp rows_main
%endif

; =============================================================================
; THE MACHINE THE ROWS RUN IN
; =============================================================================
; mem_init - a recognisable byte everywhere, so "which bank answered" is a
; question the test can ASK rather than infer:
;   RAM   $A000 = 0x1A   $D000 = 0x1D   $E000 = 0x1E   (and 0x11 elsewhere)
;   ROM   BASIC = 0xBB   CHARGEN = 0xCC   KERNAL = 0xEE
mem_init:
    push es
    push di
    push cx
    mov ax, SEG_RAM
    mov es, ax
    xor di, di
    mov cx, 0x8000
    mov al, 0x11
    rep stosb                       ; $0000-$7FFF
    mov cx, 0x8000
    rep stosb                       ; $8000-$FFFF
    mov byte [es:0xA000], 0x1A
    mov byte [es:0xBFFF], 0x1B
    mov byte [es:0xD000], 0x1D
    mov byte [es:0xE000], 0x1E
    mov byte [es:0xFFFF], 0x1F
    ; the ROM claim: KERNAL at 0, BASIC at $2000, CHARGEN at $4000 (1.4)
    mov ax, SEG_ROM
    mov es, ax
    xor di, di
    mov cx, 0x2000
    mov al, 0xEE
    rep stosb
    mov cx, 0x2000
    mov al, 0xBB
    rep stosb
    mov cx, 0x1000
    mov al, 0xCC
    rep stosb
    pop cx
    pop di
    pop es
    ret

; scr_wr / scr_rd - the core's scratch, from the harness's side
; AL = value, BX = offset
scr_wr:
    push es
    push bx
    mov es, [_c64_m+CM_RAMSEG]
    add bx, C64_SCR_BASE
    mov [es:bx], al
    pop bx
    pop es
    ret
scr_rd:
    push es
    push bx
    mov es, [_c64_m+CM_RAMSEG]
    add bx, C64_SCR_BASE
    mov al, [es:bx]
    pop bx
    pop es
    ret

; ram_wr / ram_rd - BX = address, AL = value
ram_wr:
    push es
    mov es, [_c64_m+CM_RAMSEG]
    mov [es:bx], al
    pop es
    ret
ram_rd:
    push es
    mov es, [_c64_m+CM_RAMSEG]
    mov al, [es:bx]
    pop es
    ret

; bank_set - AL = the $01 value; publish the three map bytes the core reads
; (3.3's table, and the harness's own copy of it, so the CORE's reader is what
; is under test and not a shared subroutine).
bank_set:
    push ax
    push bx
    push si
    and al, 7
    mov [pp_data], al
    cmp byte [neg_map], 0           ; the negative control for row 2
    je .ok
    xor al, 4                       ; ...a deliberately wrong row
    and al, 7
.ok:
    mov bl, al
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov si, bx
    mov al, [cs:si+bank_tab]
    mov bx, C64_SCR_MAPA
    call scr_wr
    mov al, [cs:si+bank_tab+1]
    mov bx, C64_SCR_MAPD
    call scr_wr
    mov al, [cs:si+bank_tab+2]
    mov bx, C64_SCR_MAPE
    call scr_wr
    call rebank
    pop si
    pop bx
    pop ax
    ret

rebank:
    push ax
    mov bx, C64_SCR_BOUND
    xor al, al
    call scr_wr
    inc bx
    call scr_wr
    mov bx, C64_SCR_BLO
    call scr_wr
    inc bx
    call scr_wr
    pop ax
    ret

; the seven maps, four bytes an entry so the index is a shift (3.3)
bank_tab:
    db 0, 0, 0, 0                   ; 0  RAM RAM RAM
    db 0, 3, 0, 0                   ; 1  RAM CHARGEN RAM
    db 0, 3, 2, 0                   ; 2  RAM CHARGEN KERNAL
    db 1, 3, 2, 0                   ; 3  BASIC CHARGEN KERNAL
    db 0, 0, 0, 0                   ; 4  RAM RAM RAM
    db 0, 4, 0, 0                   ; 5  RAM IO RAM
    db 0, 4, 2, 0                   ; 6  RAM IO KERNAL
    db 1, 4, 2, 0                   ; 7  BASIC IO KERNAL

; what each map answers at $A000 / $D000 / $E000, in the same order
want_tab:
    db 0x1A, 0x1D, 0x1E, 0
    db 0x1A, 0xCC, 0x1E, 0
    db 0x1A, 0xCC, 0xEE, 0
    db 0xBB, 0xCC, 0xEE, 0
    db 0x1A, 0x1D, 0x1E, 0
    db 0x1A, 0x5A, 0x1E, 0          ; $D000 in an I/O bank is the STUB's answer
    db 0x1A, 0x5A, 0xEE, 0
    db 0xBB, 0x5A, 0xEE, 0

; =============================================================================
; THE I/O STUBS - REAL RETURNS, NOT PARK-AT-FAIL (C64-SPEC §4.6 row 5)
;
; They are the compiled C in the package. Here they answer values the test
; then CHECKS, so the cdecl convention, the DS swap and the ES save and reload
; are exercised rather than merely forbidden - and they log every access, so
; "did the core take the slow path at all?" is a question with an answer.
;
; The read answers `(a & 0xFF) ^ 0x5A`, which is a different byte for every
; register and cannot be confused with RAM, ROM or zero.
; =============================================================================
_c64_io_rd:
    push bp
    mov bp, sp
    push bx
    mov bx, [bp+4]
    mov [io_last_a], bx
    inc word [io_reads]
    cmp bx, 2
    jae .plain
    ; $0000/$0001 are the processor port (3.2): the DDR, and the data port's
    ; read-back with bit 4 (cassette sense) high and bits 6-7 clear
    or bx, bx
    jnz .data
    mov al, [pp_ddr]
    jmp short .out
.data:
    ; the SHIPPING formula (c64io.c's c64_pp_read): an input bit reads the
    ; pull-up where the board has one and ZERO where it has none, which is
    ; `(data & ddr) | (~ddr & $17)` - bits 3 and 5 have no pull-up
    ; (c64pla.c:55, :61, and the C64's pullup byte at c64mem.c:222)
    mov al, [pp_data]
    and al, [pp_ddr]
    mov ah, [pp_ddr]
    not ah
    and ah, 0x17
    or al, ah
    jmp short .out
.plain:
    mov al, bl
    xor al, 0x5A                    ; a byte no bank and no zero can be
    cmp byte [neg_io], 0            ; the negative control for row 5:
    je .out
    mov al, 0                       ; ...a stub that answers a constant
.out:
    xor ah, ah
    ; ES IS CALLER-CLOBBERED BY THE C ABI, so this stub clobbers it - always,
    ; not only under a control. The core is what has to put it back (3.4), and
    ; row 6's control is the one that stops it doing so.
    push ax
    mov ax, 0xBAD0
    mov es, ax
    pop ax
    pop bx
    pop bp
    ret

_c64_io_wr:
    push bp
    mov bp, sp
    push bx
    mov bx, [bp+4]
    mov [io_last_a], bx
    mov al, [bp+6]
    mov [io_last_v], al
    inc word [io_writes]
    cmp bx, 2
    jae .out
    or bx, bx
    jnz .data
    mov [pp_ddr], al
    jmp short .bank
.data:
    mov [pp_data], al
.bank:
    ; THE NEGATIVE CONTROL FOR ROW 3: the write is taken and the map is NOT
    ; re-evaluated, which is exactly the defect 3.2 exists to forbid - the
    ; next fetch runs under the old bank.
    cmp byte [neg_port], 0
    jne .out
    ; a write to $00 or $01 RE-EVALUATES THE MAP AT ONCE (3.2), and the core
    ; must not fetch one instruction under the old one
    mov al, [pp_data]
    and al, [pp_ddr]
    mov ah, [pp_ddr]
    not ah
    and ah, 7
    or al, ah
    call bank_set
.out:
    pop bx
    pop bp
    ret

; =============================================================================
; THE ROWS
; =============================================================================
%ifndef DORMANN
rows_main:
    mov word [fails], 0

    mov si, msg_r2
    call puts
    call row_maps
    call verdict
    mov byte [neg_map], 1
    call row_maps
    call control
    mov byte [neg_map], 0

    mov si, msg_r3
    call puts
    call row_port
    call verdict
    mov byte [neg_port], 1
    call row_port
    call control
    mov byte [neg_port], 0

    mov si, msg_r4
    call puts
    call row_bound
    call verdict
    mov byte [neg_stale], 1
    call row_bound
    call control
    mov byte [neg_stale], 0

    mov si, msg_r5
    call puts
    call row_io
    call verdict
    mov byte [neg_io], 1
    call row_io
    call control
    mov byte [neg_io], 0

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
    call row_int
    call verdict
    mov byte [neg_int], 1
    call row_int
    call control
    mov byte [neg_int], 0

    mov si, msg_r8
    call puts
    call row_ill
    call verdict
    call patch_lax                  ; LAX dispatched to NOP
    call row_ill
    call control
    call unpatch_lax
    call patch_arr                  ; ...and ARR dispatched to AND, which is
    call row_ill                    ; the family the old row could not see
    call control
    call unpatch_arr

    mov si, msg_r9
    call puts
    call row_cyc
    call verdict
    call patch_cyc                  ; one opcode's cost made wrong
    call row_cyc
    call control
    call unpatch_cyc

    mov si, msg_r10
    call puts
    call row_dec
    call verdict
    call patch_dec                  ; the decimal path's carry made wrong
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
; runprog - assemble a little 6510 program at $0800 and run it.
;   SI = a cs: pointer to {length, bytes...}, CX = the cycle budget
; The core's answer is left in AX and c64_m is the machine afterwards.
; -----------------------------------------------------------------------------
runprog:
    mov bx, 0x0800
; runprog_at - the same, loaded at BX and started there, because a row that
; asks about a BRANCH ACROSS A PAGE cannot ask it at $0800: every branch in a
; program that begins there lands in the same page, which is why the old
; `p_cyc_c` at $0800 was a page-cross row that never crossed.
runprog_at:
    push es
    push di
    push bx
    mov es, [_c64_m+CM_RAMSEG]
    ; THE ANSWER BYTES ARE CLEARED FIRST. Without this a row's negative
    ; control read the value the POSITIVE run had left at the same address
    ; and passed - which is a control proving nothing, and it is exactly the
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
    mov [_c64_m+CM_PC], bx
    mov word [_c64_m+CM_S], 0x00FD
    mov word [_c64_m+CM_P], 0x0024
    mov word [_c64_m+CM_A], 0
    mov word [_c64_m+CM_X], 0
    mov word [_c64_m+CM_Y], 0
    call rebank
    PUSHI 200
    call _c64_run
    add sp, 2
    ret

; -----------------------------------------------------------------------------
; ROW 2 - THE SEVEN BANK MAPS (C64-SPEC §3.3)
; The core reads $A000, $D000 and $E000 in each of the eight $01 values and
; stores what it saw; the answers are want_tab's.
; -----------------------------------------------------------------------------
row_maps:
    push si
    ; THE LOOP COUNTER IS IN MEMORY, because _c64_run is cdecl and AX BX CX DX
    ; SI DI are all the caller's to lose (SPEC.md 73.3). A counter in DI here
    ; was destroyed by the first call and the row ran for ever.
    mov byte [rowi], 0
.next:
    mov al, [rowi]
    xor ah, ah
    call bank_set
    mov si, p_readbanks
    call runprog
    mov bx, 0x0900
    call ram_rd
    mov [saw0], al
    mov bx, 0x0901
    call ram_rd
    mov [saw1], al
    mov bx, 0x0902
    call ram_rd
    mov [saw2], al
    mov al, [rowi]
    xor ah, ah
    mov si, ax
    shl si, 1
    shl si, 1
    mov al, [cs:si+want_tab]
    cmp al, [saw0]
    jne .bad
    mov al, [cs:si+want_tab+1]
    cmp al, [saw1]
    jne .bad
    mov al, [cs:si+want_tab+2]
    cmp al, [saw2]
    jne .bad
    inc byte [rowi]
    cmp byte [rowi], 8
    jb .next
    xor ax, ax
    jmp short .out
.bad:
    mov ax, 1
.out:
    pop si
    ret

; LDA $A000 / STA $0900 / LDA $D000 / STA $0901 / LDA $E000 / STA $0902 / KIL
p_readbanks:
    db 19
    db 0xAD, 0x00, 0xA0
    db 0x8D, 0x00, 0x09
    db 0xAD, 0x00, 0xD0
    db 0x8D, 0x01, 0x09
    db 0xAD, 0x00, 0xE0
    db 0x8D, 0x02, 0x09
    db 0x02

; -----------------------------------------------------------------------------
; ROW 3 - $0000 AND $0001 ARE NOT RAM (C64-SPEC §3.2)
; The core writes $01, reads it back, and the RE-BANK MUST HAVE TAKEN EFFECT ON
; THE VERY NEXT FETCH: the program switches to an all-RAM map and immediately
; reads $E000, which must answer RAM and not the KERNAL.
; -----------------------------------------------------------------------------
row_port:
    push si
    mov al, 0x2F
    mov [pp_ddr], al
    mov al, 7
    call bank_set
    mov byte [pp_data], 0x37        ; the RESET value of the data port, which
                                    ; bank_set's own bookkeeping masks to its
                                    ; three bank bits - $01 reads back the
                                    ; whole byte through the DDR (3.2)
    mov si, p_port
    call runprog
    mov bx, 0x0910
    call ram_rd                     ; the read-back of $01
    cmp al, 0x37                    ; ($37 & $2F) | (~$2F & $17): bits 3 and 5
                                    ; are OUTPUTS here, so they read what was
                                    ; written and the pull-up mask does not
                                    ; reach them (3.2)
    jne .bad
    mov bx, 0x0911
    call ram_rd                     ; what $E000 answered AFTER `STA $01`
    cmp al, 0x1E                    ; RAM, because $01 = $30 banks it out
    jne .bad
    xor ax, ax
    jmp short .out
.bad:
    mov ax, 1
.out:
    pop si
    ret

; LDA $01 / STA $0910 / LDA #$30 / STA $01 / LDA $E000 / STA $0911 / KIL
p_port:
    db 16
    db 0xA5, 0x01
    db 0x8D, 0x10, 0x09
    db 0xA9, 0x30
    db 0x85, 0x01
    db 0xAD, 0x00, 0xE0
    db 0x8D, 0x11, 0x09
    db 0x02

; -----------------------------------------------------------------------------
; ROW 4 - FETCHES AND ACCESSES AT EVERY MAPPING BOUNDARY (C64-SPEC §4.3)
;
; The instruction is PLACED ACROSS the boundary: three bytes at $9FFE, so the
; opcode is fetched below $A000 and the operand's high byte above it. In the
; BASIC map that is a fetch whose second half comes from a bank the first half
; could not see, and the whole point of the boundary word is that the core
; notices without a control transfer in sight.
; -----------------------------------------------------------------------------
row_bound:
    push si
    push di
    ; $9FFE: LDA #$A5 (2 bytes), $A000: STA $0920 (3 bytes) - so the LDA is
    ; wholly below the boundary and the STA wholly above it, and PC crosses
    ; with no branch
    mov bx, 0x9FFE
    mov al, 0xA9
    call ram_wr
    inc bx
    mov al, 0xA5
    call ram_wr
    ; the BASIC image at $A000 has to be written to the ROM claim, because
    ; that is the bank the core will fetch from
    push es
    mov ax, SEG_ROM
    mov es, ax
    mov word [es:0x2000], 0x208D    ; STA $0920
    mov byte [es:0x2002], 0x09
    mov byte [es:0x2003], 0x02      ; KIL
    pop es
    mov al, 7                       ; BASIC in, so $A000 is ROM
    call bank_set
    mov bx, 0x0920                  ; the answer byte is CLEARED FIRST, or the
    xor al, al                      ; control reads what the positive run left
    call ram_wr
    mov word [_c64_m+CM_PC], 0x9FFE
    mov word [_c64_m+CM_S], 0x00FD
    mov word [_c64_m+CM_P], 0x0024
    mov word [_c64_m+CM_A], 0
    call rebank
    cmp byte [neg_stale], 0         ; THE NEGATIVE CONTROL FOR ROW 4: the bank
    je .go                          ; above the boundary is made RAM, where
    mov bx, C64_SCR_MAPA            ; mem_init left $1A $11 $11 - so if the
    mov al, 0                       ; core takes its operand from the wrong
    call scr_wr                     ; side of $A000 it executes junk and
    call rebank                     ; $0920 is never written
.go:
    PUSHI 200
    call _c64_run
    add sp, 2
    mov bx, 0x0920
    call ram_rd
    cmp al, 0xA5
    jne .bad

    ; --- (b) AN INSTRUCTION THAT REALLY STRADDLES $9FFF/$A000. The case above
    ; puts two WHOLE instructions either side of the boundary, which is the
    ; fetch question but not the OPERAND one: here the opcode is the last byte
    ; below the boundary and its immediate is the first byte above it, so a
    ; core that re-biases only between instructions reads the wrong side.
    mov bx, 0x9FFF
    mov al, 0xA9                    ; LDA #
    call ram_wr
    push es
    mov ax, SEG_ROM
    mov es, ax
    mov byte [es:0x2000], 0xA5      ; $A000 - the IMMEDIATE, in BASIC
    mov byte [es:0x2001], 0x8D      ; STA $0921
    mov byte [es:0x2002], 0x21
    mov byte [es:0x2003], 0x09
    mov byte [es:0x2004], 0x02
    mov byte [es:0x3FFF], 0xA9      ; $BFFF - (c)'s opcode, in BASIC
    pop es
    mov al, 7
    call bank_set
    mov bx, 0x0921
    xor al, al
    call ram_wr
    mov bx, 0x9FFF
    call bnd_run
    mov bx, 0x0921
    call ram_rd
    cmp al, 0xA5
    jne .bad

    ; --- (c) $BFFF/$C000: the opcode is the LAST byte of the BASIC ROM and
    ; the immediate the FIRST byte of the RAM above it
    push es
    mov es, [_c64_m+CM_RAMSEG]
    mov byte [es:0xC000], 0x5C      ; the immediate
    mov byte [es:0xC001], 0x8D      ; STA $0922
    mov byte [es:0xC002], 0x22
    mov byte [es:0xC003], 0x09
    mov byte [es:0xC004], 0x02
    mov byte [es:0x0922], 0
    pop es
    mov bx, 0xBFFF
    call bnd_run
    mov bx, 0x0922
    call ram_rd
    cmp al, 0x5C
    jne .bad

    ; --- (d) $CFFF/$D000, WHICH IS ALSO THE SLOW I/O FETCH. `JMP` has its
    ; opcode at $CFFE, its low byte at $CFFF and its HIGH byte at $D000 - a
    ; register file, which no biased ES can read, so that byte has to come
    ; back through the same call out the data path uses (3.4). The stub
    ; answers `a ^ $5A`, so $D000 reads $5A and the jump goes to $5A00.
    push es
    mov es, [_c64_m+CM_RAMSEG]
    mov byte [es:0xCFFE], 0x4C      ; JMP
    mov byte [es:0xCFFF], 0x00      ; ...low byte
    mov byte [es:0x5A00], 0xA9      ; LDA #$5A
    mov byte [es:0x5A01], 0x5A
    mov byte [es:0x5A02], 0x8D      ; STA $0923
    mov byte [es:0x5A03], 0x23
    mov byte [es:0x5A04], 0x09
    mov byte [es:0x5A05], 0x02
    mov byte [es:0x0923], 0
    pop es
    mov bx, 0xCFFE
    call bnd_run
    mov bx, 0x0923
    call ram_rd
    cmp al, 0x5A
    jne .bad

    ; --- (e) $DFFF/$E000: the opcode is the last byte of the CHARGEN ROM and
    ; the immediate the first of the KERNAL, one bank apart
    mov al, 3                       ; BASIC / CHARGEN / KERNAL
    call bank_set
    push es
    mov ax, SEG_ROM
    mov es, ax
    mov byte [es:0x4FFF], 0xA9      ; $DFFF, in CHARGEN
    mov byte [es:0x0000], 0x5B      ; $E000, in the KERNAL - the immediate
    mov byte [es:0x0001], 0x8D      ; STA $0924
    mov byte [es:0x0002], 0x24
    mov byte [es:0x0003], 0x09
    mov byte [es:0x0004], 0x4C      ; ...and (f)'s backward JMP $0400
    mov byte [es:0x0005], 0x00
    mov byte [es:0x0006], 0x04
    pop es
    push es
    mov es, [_c64_m+CM_RAMSEG]
    mov byte [es:0x0924], 0
    mov byte [es:0x0925], 0
    mov byte [es:0x0400], 0xA9      ; (f)'s landing: LDA #$5D / STA $0925
    mov byte [es:0x0401], 0x5D
    mov byte [es:0x0402], 0x8D
    mov byte [es:0x0403], 0x25
    mov byte [es:0x0404], 0x09
    mov byte [es:0x0405], 0x02
    pop es
    mov bx, 0xDFFF
    call bnd_run
    mov bx, 0x0924
    call ram_rd
    cmp al, 0x5B
    jne .bad
    ; --- (f) ...AND THE SAME RUN WENT BACKWARDS OUT OF THE KERNAL INTO RAM.
    ; A JMP from $E004 to $0400 leaves PC BELOW the biased region, which a
    ; one-compare ceiling guard cannot see: it would have gone on fetching
    ; RAM through the KERNAL's bias. This is C64-SPEC §4.3's LOW edge.
    mov bx, 0x0925
    call ram_rd
    cmp al, 0x5D
    jne .bad

    ; --- (g) A $01 REMAP IN THE MIDDLE OF THE INSTRUCTION STREAM. The
    ; program runs from $A000 in the BASIC ROM, banks BASIC OUT, and the very
    ; next instruction is fetched from the RAM that was under it. The two
    ; images differ, so a stale bias writes $99 where the RAM writes $66.
    mov al, 7
    call bank_set
    push es
    mov ax, SEG_ROM
    mov es, ax
    mov byte [es:0x2000], 0xA9      ; $A000 LDA #$30
    mov byte [es:0x2001], 0x30
    mov byte [es:0x2002], 0x85      ; STA $01 - BASIC out, RAM in
    mov byte [es:0x2003], 0x01
    mov byte [es:0x2004], 0xA9      ; ...what the ROM would say next
    mov byte [es:0x2005], 0x99
    mov byte [es:0x2006], 0x8D
    mov byte [es:0x2007], 0x26
    mov byte [es:0x2008], 0x09
    mov byte [es:0x2009], 0x02
    pop es
    push es
    mov es, [_c64_m+CM_RAMSEG]
    mov byte [es:0xA004], 0xA9      ; ...and what the RAM under it says
    mov byte [es:0xA005], 0x66
    mov byte [es:0xA006], 0x8D
    mov byte [es:0xA007], 0x26
    mov byte [es:0xA008], 0x09
    mov byte [es:0xA009], 0x02
    mov byte [es:0x0926], 0
    pop es
    mov bx, 0xA000
    call bnd_run
    mov bx, 0x0926
    call ram_rd
    cmp al, 0x66
    jne .bad

    xor ax, ax
    jmp short .out
.bad:
    mov ax, 1
.out:
    pop di
    pop si
    ret

; bnd_run - start the core at BX with a fresh machine and 200 cycles
bnd_run:
    mov [_c64_m+CM_PC], bx
    mov word [_c64_m+CM_S], 0x00FD
    mov word [_c64_m+CM_P], 0x0024
    mov word [_c64_m+CM_A], 0
    mov word [_c64_m+CM_X], 0
    mov word [_c64_m+CM_Y], 0
    call rebank
    PUSHI 200
    call _c64_run
    add sp, 2
    ret

scr_wr_word:                        ; AX = value, BX = scratch offset
    push ax
    call scr_wr
    inc bx
    pop ax
    mov al, ah
    call scr_wr
    ret

; -----------------------------------------------------------------------------
; ROW 5 - REAL I/O STUB RETURNS (C64-SPEC §4.6 row 5)
; $D000-$DFFF in an I/O bank goes out to the C, and what comes back is what
; the stub said - not RAM, not ROM and not zero. The write side is checked by
; what the stub LOGGED.
; -----------------------------------------------------------------------------
row_io:
    push si
    mov al, 7                       ; I/O mapped
    call bank_set
    mov word [io_reads], 0
    mov word [io_writes], 0
    mov si, p_io
    call runprog
    mov bx, 0x0930
    call ram_rd
    cmp al, 0x12 ^ 0x5A             ; $D012's stub answer
    jne .bad
    cmp word [io_writes], 0
    je .bad
    cmp word [io_last_a], 0xD020
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

; LDA $D012 / STA $0930 / LDA #$0E / STA $D020 / KIL
p_io:
    db 12
    db 0xAD, 0x12, 0xD0
    db 0x8D, 0x30, 0x09
    db 0xA9, 0x0E
    db 0x8D, 0x20, 0xD0
    db 0x02

; -----------------------------------------------------------------------------
; ROW 6 - ES AND DS COME BACK (C64-SPEC §3.4)
; ES is caller-clobbered by the C ABI, so the core reloads it from the cached
; fetch segment rather than trusting it. The proof is that the instruction
; AFTER a call out still fetches: the program reads I/O and then executes six
; more bytes out of the same bank.
; -----------------------------------------------------------------------------
row_seg:
    push si
    mov al, 7
    call bank_set
    mov si, p_seg
    call runprog
    mov bx, 0x0940
    call ram_rd
    cmp al, 0x99
    jne .bad
    ; ...and the harness's own ES sentinel is intact
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

; LDA $D011 / LDA #$99 / STA $0940 / KIL   - the LDA #$99 is fetched AFTER the
; call out, which is the whole question
p_seg:
    db 9
    db 0xAD, 0x11, 0xD0
    db 0xA9, 0x99
    db 0x8D, 0x40, 0x09
    db 0x02

; -----------------------------------------------------------------------------
; ROW 7 - IRQ AND NMI ENTRY (C64-SPEC §4.4)
; The vectors are put in RAM under an all-RAM map, the pending flags raised,
; and the core must enter through them with B CLEAR and I SET - and take the
; NMI first when both are up.
; -----------------------------------------------------------------------------
row_int:
    push si
    mov al, 0                       ; all RAM: $FFFA-$FFFF are ours
    call bank_set
    mov bx, 0xFFFA
    mov al, 0x00
    call ram_wr
    inc bx
    mov al, 0x0A                    ; NMI -> $0A00
    call ram_wr
    mov bx, 0xFFFE
    mov al, 0x00
    call ram_wr
    inc bx
    mov al, 0x0B                    ; IRQ -> $0B00
    call ram_wr
    ; $0A00: LDA #$4E / STA $0950 / KIL     $0B00: LDA #$49 / STA $0950 / KIL
    push es
    mov es, [_c64_m+CM_RAMSEG]
    mov word [es:0x0A00], 0x4EA9
    mov word [es:0x0A02], 0x508D
    mov byte [es:0x0A04], 0x09
    mov byte [es:0x0A05], 0x02
    mov word [es:0x0B00], 0x49A9
    mov word [es:0x0B02], 0x508D
    mov byte [es:0x0B04], 0x09
    mov byte [es:0x0B05], 0x02
    mov byte [es:0x0950], 0
    pop es
    ; --- the IRQ, with I clear
    mov word [_c64_m+CM_PC], 0x0800
    mov word [_c64_m+CM_S], 0x00FD
    mov word [_c64_m+CM_P], 0x0020  ; I CLEAR
    call rebank
    mov bx, C64_SCR_IRQ
    mov al, 1
    cmp byte [neg_int], 0           ; the negative control for row 7
    je .arm
    mov al, 0                       ; ...nothing pending at all
.arm:
    call scr_wr
    PUSHI 200
    call _c64_run
    add sp, 2
    mov bx, 0x0950
    call ram_rd
    cmp al, 0x49
    jne .bad
    ; ...and I is SET on entry, B is CLEAR on the stack
    mov ax, [_c64_m+CM_P]
    test al, 0x04
    jz .bad
    ; --- the NMI wins when both are up
    push es
    mov es, [_c64_m+CM_RAMSEG]
    mov byte [es:0x0950], 0
    pop es
    mov word [_c64_m+CM_PC], 0x0800
    mov word [_c64_m+CM_S], 0x00FD
    mov word [_c64_m+CM_P], 0x0024  ; I SET: the IRQ is masked, the NMI is not
    call rebank
    mov bx, C64_SCR_IRQ
    mov al, 3
    cmp byte [neg_int], 0
    je .arm2
    mov al, 0
.arm2:
    call scr_wr
    PUSHI 200
    call _c64_run
    add sp, 2
    mov bx, 0x0950
    call ram_rd
    cmp al, 0x4E
    jne .bad
    ; ...and the EDGE was consumed
    mov bx, C64_SCR_IRQ
    call scr_rd
    test al, 2
    jnz .bad
    xor ax, ax
    jmp short .out
.bad:
    mov ax, 1
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; ROW 8 - THE ILLEGAL OPCODES (6510core.c)
; LAX, SAX, DCP, ISB, SLO, ANC and SBX, each with an answer only that opcode
; produces.
; -----------------------------------------------------------------------------
row_ill:
    push si
    mov al, 0
    call bank_set
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
    ; --- AND THE FAMILIES THE ROW USED TO CLAIM AND NOT EXECUTE. The program
    ; had LAX, SAX and DCP in it and the comment said seven families, so the
    ; broken ARR (no decimal mode at all) and the wrong ANE magic constant
    ; both passed this row for a whole wave.
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

; CLC / LDA #$FF / ARR #$F0 / STA $0964 / PHP / PLA / STA $0965
; SED / SEC / LDA #$FF / ARR #$FF / STA $0966 / PHP / PLA / STA $0967 / CLD
; LDA #$00 / LDX #$FF / ANE #$FF / STA $0968
; LDA #$FF / ALR #$03 / STA $0969
; LDA #$FF / ANC #$80 / STA $096A / PHP / PLA / STA $096B
; LDA #$FF / LDX #$F0 / SBX #$10 / STX $096C / KIL
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

; -----------------------------------------------------------------------------
; ROW 11 - THE FOUR UNSTABLE STORES (6510core.c:1769-1822)
;
; SHA, SHX, SHY and SHS mask the register with the UNINDEXED base's high byte
; plus one, and on a page cross the target's own high byte becomes the value
; (STORE_ABS_SH_Y, :699-711). This core used the INDEXED high byte - one more
; than VICE's whenever the index carried - and did not corrupt the address at
; all, which it called a stated deviation. Both halves are checkable:
;
;   SHA $1FF0,Y  Y=$10  A=X=$FF   value = $FF & ($1F+1) = $20  at $2000
;                                 (the old code stored $21)
;   SHX $1EF0,Y  Y=$10  X=$FF     value = $FF & $1F = $1F      at $1F00
;   SHY $1DF0,X  X=$10  Y=$FF     value = $FF & $1E = $1E      at $1E00
;   SHS $1CF0,Y  Y=$10  A=X=$FF   value = $1D at $1D00, and S = A & X = $FF
;   SHA $1FF8,Y  Y=$10  A=$FF X=$0F
;                value = $0F & $20 = 0, and the cross puts the VALUE in the
;                high byte: the store lands at $0008 and NOT at $2008
; -----------------------------------------------------------------------------
row_sh:
    push si
    mov al, 0
    call bank_set
    push es
    mov es, [_c64_m+CM_RAMSEG]
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
    mov ax, [_c64_m+CM_S]
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

; -----------------------------------------------------------------------------
; ROW 12 - WHAT AN INTERRUPT PUTS ON THE STACK, AND WHAT RTI TAKES OFF
;
; Row 7 asks whether the handler ran and whether I is set afterwards, and both
; of those pass with the pushed bytes in any order and the B flag either way.
; VICE pushes PC high, PC low, then the status byte with B CLEAR for a
; hardware interrupt (6510core.c:456's LOCAL_SET_BREAK(0) then the three
; PUSHes) and B SET for BRK (:1000). So this row reads $01FD, $01FC and $01FB
; by name, checks S landed at $FA, and then makes BRK come back through RTI.
; -----------------------------------------------------------------------------
row_intstk:
    push si
    mov al, 0
    call bank_set
    mov bx, 0xFFFA
    mov al, 0x00
    call ram_wr
    inc bx
    mov al, 0x0A
    call ram_wr
    mov bx, 0xFFFE
    mov al, 0x00
    call ram_wr
    inc bx
    mov al, 0x0B
    call ram_wr
    push es
    mov es, [_c64_m+CM_RAMSEG]
    mov byte [es:0x0A00], 0x02      ; both handlers are one KIL, so the stack
    mov byte [es:0x0B00], 0x02      ; is read exactly as the entry left it
    mov byte [es:0x01FD], 0
    mov byte [es:0x01FC], 0
    mov byte [es:0x01FB], 0
    pop es
    ; --- THE IRQ: PC high, PC low, then P with B CLEAR
    mov word [_c64_m+CM_PC], 0x0800
    mov word [_c64_m+CM_S], 0x00FD
    mov word [_c64_m+CM_P], 0x0020  ; I clear, bit 5 as always
    call rebank
    mov bx, C64_SCR_IRQ
    mov al, 1
    call scr_wr
    PUSHI 200
    call _c64_run
    add sp, 2
    mov bx, C64_SCR_IRQ
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
    mov ax, [_c64_m+CM_S]
    cmp al, 0xFA                    ; three bytes off a stack that began $FD
    jne .bad
    ; --- THE NMI pushes the same shape, with I set in the P it pushes
    push es
    mov es, [_c64_m+CM_RAMSEG]
    mov byte [es:0x01FB], 0
    pop es
    mov word [_c64_m+CM_PC], 0x0800
    mov word [_c64_m+CM_S], 0x00FD
    mov word [_c64_m+CM_P], 0x0024  ; I SET: the NMI is not masked by it
    call rebank
    mov bx, C64_SCR_IRQ
    mov al, 2
    call scr_wr
    PUSHI 200
    call _c64_run
    add sp, 2
    mov bx, 0x01FB
    call ram_rd
    cmp al, 0x24                    ; B CLEAR on an NMI too
    jne .bad
    ; --- BRK: B SET, the return address is PC + 2, and RTI takes it back
    push es
    mov es, [_c64_m+CM_RAMSEG]
    mov byte [es:0x0800], 0x00      ; BRK
    mov byte [es:0x0801], 0xEA      ; ...its padding byte
    mov byte [es:0x0802], 0x02      ; ...and where RTI must come back to
    mov byte [es:0x0B00], 0x40      ; the handler is RTI
    mov byte [es:0x01FB], 0
    pop es
    mov word [_c64_m+CM_PC], 0x0800
    mov word [_c64_m+CM_S], 0x00FD
    mov word [_c64_m+CM_P], 0x0020
    call rebank
    PUSHI 200
    call _c64_run
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
    mov ax, [_c64_m+CM_PC]
    cmp ax, 0x0802                  ; RTI came back to the padding's byte
    jne .bad
    mov ax, [_c64_m+CM_S]
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
    mov ax, [cs:c64_tab + 0xEA*2]   ; pushed and RTI has nothing to take back
    mov [cs:c64_tab + 0x00*2], ax
    ret
unpatch_brk:
    mov word [cs:c64_tab + 0x00*2], o_brk
    ret

patch_arr:                          ; ARR # ($6B) -> AND # , which has the
    mov ax, [cs:c64_tab + 0x29*2]   ; same operand and a plausible answer
    mov [cs:c64_tab + 0x6B*2], ax
    ret
unpatch_arr:
    mov word [cs:c64_tab + 0x6B*2], o_arr
    ret

patch_sha:                          ; SHA abs,Y ($9F) -> STA abs,Y, which
    mov ax, [cs:c64_tab + 0x99*2]   ; stores the WHOLE accumulator
    mov [cs:c64_tab + 0x9F*2], ax
    ret
unpatch_sha:
    mov word [cs:c64_tab + 0x9F*2], o_sha_absy
    ret

; the fixture byte and the program
; LDA #$3C / STA $0970 / LDA #0 / LDX #0
; LAX $0970          (A = X = $3C)
; STA $0960 / STX $0961
; AND #$18           (A = $18, X still $3C)
; SAX $0962          ($18 & $3C = $18)
; DCP $0970          ($0970 -> $3B)
; LDA $0970 / STA $0963 / KIL
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

; patch_esfix - four NOPs over `mov es, [FES]` at the end of c64_io_rd_bx, so
; the core comes back from a call out with the stub's garbage ES still in it.
; That is the ONLY honest way to make a stale fetch segment: the C ABI says ES
; is the callee's to lose, so a stub that clobbers it is behaving correctly and
; the reload is the thing under test (C64-SPEC §3.4).
patch_esfix:
    mov ax, [cs:c64_io_rd_esfix]
    mov [esfix_save], ax
    mov ax, [cs:c64_io_rd_esfix+2]
    mov [esfix_save+2], ax
    mov word [cs:c64_io_rd_esfix], 0x9090
    mov word [cs:c64_io_rd_esfix+2], 0x9090
    ret
unpatch_esfix:
    mov ax, [esfix_save]
    mov [cs:c64_io_rd_esfix], ax
    mov ax, [esfix_save+2]
    mov [cs:c64_io_rd_esfix+2], ax
    ret

patch_lax:                          ; LAX abs ($AF) -> NOP implied
    mov ax, [cs:c64_tab + 0xEA*2]
    mov [cs:c64_tab + 0xAF*2], ax
    ret
unpatch_lax:
    mov word [cs:c64_tab + 0xAF*2], o_lax_abs
    ret

; -----------------------------------------------------------------------------
; ROW 9 - CYCLE TOTALS PER OPCODE FAMILY (C64-SPEC §4.2)
;
; The core is given a budget and the leftover says exactly how much it spent.
; Three shapes, and the point of each is a PENALTY that a table alone does not
; carry: the page cross on an indexed read, the taken branch, and the branch
; that crosses a page.
; -----------------------------------------------------------------------------
; IT IS TABLE-DRIVEN, AND THAT IS THE FIX. The row used to be three shapes -
; LDA immediate, LDA abs,X either side of a page boundary, and one branch -
; so a cycle table could be wrong in almost any other family and this row
; passed. BRK's SEVEN cycles were being charged TWICE (the dispatch's table
; entry plus c64_vector's), which is a defect a machine boots straight
; through, and no row here could see it.
;
; Each entry is {program, expected cycles}, the expectations are VICE's own
; per-family costs (src/6510core.c's tables and its branch macro at :978,
; which charges the taken and the crossing cycles separately), and every
; program ends in KIL, which is 2.
row_cyc:
    push si
    push di
    mov al, 0
    call bank_set
    push es
    mov es, [_c64_m+CM_RAMSEG]
    mov byte [es:0x1100], 0x77
    pop es
    mov di, p_cyc_tab
.next:
    mov si, [cs:di]
    or si, si
    jz .special
    call runprog
    mov ax, [_c64_m+CM_CNT]
    neg ax
    add ax, 200                     ; cycles spent
    cmp ax, [cs:di+2]
    jne .bad
    add di, 4
    jmp short .next

.special:
    ; --- A TAKEN BRANCH THAT REALLY CROSSES A PAGE. Loaded at $08FB, so the
    ; BEQ's own bytes are at $08FD-$08FE, its next PC is $08FF and its target
    ; $0900: one page on. VICE charges the taken cycle and the crossing cycle
    ; separately (6510core.c:978), and at $0800 neither the old row nor any
    ; program in it could ever cross.
    mov si, p_cyc_cross
    mov bx, 0x08FB
    call runprog_at
    mov ax, [_c64_m+CM_CNT]
    neg ax
    add ax, 200
    cmp ax, 2 + 4 + 2               ; LDA #0, BEQ taken ACROSS a page, KIL
    jne .bad

    ; --- BRK IS SEVEN CYCLES, ONCE (6510core.c:998). It is an OPCODE, so the
    ; dispatch charges its table entry; entering the interrupt vector path
    ; charged seven MORE, and 14 is what this cost before the fix.
    mov bx, 0xFFFE
    mov al, 0x00
    call ram_wr
    inc bx
    mov al, 0x0B
    call ram_wr
    push es
    mov es, [_c64_m+CM_RAMSEG]
    mov byte [es:0x0B00], 0x02      ; the handler is one KIL
    pop es
    mov si, p_cyc_brk
    call runprog
    mov ax, [_c64_m+CM_CNT]
    neg ax
    add ax, 200
    cmp ax, 7 + 2                   ; BRK (7) + the handler's KIL (2)
    jne .bad

    ; --- ...AND AN IRQ ENTRY IS SEVEN, which is where the seven belongs.
    mov word [_c64_m+CM_PC], 0x0800
    mov word [_c64_m+CM_S], 0x00FD
    mov word [_c64_m+CM_P], 0x0020  ; I clear
    push es
    mov es, [_c64_m+CM_RAMSEG]
    mov byte [es:0x0800], 0x02
    pop es
    call rebank
    mov bx, C64_SCR_IRQ
    mov al, 1
    call scr_wr
    PUSHI 200
    call _c64_run
    add sp, 2
    mov ax, [_c64_m+CM_CNT]
    neg ax
    add ax, 200
    cmp ax, 7 + 2                   ; the vector (7) + the handler's KIL
    jne .bad
    mov bx, C64_SCR_IRQ
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

; --- the table: {program, cycles}, and every program ends in KIL (2) --------
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

; LDA #$00 / LDX #$01 / LDA $10FF,X / KIL
p_cyc_a:
    db 8
    db 0xA9, 0x00
    db 0xA2, 0x01
    db 0xBD, 0xFF, 0x10
    db 0x02
; LDA #$00 / LDX #$01 / LDA $1000,X / KIL
p_cyc_b:
    db 8
    db 0xA9, 0x00
    db 0xA2, 0x01
    db 0xBD, 0x00, 0x10
    db 0x02
; LDA #$00 / BEQ +1 / KIL / KIL
p_cyc_c:
    db 6
    db 0xA9, 0x00
    db 0xF0, 0x01
    db 0x02
    db 0x02
; LDA #$01 / BEQ +1 / KIL / KIL   - NOT taken, so the branch is 2
p_cyc_nt:
    db 6
    db 0xA9, 0x01
    db 0xF0, 0x01
    db 0x02
    db 0x02
; LDA $10 / KIL
p_cyc_zp:
    db 3
    db 0xA5, 0x10
    db 0x02
; LDX #$01 / LDA $10,X / KIL
p_cyc_zpx:
    db 5
    db 0xA2, 0x01
    db 0xB5, 0x10
    db 0x02
; LDA $1234 / KIL
p_cyc_abs:
    db 4
    db 0xAD, 0x34, 0x12
    db 0x02
; LDX #$01 / STA $12FF,X / KIL  - a STORE pays no page-cross cycle
p_cyc_stax:
    db 6
    db 0xA2, 0x01
    db 0x9D, 0xFF, 0x12
    db 0x02
; ASL $10 / KIL
p_cyc_aslz:
    db 3
    db 0x06, 0x10
    db 0x02
; ASL $1234 / KIL
p_cyc_asla:
    db 4
    db 0x0E, 0x34, 0x12
    db 0x02
; LDX #$01 / INC $1200,X / KIL
p_cyc_incx:
    db 6
    db 0xA2, 0x01
    db 0xFE, 0x00, 0x12
    db 0x02
; LDA #$12 / PHA / PLA / KIL
p_cyc_stk:
    db 5
    db 0xA9, 0x12
    db 0x48
    db 0x68
    db 0x02
; JSR $0806 / KIL / pad / RTS
p_cyc_jsr:
    db 7
    db 0x20, 0x06, 0x08
    db 0x02
    db 0xEA, 0xEA
    db 0x60
; JMP $0803 / KIL
p_cyc_jmp:
    db 4
    db 0x4C, 0x03, 0x08
    db 0x02
; LDA #$0B / STA $10 / LDA #$08 / STA $11 / JMP ($0010) / KIL
p_cyc_jmpi:
    db 12
    db 0xA9, 0x0B
    db 0x85, 0x10
    db 0xA9, 0x08
    db 0x85, 0x11
    db 0x6C, 0x10, 0x00
    db 0x02
; LDA #$00 / STA $10 / LDA #$12 / STA $11 / LDY #$01 / LDA ($10),Y / KIL
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
; LDA #$00 / STA $12 / LDA #$12 / STA $13 / LDX #$02 / LDA ($10,X) / KIL
p_cyc_indx:
    db 13
    db 0xA9, 0x00
    db 0x85, 0x12
    db 0xA9, 0x12
    db 0x85, 0x13
    db 0xA2, 0x02
    db 0xA1, 0x10
    db 0x02
; LDA #$00 / BEQ +1 / KIL / KIL  - LOADED AT $08FB, so the branch crosses
p_cyc_cross:
    db 6
    db 0xA9, 0x00
    db 0xF0, 0x01
    db 0x02
    db 0x02
; BRK / pad - the handler is a KIL at the IRQ vector
p_cyc_brk:
    db 2
    db 0x00, 0xEA

patch_cyc:                          ; LDA abs,X costs one cycle too few
    mov byte [cs:c64_cyc + 0xBD], 3
    ret
unpatch_cyc:
    mov byte [cs:c64_cyc + 0xBD], 4
    ret

; -----------------------------------------------------------------------------
; ROW 10 - DECIMAL MODE, AGAINST AN INDEPENDENT REFERENCE
;
; Klaus Dormann's 6502_decimal_test is published as SOURCE only - there is no
; built binary in bin_files, which is why C64-SPEC §4.6's decimal row is
; this instead, and says so: tools/c64dec.py computes, from the documented
; NMOS rules and in Python, a 16-bit checksum over ALL 262,144 cases (ADC and
; SBC, every A, every operand, carry in both ways), and the core is run over
; the same 262,144 cases here and must produce the same four checksums.
;
; A checksum is the right shape for this and not a shortcut: any single wrong
; result in any of the four sweeps changes it, and 262,144 expected answers
; will not fit in a boot sector.
; -----------------------------------------------------------------------------
row_dec:
    push si
    mov al, 0
    call bank_set
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

; dec_sweep - DI = the sweep; run 65,536 cases and fold A and P into a 16-bit
; checksum. The program is four bytes: SED, the operation, KIL - and the
; harness sets A, the operand and C directly, which is what makes 65,536 runs
; of it affordable.
dec_sweep:
    push bx
    push cx
    push dx
    mov word [dec_sum], 0
    mov word [dec_am], 0            ; the accumulator and the operand, IN
.a:                                 ; MEMORY - see row_maps
    push es
    mov es, [_c64_m+CM_RAMSEG]
    mov byte [es:0x0800], 0xF8      ; SED
    mov al, 0x69                    ; ADC #
    test byte [decsw], 2
    jz .isadc
    mov al, 0xE9                    ; SBC #
.isadc:
    mov [es:0x0801], al
    mov al, [dec_am]                ; the operand
    mov [es:0x0802], al
    mov byte [es:0x0803], 0x02      ; KIL
    pop es
    mov word [_c64_m+CM_PC], 0x0800
    mov word [_c64_m+CM_S], 0x00FD
    mov al, 0x20
    test byte [decsw], 1
    jz .noc
    or al, 0x01                     ; C set going in
.noc:
    xor ah, ah
    mov [_c64_m+CM_P], ax
    mov al, [dec_am+1]              ; the accumulator
    xor ah, ah
    mov [_c64_m+CM_A], ax
    call rebank
    PUSHI 40
    call _c64_run
    add sp, 2
    ; fold A and the N, V, Z and C bits of P into the checksum
    mov ax, [_c64_m+CM_A]
    mov bx, [_c64_m+CM_P]
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
    mov ax, [cs:c64_tab + 0xD8*2]   ; is never taken and every answer is binary
    mov [cs:c64_tab + 0xF8*2], ax
    ret
unpatch_dec:
    mov word [cs:c64_tab + 0xF8*2], o_sed
    ret
%endif

; =============================================================================
; ROW 1 - KLAUS DORMANN'S 6502 FUNCTIONAL TEST
;
; The 64KB image is appended to the floppy by the script and read straight into
; the C64's RAM. It starts at $0400 and ends by looping on itself: the success
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
    ; ...AND THE CORE'S SCRATCH IS CLEARED AFTER THE LOAD. The fixture is a
    ; whole 64KB image and it lands ON TOP of $FFC0-$FFF9, so whatever the
    ; test has at those addresses becomes the pending-interrupt byte, the
    ; countdown and the dirty bitmap. The first run then took an NMI nobody
    ; raised and settled on the test's own nmi_trap at $379D. c64.c does the
    ; same thing at launch (c64_scratch_clear), and this is 3.5's deviation
    ; being paid for where it is owed.
    push es
    mov es, [_c64_m+CM_RAMSEG]
    mov di, C64_SCR_BASE
    mov cx, C64_SCR_END
    xor al, al
    rep stosb
    pop es

    mov al, 0                       ; all RAM, which is what the test wants
    call bank_set
    mov word [_c64_m+CM_PC], 0x0400
    mov word [_c64_m+CM_S], 0x00FD
    mov word [_c64_m+CM_P], 0x0024
    mov word [_c64_m+CM_A], 0
    mov word [_c64_m+CM_X], 0
    mov word [_c64_m+CM_Y], 0
    call rebank
%ifdef NEG
    ; THE NEGATIVE CONTROL: ADC # dispatched to ORA #, which Dormann's test
    ; notices within its first few hundred instructions. A run that reaches
    ; the success trap with this in place would mean the test was not being
    ; run at all.
    mov ax, [cs:c64_tab + 0x09*2]
    mov [cs:c64_tab + 0x69*2], ax
    mov si, msg_neg
    call puts
%endif
    ; 30,000 AND NOT 60,000: the countdown is a SIGNED word
    ; (C64-SPEC §4.2), so a budget over 32,767 arrives negative and the core
    ; expires before its first fetch - which reads exactly like a test that
    ; will not start. The package's own cap is 16,384, and 30,000 under warp
    ; for the same reason.
    mov cx, 20000                   ; outer passes of 30,000 cycles each
.run:
    push cx
    PUSHI 30000
    call _c64_run
    add sp, 2
    pop cx
    cmp ax, C64_RUN_JAM
    je .stuck
    ; ...has the program counter settled on a `jmp *`?
    mov ax, [_c64_m+CM_PC]
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
    mov ax, [_c64_m+CM_PC]
    call puthex
    jmp short .verdict
.settled:
    mov si, msg_settle
    call puts
    mov ax, [_c64_m+CM_PC]
    call puthex
.verdict:
    ; ...and the machine it settled with, because "it stopped at $XXXX" names
    ; the trap and the registers name the reason
    mov si, msg_regs
    call puts
    mov ax, [_c64_m+CM_A]
    call puthex
    mov al, ' '
    call putc
    mov ax, [_c64_m+CM_X]
    call puthex
    mov al, ' '
    call putc
    mov ax, [_c64_m+CM_Y]
    call puthex
    mov al, ' '
    call putc
    mov ax, [_c64_m+CM_S]
    call puthex
    mov al, ' '
    call putc
    mov ax, [_c64_m+CM_P]
    call puthex
    mov si, msg_stk
    call puts
    mov bx, 0x0100
    call ram_rd
    xor ah, ah
    call puthex
    mov al, ' '
    call putc
    mov bx, 0x0101
    call ram_rd
    xor ah, ah
    call puthex
    mov al, ' '
    call putc
    mov bx, 0x01FF
    call ram_rd
    xor ah, ah
    call puthex
    mov si, msg_nl
    call puts
    mov ax, [_c64_m+CM_PC]
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
; THE ROUTINES UNDER TEST - THE SHIPPING TEXT
; =============================================================================
%include "c64cpu.inc"
%include "c64mem.inc"

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
pp_ddr:     db 0x2F
pp_data:    db 0x37
neg_map:    db 0
neg_port:   db 0
neg_stale:  db 0
neg_io:     db 0
neg_int:    db 0
saw0:       db 0
saw1:       db 0
saw2:       db 0
esfix_save: dw 0, 0
io_reads:   dw 0
io_writes:  dw 0
io_last_a:  dw 0
io_last_v:  db 0
dec_sum:    dw 0
dec_am:     dw 0                    ; low = the operand, high = the accumulator
decsw:      db 0                    ; which of the four sweeps
rowi:       db 0
%ifndef DORMANN
; the four checksums tools/c64dec.py computes from the documented NMOS rules
dec_want:   dw DECADC0, DECADC1, DECSBC0, DECSBC1
%endif

msg_hello:  db 13, 10, 'c64cputest: the 6510 core on a real x86, SS != DS', 13, 10, 0
msg_r2:     db 'row 2  the seven bank maps            ', 0
msg_r3:     db 'row 3  $00/$01 are not RAM            ', 0
msg_r4:     db 'row 4  a fetch across a boundary      ', 0
msg_r5:     db 'row 5  real I/O stub returns          ', 0
msg_r6:     db 'row 6  ES and DS come back            ', 0
msg_r7:     db 'row 7  IRQ and NMI entry              ', 0
msg_r8:     db 'row 8  the illegal opcodes            ', 0
msg_r9:     db 'row 9  cycle totals and the penalties ', 0
msg_r10:    db 'row 10 decimal ADC/SBC, all 262144    ', 0
msg_r11:    db 'row 11 the four unstable stores       ', 0
msg_r12:    db 'row 12 the interrupt stack and RTI    ', 0
msg_ok:     db 'ok ', 0
msg_bad:    db 'FAIL ', 0
msg_nok:    db '(control fails: ok)', 13, 10, 0
msg_nbad:   db 'CONTROL PASSED - the check proves nothing', 13, 10, 0
msg_pass:   db 13, 10, 'c64cputest: PASS', 13, 10, 0
msg_fail:   db 13, 10, 'c64cputest: FAIL', 13, 10, 0
msg_dorm:   db 'Dormann 6502 functional test, 64KB at $0400', 13, 10, 0
msg_neg:    db 'NEGATIVE CONTROL: ADC # is dispatched to ORA #', 13, 10, 0
msg_settle: db 'settled at $', 0
msg_regs:   db '  A X Y S P = ', 0
msg_stk:    db '  $0100 $0101 $01FF = ', 0
msg_jam:    db 'JAM at $', 0
msg_slow:   db 'never settled (the budget ran out)', 13, 10, 0
msg_nl:     db 13, 10, 0

