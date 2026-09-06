; =============================================================================
; os8088 - apps/c64/c64cpu.inc          the 6510 core (C64-SPEC §4)
;
; Part of C64, a reimplementation of VICE 3.10's `x64` (docs/C64-SPEC.md).
; VICE is Copyright (C) 1996-2025 the VICE team, GPL-2-or-later; apps/c64/ is
; GPL-2-or-later and apps/c64/COPYING is the licence text. Nothing of VICE's
; source is vendored: the opcode semantics, the illegal opcodes and the
; per-opcode cycle costs come from src/6510core.c and src/interrupt.c, and are
; reimplemented here.
;
; %included by apps/c64/c64.asm after the compiled C, and by
; hosttest/c64cputest.asm - the boot-sector harness that runs THIS text on a
; real x86 with SS != DS (C64-SPEC §4.6, the rcz80test precedent).
;
; -----------------------------------------------------------------------------
; THE REGISTER PLAN (C64-SPEC §4.1)
; -----------------------------------------------------------------------------
;   A          AL
;   P N,Z,C    AH        in `lahf` LAYOUT - N is SF (bit 7), Z is ZF (bit 6),
;                        C is CF (bit 0), so most results are one `lahf` and a
;                        mask, and ADC/SBC get their carry in with `shr ah,1`
;   P V,D,I,B  CH        at their 6502 bit positions (V 0x40, B 0x10, D 0x08,
;                        I 0x04). NEVER a cs: static: a write into the
;                        package's own code page is LESSONS.md 13's TCG slow
;                        path, and V moves on every ADC/SBC/BIT
;   X          CL        indexed addressing is `add bl,cl / adc bh,0`, whose
;                        carry IS the page-cross penalty of C64-SPEC §4.2 - so CH is
;                        free for the flags at no cost
;   Y          DL        DH is the MEMORY DATA byte and a free scratch: Y is
;                        indexed with `add bl,dl / adc bh,0`, so nothing ever
;                        needs DX as a word
;   PC         SI
;   S          DI        held as the FULL stack address 0x0100+S, so a push is
;                        `mov [di],v / dec di / or di,0x0100` and the page
;                        wrap costs one instruction instead of a mask and an
;                        add
;   EA         BX
;   RAM        DS        the 64KB claim, for the whole of _c64_run
;   fetch      ES        C64-SPEC §4.3's bias
;   frame      BP        the cdecl frame, and TWELVE BYTES OF SCRATCH below it
;                        (CT_* - SS != DS is used deliberately: `[bp+disp]`
;                        addresses SS, which is where the frame is). The
;                        decimal ADC/SBC of C64-SPEC §4.2 needs more temporaries than
;                        the plan has registers, and pushing inside a handler
;                        would put them where a fetch cannot reach them
;
; The package's statics are reached with `cs:` because DS is the C64's RAM for
; the whole run; CS == DS in a package (SPEC.md 20.2) and in the harness, so
; `mov ax,cs / mov ds,ax` is how the package's segment comes back.
;
; -----------------------------------------------------------------------------
; TIME IS 6510 CYCLES (C64-SPEC §4.2)
; -----------------------------------------------------------------------------
; Every opcode carries its real cost from 6510core.c's table, and the core
; subtracts it from ONE countdown - C64_SCR_DEAD, in the emulated machine's own
; scratch (C64-SPEC §3.5), never in the package's bss. The page-cross penalty on the
; indexed read modes and the taken-branch and branch-page-cross penalties are
; charged by the addressing routines and the branch handlers themselves.
;
; The countdown is checked BETWEEN instructions and never inside one, so a
; caller never has to resume a half-executed opcode. `c64_run(n)` may overrun
; n by at most the cost of one instruction (7 cycles); the leftover - which is
; then negative - is left in c64_m.cnt, so the C computes what actually ran as
; `n - c64_m.cnt` and nothing is lost or double-counted.
;
; -----------------------------------------------------------------------------
; THE ALARM MODEL (C64-SPEC §4.4), AND ONE AMENDMENT THIS WAVE MADE
; -----------------------------------------------------------------------------
; VICE's alarm model is "run to the next device event, service it, compute the
; next one", and that is exactly what happens - but the service call is a
; RETURN to the C rather than a cdecl call out of the core. The core keeps the
; whole 6510 in registers; a call out of it has to save and reload every one of
; them, which is the same work as the entry/exit shell, so a call out would
; cost what a return costs and be harder to read. `os88_onwake` runs the loop
; (c64.c), and every device phase is retained across it exactly as C64-SPEC §4.4 says.
;
; The one thing that MUST be a call out is a $D000-$DFFF access in an I/O bank:
; that happens inside an instruction. c64_io_rd_bx / c64_io_wr_bx save the
; core's live registers, swap DS to the package, call, and put DS, ES and the
; core's registers back (C64-SPEC §3.4). ES is caller-clobbered by the C ABI, which is
; why it is reloaded from the cached fetch segment and not merely trusted.
;
; INTERRUPTS are taken between instructions, at the top of `_c64_run` and at
; the three instructions that can unmask one - CLI, PLP and RTI. That is the
; whole of the cost: no per-instruction test of the pending flags. The C raises
; a line from an alarm, and an alarm is what ended the run, so the latency of
; an alarm-raised IRQ is zero instructions; an IRQ enabled by an I/O write
; inside a run is taken when that run ends, which c64_cut() makes immediate and
; exact (it moves the unspent countdown into C64_SCR_CARRY so the cycle
; accounting does not move).
;
; All cdecl (SPEC.md 73.3): arguments at [bp+4], [bp+6], ...; the answer in
; AX; BP, DS, SS:SP and DF preserved; AX BX CX DX SI DI are the caller's to
; lose. ES is the KERNEL's on entry to every callback (SPEC.md 20.4) and is
; put back before returning.
; =============================================================================

section .bss

; --- the register file (C64-SPEC §4.1) --------------------------------
; Reached from C as `extern struct c64_mach c64_m;` - the field order below IS
; the struct's, and the two cannot drift without the C reading the wrong word.
; CM_P is a REAL 6502 P byte in both directions: the core unpacks it into
; AH/CH on entry and packs it back on exit, once per call, so the C and the
; harness read the register the machine has rather than this file's layout.
_c64_m:
    resw 1                          ; CM_RAMSEG  the 64KB RAM claim
    resw 1                          ; CM_ROMSEG  the 20KB ROM claim
    resw 1                          ; CM_PC
    resw 1                          ; CM_A       (low byte)
    resw 1                          ; CM_X
    resw 1                          ; CM_Y
    resw 1                          ; CM_S       (low byte, 0..255)
    resw 1                          ; CM_P       (a 6502 P byte)
    resw 1                          ; CM_CNT     cycles LEFT of the last run
    resw 1                          ; CM_REASON  C64_RUN_* of the last return

CM_RAMSEG equ 0
CM_ROMSEG equ 2
CM_PC     equ 4
CM_A      equ 6
CM_X      equ 8
CM_Y      equ 10
CM_S      equ 12
CM_P      equ 14
CM_CNT    equ 16
CM_REASON equ 18

section .text

; --- what _c64_run answers (C64-SPEC §4.5) ----------------------------
C64_RUN_SLICE equ 0                 ; the wall-slice budget was spent
C64_RUN_JAM   equ 1                 ; a KIL/JAM opcode; the machine stops

; --- the core's scratch, in the emulated machine (C64-SPEC §3.5) ------
; 58 bytes at the top of the C64's own RAM, BELOW the six vector bytes: a hot
; counter in the package's bss is a TCG slow path under QEMU when that page
; also holds translated code, and it cost RUNCPM 5x on the whole Z80 core
; (LESSONS.md 13). $FFFA-$FFFF stay real RAM, because a program that banks the
; KERNAL out puts its own vectors there.
;
; Two stated deviations follow, and hosttest/c64cputest.asm holds a case for
; each: a read of $FFC0-$FFF9 in an all-RAM map reads the scratch, and a write
; there is dropped.
C64_SCR_BASE  equ 0xFFC0
; ...and the SAME address as a SIGNED displacement, for the one access that
; reaches it through a base register. NASM 2.16 tightened `number-overflow`:
; `[bx + 0xFFC0]` is a 16-bit displacement and 65,472 is outside the signed
; range it now insists on, so `-w+error` refuses the line - `byte data exceeds
; bounds`, at a line with no `db` on it. The encoding is identical either way;
; only the spelling moves. Without this, `make c64` does not assemble on a
; 2.16 toolchain, and nothing in `all` builds C64 - so the tree went on
; passing every gate it has while its largest C package could not be built.
C64_SCR_DISP  equ C64_SCR_BASE - 0x10000
C64_SCR_DIRTY equ 0x00              ; 32 bytes: the 256-page dirty bitmap (9.2)
C64_SCR_BLO   equ 0x20              ; the LOW edge of the region ES is biased
                                    ;   for (4.3). The guard is a RANGE and
                                    ;   not a ceiling, which is what makes a
                                    ;   BACKWARD control transfer safe without
                                    ;   re-biasing on every taken branch: a
                                    ;   JMP from $E000 to $0400 leaves PC
                                    ;   below the biased region, not above it
C64_SCR_CARRY equ 0x22              ; cycles c64_cut() took out of DEAD, put
                                    ;   back at the expiry so the accounting
                                    ;   `ran = asked - cnt` stays exact
C64_SCR_DEAD  equ 0x24              ; THE countdown - the hot counter, signed
C64_SCR_BOUND equ 0x26              ; the next mapping boundary above PC (4.3)
C64_SCR_FES   equ 0x28              ; the cached fetch ES (4.3)
C64_SCR_IRQ   equ 0x2A              ; bit 0 = the IRQ line is asserted (a
                                    ;   LEVEL the C maintains), bit 1 = an NMI
                                    ;   EDGE is pending (the core clears it)
C64_SCR_WLO   equ 0x2C              ; the write window: the lowest and the
C64_SCR_WHI   equ 0x2E              ;   highest address written since the last
                                    ;   flush (C64-SPEC §9.2)
C64_SCR_ANY   equ 0x30              ; ...and one byte that says the core wrote
                                    ;   SOMETHING, so a wake can decide to
                                    ;   flush in one read instead of 32
C64_SCR_WATLO equ 0x32              ; the WATCH RANGE the window is taken over:
C64_SCR_WATHI equ 0x34              ;   mbase .. mbase+999, written by the C
                                    ;   from c64_frame_regs (C64-SPEC §9.2)
C64_SCR_MAPA  equ 0x36              ; what $A000-$BFFF shows  (C64_MAP_*)
C64_SCR_MAPD  equ 0x37              ; ...$D000-$DFFF
C64_SCR_MAPE  equ 0x38              ; ...$E000-$FFFF
C64_SCR_END   equ 0x3A              ; ...$FFC0 + 0x3A - 1 = $FFF9

; the seven bank maps' cell values - c64io.c's C64_MAP_* and these are ONE
; enumeration, and c64io.c writes them into MAPA/MAPD/MAPE on every $00/$01
; change (3.2)
C64_MAP_RAM     equ 0
C64_MAP_BASIC   equ 1
C64_MAP_KERNAL  equ 2
C64_MAP_CHARGEN equ 3
C64_MAP_IO      equ 4

; C64.ROM's layout (1.4) as ADDRESS BIASES, so a ROM byte is one `mov` with a
; 16-bit effective address that wraps: KERNAL at ROM offset 0 covers
; $E000-$FFFF, so [es:bx+0x2000] with bx = $E000 is ROM offset 0.
C64_ROMB_KERNAL  equ 0x2000
C64_ROMB_BASIC   equ 0x8000         ; BASIC at $2000 covers $A000-$BFFF
C64_ROMB_CHARGEN equ 0x7000         ; CHARGEN at $4000 covers $D000-$DFFF
; ...and as SEGMENT biases, for the fetch, which cannot carry a displacement
C64_ROMS_KERNAL  equ 0x0E00
C64_ROMS_BASIC   equ 0x0800
C64_ROMS_CHARGEN equ 0x0900

; --- the frame scratch (see the head) ----------------------------------------
CT_B equ -10                        ; a general byte temporary
CT_A equ -11                        ; the decimal path's A
CT_M equ -12                        ; ...its operand
CT_C equ -13                        ; ...its carry in
CT_F equ -14                        ; ...the AH it is building
CT_T equ -15                        ; ...its low-nibble sum / half borrow
CT_W equ -18                        ; a WORD temporary (the branch's old PC)

; =============================================================================
; MACROS
; =============================================================================

; the stack page: DI is 0x0100+S throughout (C64-SPEC §4.1)
%macro PUSHB 1
    mov [di], %1
    call c64_stkdirty
    dec di
    or  di, 0x0100
%endmacro

%macro POPB 1
    inc di
    and di, 0x00FF                  ; ...AND THEN THE PAGE BACK ON. `and di,
    or  di, 0x0100                  ; 0x01FF` is the obvious one line and it is
    mov %1, [di]                    ; WRONG: 0x0200 & 0x01FF is 0x0000, not
%endmacro                           ; 0x0100, so a pull with S = $FF read byte
                                    ; ZERO of the address space and left every
                                    ; later push writing at $0000 downwards.
                                    ; BASIC boots straight through it - the
                                    ; KERNAL never wraps the stack - and Klaus
                                    ; Dormann's "proper stack wrap around" test
                                    ; at $0D89 is what caught it

; ROMBIAS - point ES at the ROM claim biased for one bank and deliver the
; byte. It goes through AX and an IMMEDIATE rather than through BX, because
; c64_rebias is reached from the middle of c64_ea_abs with half an effective
; address already in BX.
%macro ROMBIAS 1
    push ax
    mov ax, [cs:_c64_m+CM_ROMSEG]
    sub ax, %1
    mov es, ax
    pop ax
    jmp short c64_rebias.go
%endmacro

; OPR - leave the operand in DH. `imm` is a fetch; every other mode computes
; the effective address into BX and reads it through the banked reader.
%macro OPR 1
  %ifidni %1, imm
    call c64_fetch
  %else
    call c64_ea_%1
    call c64_rd_bx
  %endif
%endmacro

; =============================================================================
; int c64_run(unsigned cycles)
;
; Run the machine for at most `cycles` 6510 cycles and answer C64_RUN_SLICE or
; C64_RUN_JAM (C64-SPEC §4.5). c64_m.cnt is left holding what was NOT
; spent (negative by up to one instruction's cost), so the caller's
; `ran = cycles - cnt` is exact.
; =============================================================================
_c64_run:
    push bp
    mov bp, sp
    push si                         ; [bp-2]
    push di                         ; [bp-4]
    push ds                         ; [bp-6] the PACKAGE segment
    push es                         ; [bp-8] KERNEL_SEG (SPEC.md 20.4)
    sub sp, 12                      ; the CT_* scratch

    mov ax, [bp+4]
    mov si, [cs:_c64_m+CM_RAMSEG]
    mov ds, si
    mov [C64_SCR_BASE + C64_SCR_DEAD], ax
    mov word [C64_SCR_BASE + C64_SCR_CARRY], 0
    mov word [C64_SCR_BASE + C64_SCR_BOUND], 0   ; the first fetch re-biases

    mov al, [cs:_c64_m+CM_A]
    mov bl, [cs:_c64_m+CM_P]
    call c64_setp                   ; -> AH (N,Z,C) and CH (V,D,I,B)
    mov cl, [cs:_c64_m+CM_X]
    mov dl, [cs:_c64_m+CM_Y]
    mov si, [cs:_c64_m+CM_PC]
    mov di, [cs:_c64_m+CM_S]
    and di, 0x00FF
    or  di, 0x0100

    ; an interrupt pending at the top of a run is taken before the first
    ; instruction (C64-SPEC §4.4): the alarm that raised it is what ended the last run
    test byte [C64_SCR_BASE + C64_SCR_IRQ], 0x02
    jnz c64_do_nmi
    jmp c64_irq_chk

; -----------------------------------------------------------------------------
; THE FETCH AND THE DISPATCH
; -----------------------------------------------------------------------------
c64_next:
    cmp word [C64_SCR_BASE + C64_SCR_DEAD], 0
    jle c64_expire
    cmp si, [C64_SCR_BASE + C64_SCR_BOUND]
    jae .slow
    cmp si, [C64_SCR_BASE + C64_SCR_BLO]
    jb  .slow
    mov bl, [es:si]
    inc si
.disp:
    xor bh, bh
    mov dh, [cs:bx+c64_cyc]
    sub [C64_SCR_BASE + C64_SCR_DEAD], dh
    sbb byte [C64_SCR_BASE + C64_SCR_DEAD + 1], 0
    shl bx, 1
    jmp [cs:bx+c64_tab]
.slow:
    call c64_rebias
    mov bl, dh
    jmp short .disp

; c64_fetch - one operand byte, through the same guarded path.
c64_fetch:
    cmp si, [C64_SCR_BASE + C64_SCR_BOUND]
    jae c64_rebias
    cmp si, [C64_SCR_BASE + C64_SCR_BLO]
    jb  c64_rebias
    mov dh, [es:si]
    inc si
    ret

; c64_rebias - PC has left the region ES is biased for (or the map changed).
; Work out the region, set ES, BLO and BOUND, and deliver the byte. A fetch
; from $D000-$DFFF with I/O mapped, or from $0000/$0001, is the TRUE SLOW
; FETCH PATH of C64-SPEC §3.4: the biased ES physically cannot read a
; register file, so the byte comes through the same call out the data path
; uses and the region is left marked "always re-bias".
c64_rebias:
    cmp si, 2
    jb  .slow                       ; $0000/$0001 are the processor port (3.2)
    cmp si, 0xA000
    jb  .low
    cmp si, 0xC000
    jb  .a000
    cmp si, 0xD000
    jb  .c000
    cmp si, 0xE000
    jb  .d000
    ; --- $E000-$FFFF. BOUND is $FFFF and not the wrap, so the last byte of
    ; the address space re-biases and the wrap to $0000 cannot be fetched
    ; through the KERNAL's bias.
    mov word [C64_SCR_BASE + C64_SCR_BLO], 0xE000
    mov word [C64_SCR_BASE + C64_SCR_BOUND], 0xFFFF
    cmp byte [C64_SCR_BASE + C64_SCR_MAPE], C64_MAP_KERNAL
    jne .ram
    ROMBIAS C64_ROMS_KERNAL
.low:
    mov word [C64_SCR_BASE + C64_SCR_BLO], 2
    mov word [C64_SCR_BASE + C64_SCR_BOUND], 0xA000
    jmp short .ram
.a000:
    mov word [C64_SCR_BASE + C64_SCR_BLO], 0xA000
    mov word [C64_SCR_BASE + C64_SCR_BOUND], 0xC000
    cmp byte [C64_SCR_BASE + C64_SCR_MAPA], C64_MAP_BASIC
    jne .ram
    ROMBIAS C64_ROMS_BASIC
.c000:
    mov word [C64_SCR_BASE + C64_SCR_BLO], 0xC000
    mov word [C64_SCR_BASE + C64_SCR_BOUND], 0xD000
    jmp short .ram
.d000:
    cmp byte [C64_SCR_BASE + C64_SCR_MAPD], C64_MAP_IO
    je  .slow
    mov word [C64_SCR_BASE + C64_SCR_BLO], 0xD000
    mov word [C64_SCR_BASE + C64_SCR_BOUND], 0xE000
    cmp byte [C64_SCR_BASE + C64_SCR_MAPD], C64_MAP_CHARGEN
    jne .ram
    ROMBIAS C64_ROMS_CHARGEN
.ram:
    mov es, [cs:_c64_m+CM_RAMSEG]
.go:
    mov [C64_SCR_BASE + C64_SCR_FES], es
    mov dh, [es:si]
    inc si
    ret
.slow:
    ; every fetch from here re-enters this routine, which is what a fetch out
    ; of a register file costs and what it is meant to cost
    mov word [C64_SCR_BASE + C64_SCR_BOUND], 0
    mov word [C64_SCR_BASE + C64_SCR_BLO], 0
    push bx
    mov bx, si
    call c64_io_rd_bx
    pop bx
    inc si
    ret

; -----------------------------------------------------------------------------
; THE EXITS
; -----------------------------------------------------------------------------
c64_expire:
    ; put back whatever c64_cut() took out, so `ran = asked - cnt` is exact
    push bx
    mov bx, [C64_SCR_BASE + C64_SCR_CARRY]
    mov word [C64_SCR_BASE + C64_SCR_CARRY], 0
    add [C64_SCR_BASE + C64_SCR_DEAD], bx
    pop bx
    mov word [cs:_c64_m+CM_REASON], C64_RUN_SLICE
    jmp short c64_out

o_jam:
    dec si                          ; PC points AT the opcode that jammed
    mov word [cs:_c64_m+CM_REASON], C64_RUN_JAM

; DS is still the RAM claim here, so the countdown is read before it goes.
c64_out:
    mov [cs:_c64_m+CM_PC], si
    mov bx, [C64_SCR_BASE + C64_SCR_DEAD]
    mov [cs:_c64_m+CM_CNT], bx
    mov bx, di
    and bx, 0x00FF
    mov [cs:_c64_m+CM_S], bx
    mov bl, al
    xor bh, bh
    mov [cs:_c64_m+CM_A], bx
    mov bl, cl
    mov [cs:_c64_m+CM_X], bx
    mov bl, dl
    mov [cs:_c64_m+CM_Y], bx
    call c64_getp                   ; -> BL, and BH is the scratch it uses
    xor bh, bh
    mov [cs:_c64_m+CM_P], bx
    mov ax, [cs:_c64_m+CM_REASON]
    add sp, 12
    pop es
    pop ds
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; P, PACKED AND UNPACKED
; -----------------------------------------------------------------------------
; c64_getp - AH (N,Z,C) + CH (V,D,I,B) -> a 6502 P byte in BL. Clobbers BH.
c64_getp:
    mov bl, ah
    and bl, 0x81                    ; N and C are already in place
    mov bh, ah
    and bh, 0x40                    ; Z sits at bit 6 and belongs at bit 1
    rol bh, 1
    rol bh, 1
    rol bh, 1
    or  bl, bh
    mov bh, ch
    and bh, 0x5C                    ; V, B, D, I
    or  bl, bh
    or  bl, 0x20                    ; bit 5 always reads 1
    ret

; c64_setp - a 6502 P byte in BL -> AH and CH. Clobbers BH.
c64_setp:
    mov ah, bl
    and ah, 0x81
    mov bh, bl
    and bh, 0x02
    ror bh, 1
    ror bh, 1
    ror bh, 1
    or  ah, bh
    mov ch, bl
    and ch, 0x5C
    ret

; -----------------------------------------------------------------------------
; MEMORY (C64-SPEC §3.4)
; -----------------------------------------------------------------------------
; c64_rd_bx - the byte at BX, into DH. Preserves everything else, BX included,
; because the read-modify-write opcodes need the address back.
c64_rd_bx:
    cmp bh, 0xA0
    jae .high
    cmp bx, 2
    jb  c64_io_rd_bx                ; $0000/$0001 are NOT RAM (3.2)
    mov dh, [bx]
    ret
.high:
    cmp bh, 0xC0
    jb  .a000
    cmp bh, 0xD0
    jb  .ram                        ; $C000-$CFFF is RAM in every map
    cmp bh, 0xE0
    jb  .d000
    cmp byte [C64_SCR_BASE + C64_SCR_MAPE], C64_MAP_KERNAL
    jne .ram
    push es
    mov es, [cs:_c64_m+CM_ROMSEG]
    mov dh, [es:bx+C64_ROMB_KERNAL]
    pop es
    ret
.a000:
    cmp byte [C64_SCR_BASE + C64_SCR_MAPA], C64_MAP_BASIC
    jne .ram
    push es
    mov es, [cs:_c64_m+CM_ROMSEG]
    mov dh, [es:bx+C64_ROMB_BASIC]
    pop es
    ret
.d000:
    cmp byte [C64_SCR_BASE + C64_SCR_MAPD], C64_MAP_IO
    je  c64_io_rd_bx
    cmp byte [C64_SCR_BASE + C64_SCR_MAPD], C64_MAP_CHARGEN
    jne .ram
    push es
    mov es, [cs:_c64_m+CM_ROMSEG]
    mov dh, [es:bx+C64_ROMB_CHARGEN]
    pop es
    ret
.ram:
    mov dh, [bx]
    ret

; c64_wr_bx - DH into the byte at BX, and ONE BIT into the 256-page dirty
; bitmap (C64-SPEC §9.2). Preserves every register, DH included: SLO, RLA, SRE, RRA,
; DCP and ISB all use the written value afterwards.
;
; A write is RAM everywhere except the I/O window and $0000/$0001 - the 6510
; has no writable ROM - and a write into the core's own scratch is dropped
; (3.5's second stated deviation).
c64_wr_bx:
    cmp bh, 0xD0
    jae .high
    cmp bx, 2
    jb  c64_io_wr_bx
.store:
    mov [bx], dh
    push bx
    push dx
    mov bl, bh                      ; the page
    xor bh, bh
    mov dh, [cs:bx+c64_dmask]
    shr bl, 1
    shr bl, 1
    shr bl, 1
    or  [bx + C64_SCR_DISP + C64_SCR_DIRTY], dh
    pop dx
    pop bx
    mov byte [C64_SCR_BASE + C64_SCR_ANY], 1
    ; the WRITE WINDOW, taken over the WATCH RANGE and not over the address
    ; space (C64-SPEC §9.2): a JSR writes the stack and a BASIC statement zero page, so
    ; a window taken over everything spans every matrix row inside one slice
    ; and the per-row intersection degenerates to "all forty cells".
    cmp bx, [C64_SCR_BASE + C64_SCR_WATLO]
    jb  .out
    cmp bx, [C64_SCR_BASE + C64_SCR_WATHI]
    ja  .out
    cmp bx, [C64_SCR_BASE + C64_SCR_WLO]
    jae .nolo
    mov [C64_SCR_BASE + C64_SCR_WLO], bx
.nolo:
    cmp bx, [C64_SCR_BASE + C64_SCR_WHI]
    jbe .out
    mov [C64_SCR_BASE + C64_SCR_WHI], bx
.out:
    ret
.high:
    cmp bh, 0xE0
    jae .top
    cmp byte [C64_SCR_BASE + C64_SCR_MAPD], C64_MAP_IO
    je  c64_io_wr_bx
    jmp short .store
.top:
    cmp bx, C64_SCR_BASE
    jb  .store
    cmp bx, C64_SCR_BASE + C64_SCR_END
    jae .store
    ret                             ; the scratch is not writable memory (3.5)

; c64_stkdirty - a stack push is always RAM and always page 1, so its dirty
; bit is a constant and its window test needs no scratch register at all.
c64_stkdirty:
    or  byte [C64_SCR_BASE + C64_SCR_DIRTY], 0x40
    mov byte [C64_SCR_BASE + C64_SCR_ANY], 1
    cmp di, [C64_SCR_BASE + C64_SCR_WATLO]
    jb  .out
    cmp di, [C64_SCR_BASE + C64_SCR_WATHI]
    ja  .out
    cmp di, [C64_SCR_BASE + C64_SCR_WLO]
    jae .nolo
    mov [C64_SCR_BASE + C64_SCR_WLO], di
.nolo:
    cmp di, [C64_SCR_BASE + C64_SCR_WHI]
    jbe .out
    mov [C64_SCR_BASE + C64_SCR_WHI], di
.out:
    ret

; -----------------------------------------------------------------------------
; THE CALLS OUT (C64-SPEC §3.4)
;
; DS is the package's for the length of the call and the RAM claim afterwards;
; ES is the C ABI's to clobber, so it comes back from the cached fetch segment
; rather than being trusted. Every core register survives.
; -----------------------------------------------------------------------------
c64_io_rd_bx:                       ; BX = address -> DH; BX preserved
    push si
    push di
    push cx
    push ax
    push dx
    push bx
    push bx                         ; the cdecl argument
    mov ax, cs
    mov ds, ax
    call _c64_io_rd
    add sp, 2
    mov cl, al                      ; CX is on the stack: CL is free
    pop bx
    pop dx
    pop ax
    mov dh, cl
    pop cx
    pop di
    pop si
    mov ds, [cs:_c64_m+CM_RAMSEG]
; c64_io_rd_esfix / c64_io_wr_esfix - the two instructions that put the fetch
; segment back, LABELLED because hosttest/c64cputest.asm's row 6 needs a
; DELIBERATELY STALE ES and the only honest way to make one is to NOP out the
; reload in the shipping text at runtime (C64-SPEC §4.6). Nothing in the
; package reaches these names.
c64_io_rd_esfix:
    mov es, [C64_SCR_BASE + C64_SCR_FES]
    ret

c64_io_wr_bx:                       ; BX = address, DH = value; both preserved
    push si
    push di
    push cx
    push ax
    push dx
    push bx
    mov al, dh
    xor ah, ah
    push ax                         ; the value
    push bx                         ; the address
    mov ax, cs
    mov ds, ax
    call _c64_io_wr
    add sp, 4
    pop bx
    pop dx
    pop ax
    pop cx
    pop di
    pop si
    mov ds, [cs:_c64_m+CM_RAMSEG]
c64_io_wr_esfix:
    mov es, [C64_SCR_BASE + C64_SCR_FES]
    ret

; -----------------------------------------------------------------------------
; ADDRESSING (C64-SPEC §4.2's page-cross penalty is charged HERE)
;
; The `p` forms are the read modes, which pay one more cycle when the index
; carries into the high byte. The bare forms are the write and read-modify-
; write modes, whose base cost in 6510core.c's table already includes it.
; -----------------------------------------------------------------------------
c64_ea_zp:
    call c64_fetch
    mov bl, dh
    xor bh, bh
    ret

c64_ea_zpx:
    call c64_fetch
    add dh, cl                      ; the zero-page wrap is the byte add
    mov bl, dh
    xor bh, bh
    ret

c64_ea_zpy:
    call c64_fetch
    add dh, dl
    mov bl, dh
    xor bh, bh
    ret

c64_ea_abs:
    call c64_fetch
    mov bl, dh
    call c64_fetch
    mov bh, dh
    ret

c64_ea_absx:
    call c64_ea_abs
    add bl, cl
    adc bh, 0
    ret

c64_ea_absy:
    call c64_ea_abs
    add bl, dl
    adc bh, 0
    ret

; (zp,X) - the pointer is read through the banked reader, so a pointer at
; $0000/$0001 is the processor port and not the two RAM bytes under it.
c64_ea_indx:
    call c64_fetch
    add dh, cl
    mov bl, dh
    xor bh, bh
    call c64_rd_bx
    push dx                         ; the low byte, and Y with it
    inc bl                          ; the zero-page wrap of the pointer
    call c64_rd_bx
    mov bh, dh
    pop dx
    mov bl, dh
    ret

c64_ea_indp:                        ; the (zp) pointer, unindexed
    call c64_fetch
    mov bl, dh
    xor bh, bh
    call c64_rd_bx
    push dx
    inc bl
    call c64_rd_bx
    mov bh, dh
    pop dx
    mov bl, dh
    ret

c64_ea_indy:
    call c64_ea_indp
    add bl, dl
    adc bh, 0
    ret

; ...and the three READ forms, which pay the page-cross cycle. They are kept
; together so that every `jnc c64_ea_ret` below is a short forward jump.
c64_ea_absxp:
    call c64_ea_abs
    add bl, cl
    jnc c64_ea_ret
    inc bh
    jmp short c64_ea_pen

c64_ea_absyp:
    call c64_ea_abs
    add bl, dl
    jnc c64_ea_ret
    inc bh
    jmp short c64_ea_pen

c64_ea_indyp:
    call c64_ea_indp
    add bl, dl
    jnc c64_ea_ret
    inc bh
c64_ea_pen:
    sub byte [C64_SCR_BASE + C64_SCR_DEAD], 1
    sbb byte [C64_SCR_BASE + C64_SCR_DEAD + 1], 0
c64_ea_ret:
    ret

; -----------------------------------------------------------------------------
; THE FLAG TAILS
; -----------------------------------------------------------------------------
c64_nz_al:
    and ah, 0x01                    ; C survives; N and Z are recomputed
    or  al, al
    jz  .z
    js  .n
    jmp c64_next
.z:
    or  ah, 0x40
    jmp c64_next
.n:
    or  ah, 0x80
    jmp c64_next

c64_nz_dh:
    and ah, 0x01
    or  dh, dh
    jz  .z
    js  .n
    jmp c64_next
.z:
    or  ah, 0x40
    jmp c64_next
.n:
    or  ah, 0x80
    jmp c64_next

c64_nz_cl:
    and ah, 0x01
    or  cl, cl
    jz  .z
    js  .n
    jmp c64_next
.z:
    or  ah, 0x40
    jmp c64_next
.n:
    or  ah, 0x80
    jmp c64_next

c64_nz_dl:
    and ah, 0x01
    or  dl, dl
    jz  .z
    js  .n
    jmp c64_next
.z:
    or  ah, 0x40
    jmp c64_next
.n:
    or  ah, 0x80
    jmp c64_next

; after a CMP/CPX/CPY: N and Z from the difference, C is the INVERSE of the
; 8086's borrow
c64_cmpfin:
    lahf
    and ah, 0xC1
    xor ah, 0x01
    jmp c64_next

; after ROL/ROR: AH already holds the new carry in bit 0 (rcl/rcr set no sign
; or zero flag on the 8086, so N and Z are taken from the value)
c64_rot_dh:
    or  dh, dh
    jz  .z
    js  .n
    jmp c64_next
.z:
    or  ah, 0x40
    jmp c64_next
.n:
    or  ah, 0x80
    jmp c64_next

; =============================================================================
; ARITHMETIC (C64-SPEC §4.2 - decimal mode included, because Klaus
; Dormann's decimal test is one of the nine rows of C64-SPEC §4.6)
; =============================================================================
c64_adc:
    test ch, 0x08
    jnz c64_adc_dec
    shr ah, 1                       ; CF = C
    adc al, dh
    jo  .ov
    lahf
    and ah, 0xC1
    and ch, 0xBF
    jmp c64_next
.ov:
    lahf
    and ah, 0xC1
    or  ch, 0x40
    jmp c64_next

; Decimal ADC, exactly as the NMOS 6510 does it: Z comes from the BINARY sum,
; N and V from the high nibble BEFORE its decimal adjustment, C from after.
c64_adc_dec:
    mov [bp+CT_A], al
    mov [bp+CT_M], dh
    mov bl, ah
    and bl, 1
    mov [bp+CT_C], bl
    mov byte [bp+CT_F], 0
    mov bh, al                      ; the binary sum, for Z alone
    add bh, dh
    add bh, bl
    jnz .nz
    mov byte [bp+CT_F], 0x40
.nz:
    mov bl, al                      ; t = (A & 15) + (M & 15) + C
    and bl, 0x0F
    mov bh, dh
    and bh, 0x0F
    add bl, bh
    add bl, [bp+CT_C]
    cmp bl, 9
    jbe .t1
    add bl, 6
.t1:
    mov [bp+CT_T], bl
    mov bh, al                      ; h = (A >> 4) + (M >> 4) + (t > 15)
    shr bh, 1
    shr bh, 1
    shr bh, 1
    shr bh, 1
    mov bl, dh
    shr bl, 1
    shr bl, 1
    shr bl, 1
    shr bl, 1
    add bh, bl
    cmp byte [bp+CT_T], 0x0F
    jbe .h1
    inc bh
.h1:
    mov bl, bh                      ; the high nibble in place, for N and V
    shl bl, 1
    shl bl, 1
    shl bl, 1
    shl bl, 1
    test bl, 0x80
    jz  .n0
    or  byte [bp+CT_F], 0x80
.n0:
    mov dh, al                      ; V = (A ^ hi) & ~(A ^ M) & 0x80
    xor dh, bl
    mov al, [bp+CT_A]
    xor al, [bp+CT_M]
    not al
    and dh, al
    and ch, 0xBF
    test dh, 0x80
    jz  .v0
    or  ch, 0x40
.v0:
    cmp bh, 9
    jbe .h2
    add bh, 6
.h2:
    cmp bh, 0x0F
    jbe .c0
    or  byte [bp+CT_F], 0x01
.c0:
    mov al, bh
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1
    mov dh, [bp+CT_T]
    and dh, 0x0F
    or  al, dh
    mov ah, [bp+CT_F]
    jmp c64_next

c64_sbc:
    test ch, 0x08
    jnz c64_sbc_dec
    shr ah, 1
    cmc                             ; the 6502 borrows on NOT C
    sbb al, dh
    jo  .ov
    lahf
    and ah, 0xC1
    xor ah, 0x01
    and ch, 0xBF
    jmp c64_next
.ov:
    lahf
    and ah, 0xC1
    xor ah, 0x01
    or  ch, 0x40
    jmp c64_next

; Decimal SBC: on the NMOS 6510 every FLAG is the binary subtraction's and
; only A is adjusted, which is why the binary path runs first here.
c64_sbc_dec:
    mov [bp+CT_A], al
    mov [bp+CT_M], dh
    mov bl, ah
    and bl, 1
    mov [bp+CT_C], bl
    shr ah, 1
    cmc
    sbb al, dh
    jo  .ov
    lahf
    and ah, 0xC1
    xor ah, 0x01
    and ch, 0xBF
    jmp short .adj
.ov:
    lahf
    and ah, 0xC1
    xor ah, 0x01
    or  ch, 0x40
.adj:
    mov [bp+CT_F], ah
    mov bl, [bp+CT_A]               ; t = (A & 15) - (M & 15) - (1 - C)
    and bl, 0x0F
    mov bh, [bp+CT_M]
    and bh, 0x0F
    sub bl, bh
    mov bh, 1
    sub bh, [bp+CT_C]
    sub bl, bh
    mov byte [bp+CT_T], 0
    test bl, 0x10
    jz  .t1
    sub bl, 6
    mov byte [bp+CT_T], 1
.t1:
    and bl, 0x0F
    mov [bp+CT_B], bl
    mov bl, [bp+CT_A]               ; h = (A >> 4) - (M >> 4) - half borrow
    shr bl, 1
    shr bl, 1
    shr bl, 1
    shr bl, 1
    mov bh, [bp+CT_M]
    shr bh, 1
    shr bh, 1
    shr bh, 1
    shr bh, 1
    sub bl, bh
    sub bl, [bp+CT_T]
    test bl, 0x10
    jz  .h1
    sub bl, 6
.h1:
    and bl, 0x0F
    shl bl, 1
    shl bl, 1
    shl bl, 1
    shl bl, 1
    or  bl, [bp+CT_B]
    mov al, bl
    mov ah, [bp+CT_F]
    jmp c64_next

; =============================================================================
; INTERRUPTS (C64-SPEC §4.4)
; =============================================================================
; c64_delay_one - CLI or PLP has just UNMASKED the line, and a 6502 takes that
; IRQ only after ONE MORE INSTRUCTION (src/6510core.c:1052, mainc64cpu.c:702).
;
; IT IS DONE BY ENDING THE RUN AFTER THAT INSTRUCTION, not by a flag the
; dispatch has to test. c64_next is the hottest path in this machine - four
; instructions before the fetch - and one more compare there is paid by every
; opcode in every program for a case that arises when an IRQ is pending at a
; CLI. So the countdown is set to 1: the next instruction passes
; `cmp DEAD,0 / jle` and executes, its cost takes DEAD negative, the run
; expires, and the C's slice loop calls c64_run again - whose FIRST act is the
; interrupt check (4.4). The unspent budget moves into CARRY exactly as
; c64_cut does and c64_expire puts it back, so `ran = asked - cnt` is still
; exact and nothing is lost or double-counted.
;
; THE ONE CORNER, STATED: if the countdown is already 1 or less the run is
; ending anyway, and the IRQ is then taken at the top of the next run with no
; instruction in between - one instruction early, at a slice boundary the C
; chose. The alternative is the compare on the hot path.
c64_delay_one:
    test byte [C64_SCR_BASE + C64_SCR_IRQ], 0x01
    jz  c64_next                    ; no line asserted: nothing to delay
    test ch, 0x04
    jnz c64_next                    ; ...and I is set after all
    mov bx, [C64_SCR_BASE + C64_SCR_DEAD]
    cmp bx, 2
    jl  c64_next                    ; the run ends by itself (the corner above)
    dec bx                          ; leave exactly one cycle of budget
    add [C64_SCR_BASE + C64_SCR_CARRY], bx
    mov word [C64_SCR_BASE + C64_SCR_DEAD], 1
    jmp c64_next

; c64_irq_chk - taken at the top of a run and after the instructions that can
; change the I flag. An IRQ is due when the line is asserted and I is clear.
c64_irq_chk:
    test byte [C64_SCR_BASE + C64_SCR_IRQ], 0x01
    jz  c64_next
    test ch, 0x04
    jnz c64_next
c64_do_irq:
    mov bx, si
    PUSHB bh
    PUSHB bl
    call c64_getp
    and bl, 0xEF                    ; B is clear on a hardware interrupt
    PUSHB bl
    or  ch, 0x04                    ; I
    mov bx, 0xFFFE
    jmp short c64_vector

c64_do_nmi:
    and byte [C64_SCR_BASE + C64_SCR_IRQ], 0xFD   ; the EDGE is consumed
    mov bx, si
    PUSHB bh
    PUSHB bl
    call c64_getp
    and bl, 0xEF
    PUSHB bl
    or  ch, 0x04
    mov bx, 0xFFFA
; c64_vector - AN INTERRUPT'S seven cycles and the vector. BRK does NOT come
; in here: it is an OPCODE, so the dispatch has already charged its table cost
; of 7 (src/6510core.c:998 charges BRK once), and entering here charged seven
; more - 14 for a BRK, which is exactly the kind of defect a machine still
; boots with. It jumps to c64_vec_load instead.
c64_vector:
    sub word [C64_SCR_BASE + C64_SCR_DEAD], 7   ; a WORD subtract: the borrow
                                                ; is already in it
c64_vec_load:
    call c64_rd_bx
    mov [bp+CT_B], dh
    inc bx
    call c64_rd_bx
    mov bh, dh
    mov bl, [bp+CT_B]
    mov si, bx
    jmp c64_next

o_brk:
    inc si                          ; BRK is two bytes: the padding is skipped
    mov bx, si
    PUSHB bh
    PUSHB bl
    call c64_getp
    or  bl, 0x10                    ; B is SET on a software break
    PUSHB bl
    or  ch, 0x04
    mov bx, 0xFFFE
    jmp short c64_vec_load          ; ...and NOT c64_vector: BRK's seven
                                    ; cycles are the dispatch's table entry,
                                    ; charged once (6510core.c:998)

o_rti:
    POPB bl
    call c64_setp
    POPB bl
    mov [bp+CT_B], bl
    POPB bh
    mov bl, [bp+CT_B]
    mov si, bx
    jmp c64_irq_chk

o_rts:
    POPB bl
    mov [bp+CT_B], bl
    POPB bh
    mov bl, [bp+CT_B]
    inc bx
    mov si, bx
    jmp c64_next

o_jsr:
    call c64_fetch
    mov [bp+CT_B], dh
    mov bx, si                      ; PC is ON the high byte: the 6502 pushes
    PUSHB bh                        ; the return address MINUS ONE
    PUSHB bl
    call c64_fetch
    mov bh, dh
    mov bl, [bp+CT_B]
    mov si, bx
    jmp c64_next

; =============================================================================
; THE HANDLERS
; =============================================================================

; --- loads and stores --------------------------------------------------------
%macro M_LDA 1
o_lda_%1:
    OPR %1
    mov al, dh
    jmp c64_nz_al
%endmacro
M_LDA imm
M_LDA zp
M_LDA zpx
M_LDA abs
M_LDA absxp
M_LDA absyp
M_LDA indx
M_LDA indyp

%macro M_LDX 1
o_ldx_%1:
    OPR %1
    mov cl, dh
    jmp c64_nz_cl
%endmacro
M_LDX imm
M_LDX zp
M_LDX zpy
M_LDX abs
M_LDX absyp

%macro M_LDY 1
o_ldy_%1:
    OPR %1
    mov dl, dh
    jmp c64_nz_dl
%endmacro
M_LDY imm
M_LDY zp
M_LDY zpx
M_LDY abs
M_LDY absxp

%macro M_STA 1
o_sta_%1:
    call c64_ea_%1
    mov dh, al
    call c64_wr_bx
    jmp c64_next
%endmacro
M_STA zp
M_STA zpx
M_STA abs
M_STA absx
M_STA absy
M_STA indx
M_STA indy

%macro M_STX 1
o_stx_%1:
    call c64_ea_%1
    mov dh, cl
    call c64_wr_bx
    jmp c64_next
%endmacro
M_STX zp
M_STX zpy
M_STX abs

%macro M_STY 1
o_sty_%1:
    call c64_ea_%1
    mov dh, dl
    call c64_wr_bx
    jmp c64_next
%endmacro
M_STY zp
M_STY zpx
M_STY abs

; --- the logic group ---------------------------------------------------------
%macro M_ORA 1
o_ora_%1:
    OPR %1
    or  al, dh
    jmp c64_nz_al
%endmacro
M_ORA imm
M_ORA zp
M_ORA zpx
M_ORA abs
M_ORA absxp
M_ORA absyp
M_ORA indx
M_ORA indyp

%macro M_AND 1
o_and_%1:
    OPR %1
    and al, dh
    jmp c64_nz_al
%endmacro
M_AND imm
M_AND zp
M_AND zpx
M_AND abs
M_AND absxp
M_AND absyp
M_AND indx
M_AND indyp

%macro M_EOR 1
o_eor_%1:
    OPR %1
    xor al, dh
    jmp c64_nz_al
%endmacro
M_EOR imm
M_EOR zp
M_EOR zpx
M_EOR abs
M_EOR absxp
M_EOR absyp
M_EOR indx
M_EOR indyp

; --- arithmetic --------------------------------------------------------------
%macro M_ADC 1
o_adc_%1:
    OPR %1
    jmp c64_adc
%endmacro
M_ADC imm
M_ADC zp
M_ADC zpx
M_ADC abs
M_ADC absxp
M_ADC absyp
M_ADC indx
M_ADC indyp

%macro M_SBC 1
o_sbc_%1:
    OPR %1
    jmp c64_sbc
%endmacro
M_SBC imm
M_SBC zp
M_SBC zpx
M_SBC abs
M_SBC absxp
M_SBC absyp
M_SBC indx
M_SBC indyp

%macro M_CMP 1
o_cmp_%1:
    OPR %1
    cmp al, dh
    jmp c64_cmpfin
%endmacro
M_CMP imm
M_CMP zp
M_CMP zpx
M_CMP abs
M_CMP absxp
M_CMP absyp
M_CMP indx
M_CMP indyp

%macro M_CPX 1
o_cpx_%1:
    OPR %1
    cmp cl, dh
    jmp c64_cmpfin
%endmacro
M_CPX imm
M_CPX zp
M_CPX abs

%macro M_CPY 1
o_cpy_%1:
    OPR %1
    cmp dl, dh
    jmp c64_cmpfin
%endmacro
M_CPY imm
M_CPY zp
M_CPY abs

; --- BIT: Z from A & M, but N and V come STRAIGHT off the operand ------------
%macro M_BIT 1
o_bit_%1:
    OPR %1
    and ah, 0x01
    mov bl, dh
    and bl, 0x80
    or  ah, bl
    and ch, 0xBF
    test dh, 0x40
    jz  %%v0
    or  ch, 0x40
%%v0:
    mov bl, al
    and bl, dh
    jnz %%nz
    or  ah, 0x40
%%nz:
    jmp c64_next
%endmacro
M_BIT zp
M_BIT abs

; --- shifts and rotates, in memory ------------------------------------------
;
; EVERY MEMORY READ-MODIFY-WRITE WRITES TWICE, AND THE FIRST WRITE IS THE OLD
; BYTE. That is the 6502's dummy write and VICE models it in the macro itself
; - `dummy_func(tmp_addr, tmp_value)` between the load and the store
; (src/6510core.c:929 ASL, :1175 INC, :1100 DCP, and every other RMW macro
; beside them). It costs no cycle here: the table already charges the RMW
; modes their 5/6/7, exactly as VICE's CLK_ADD_DUMMY does.
;
; It is not decoration. `INC $D019` and `DEC $DC0D` are real instructions and
; the value the CHIP sees first is the old one, which acknowledges a VIC
; interrupt or reads a CIA ICR before the modified byte lands; and on RAM the
; dirty bit and the write window are raised by the first write too. c64_wr_bx
; preserves every register, DH included, so the pair is two calls and nothing
; else.
;
%macro M_ASL 1
o_asl_%1:
    call c64_ea_%1
    call c64_rd_bx
    call c64_wr_bx                  ; THE 6502'S DUMMY WRITE of the OLD byte
    shl dh, 1
    lahf
    and ah, 0xC1
    call c64_wr_bx
    jmp c64_next
%endmacro
M_ASL zp
M_ASL zpx
M_ASL abs
M_ASL absx

%macro M_LSR 1
o_lsr_%1:
    call c64_ea_%1
    call c64_rd_bx
    call c64_wr_bx                  ; THE 6502'S DUMMY WRITE of the OLD byte
    shr dh, 1
    lahf
    and ah, 0xC1
    call c64_wr_bx
    jmp c64_next
%endmacro
M_LSR zp
M_LSR zpx
M_LSR abs
M_LSR absx

%macro M_ROL 1
o_rol_%1:
    call c64_ea_%1
    call c64_rd_bx
    call c64_wr_bx                  ; THE 6502'S DUMMY WRITE of the OLD byte
    shr ah, 1
    rcl dh, 1
    mov ah, 0
    jnc %%nc
    mov ah, 1
%%nc:
    call c64_wr_bx
    jmp c64_rot_dh
%endmacro
M_ROL zp
M_ROL zpx
M_ROL abs
M_ROL absx

%macro M_ROR 1
o_ror_%1:
    call c64_ea_%1
    call c64_rd_bx
    call c64_wr_bx                  ; THE 6502'S DUMMY WRITE of the OLD byte
    shr ah, 1
    rcr dh, 1
    mov ah, 0
    jnc %%nc
    mov ah, 1
%%nc:
    call c64_wr_bx
    jmp c64_rot_dh
%endmacro
M_ROR zp
M_ROR zpx
M_ROR abs
M_ROR absx

o_asl_acc:
    shl al, 1
    lahf
    and ah, 0xC1
    jmp c64_next

o_lsr_acc:
    shr al, 1
    lahf
    and ah, 0xC1
    jmp c64_next

o_rol_acc:
    shr ah, 1
    rcl al, 1
    mov ah, 0
    jnc .nc
    mov ah, 1
.nc:
    or  al, al
    jz  .z
    js  .n
    jmp c64_next
.z:
    or  ah, 0x40
    jmp c64_next
.n:
    or  ah, 0x80
    jmp c64_next

o_ror_acc:
    shr ah, 1
    rcr al, 1
    mov ah, 0
    jnc .nc
    mov ah, 1
.nc:
    or  al, al
    jz  .z
    js  .n
    jmp c64_next
.z:
    or  ah, 0x40
    jmp c64_next
.n:
    or  ah, 0x80
    jmp c64_next

; --- increments and decrements ----------------------------------------------
%macro M_INC 1
o_inc_%1:
    call c64_ea_%1
    call c64_rd_bx
    call c64_wr_bx                  ; THE 6502'S DUMMY WRITE of the OLD byte
    and ah, 0x01
    inc dh
    call c64_wr_bx
    jmp c64_nz_dh
%endmacro
M_INC zp
M_INC zpx
M_INC abs
M_INC absx

%macro M_DEC 1
o_dec_%1:
    call c64_ea_%1
    call c64_rd_bx
    call c64_wr_bx                  ; THE 6502'S DUMMY WRITE of the OLD byte
    and ah, 0x01
    dec dh
    call c64_wr_bx
    jmp c64_nz_dh
%endmacro
M_DEC zp
M_DEC zpx
M_DEC abs
M_DEC absx

o_inx:
    inc cl
    jmp c64_nz_cl
o_dex:
    dec cl
    jmp c64_nz_cl
o_iny:
    inc dl
    jmp c64_nz_dl
o_dey:
    dec dl
    jmp c64_nz_dl

; --- transfers ---------------------------------------------------------------
o_tax:
    mov cl, al
    jmp c64_nz_cl
o_tay:
    mov dl, al
    jmp c64_nz_dl
o_txa:
    mov al, cl
    jmp c64_nz_al
o_tya:
    mov al, dl
    jmp c64_nz_al
o_tsx:
    mov bx, di                      ; DI is 0x0100+S; CL takes the low byte
    mov cl, bl
    jmp c64_nz_cl
o_txs:
    mov bl, cl                      ; TXS sets NO flags
    xor bh, bh
    or  bx, 0x0100
    mov di, bx
    jmp c64_next

; --- the stack ---------------------------------------------------------------
o_pha:
    PUSHB al
    jmp c64_next
o_php:
    call c64_getp
    or  bl, 0x10                    ; PHP always pushes B set
    PUSHB bl
    jmp c64_next
o_pla:
    POPB al
    jmp c64_nz_al
o_plp:
    mov dh, ch                      ; the OLD flags: DH is the scratch and
                                    ; neither POPB nor c64_setp touches it
    POPB bl
    call c64_setp
    test dh, 0x04
    jz  .was_clear
    test ch, 0x04                   ; I WAS set...
    jnz c64_next                    ; ...and still is
    jmp c64_delay_one               ; ...and is now clear: ENABLES
.was_clear:
    test ch, 0x04                   ; I WAS clear...
    jz  c64_irq_chk                 ; ...and still is: the ordinary check
    test byte [C64_SCR_BASE + C64_SCR_IRQ], 0x01
    jz  c64_next
    jmp c64_do_irq                  ; ...and is now set: DISABLES, so the line
                                    ; visible under the old I is still taken

; --- flags -------------------------------------------------------------------
o_clc:
    and ah, 0xFE
    jmp c64_next
o_sec:
    or  ah, 0x01
    jmp c64_next
; --- THE I FLAG'S ONE-INSTRUCTION TIMING (src/6510core.c:1052 CLI, :1493 PLP,
; :1760 SEI, and :1652 says RTI has NONE of it) ------------------------------
;
; VICE records the transition in the LAST OPCODE's info and the interrupt
; check reads it (`(!LOCAL_INTERRUPT() || OPINFO_DISABLES_IRQ(...))`,
; 6510core.c:445; mainc64cpu.c:702 refuses the IRQ after an ENABLES). Two
; halves, and this core had both wrong in opposite directions:
;
;   ENABLES (CLI, or a PLP that clears I): the IRQ is NOT taken at the end of
;   this instruction. One more instruction executes first - which is what lets
;   `CLI / SEI` run with an IRQ pending and take it after the SEI, and what
;   makes the KERNAL's own `CLI / RTS` sequences behave.
;
;   DISABLES (SEI, or a PLP that sets I): an IRQ that was visible under the
;   OLD I value is still taken at the end of this instruction. Taking I at
;   face value here HIDES it, and on a machine that never clears I again it is
;   hidden for good.
;
; c64_delay_one is how the first half is done without putting a test on the
; hottest path in the machine: see its own comment.
o_cli:
    and ch, 0xFB
    jmp c64_delay_one
o_sei:
    test ch, 0x04
    jnz .set                        ; I was already set: nothing transitions
    or  ch, 0x04
    test byte [C64_SCR_BASE + C64_SCR_IRQ], 0x01
    jz  c64_next
    jmp c64_do_irq                  ; DISABLES: the line was visible under the
                                    ; old I and is still taken (6510core.c:445)
.set:
    or  ch, 0x04
    jmp c64_next
o_cld:
    and ch, 0xF7
    jmp c64_next
o_sed:
    or  ch, 0x08
    jmp c64_next
o_clv:
    and ch, 0xBF
    jmp c64_next

; --- jumps and branches ------------------------------------------------------
o_jmp_abs:
    call c64_ea_abs
    mov si, bx
    jmp c64_next

; JMP (ind) keeps the NMOS page-wrap bug: only the LOW byte of the pointer
; increments, so JMP ($10FF) takes its high byte from $1000.
o_jmp_ind:
    call c64_ea_abs
    call c64_rd_bx
    mov [bp+CT_B], dh
    inc bl
    call c64_rd_bx
    mov bh, dh
    mov bl, [bp+CT_B]
    mov si, bx
    jmp c64_next

; A taken branch is one more cycle, and one more again when the target is on
; another page (C64-SPEC §4.2).
%macro M_BR 3                       ; label, register/flag test, jump-if-taken
o_%1:
    call c64_fetch
    %2
    %3 %%take
    jmp c64_next
%%take:
    dec word [C64_SCR_BASE + C64_SCR_DEAD]
    mov bl, dh
    xor bh, bh
    or  dh, dh
    jns %%pos
    dec bh
%%pos:
    mov [bp+CT_W], si
    add bx, si
    mov si, bx
    mov dh, [bp+CT_W+1]
    cmp dh, bh
    je  %%same
    dec word [C64_SCR_BASE + C64_SCR_DEAD]
%%same:
    jmp c64_next
%endmacro

M_BR bpl, {test ah, 0x80}, jz
M_BR bmi, {test ah, 0x80}, jnz
M_BR bvc, {test ch, 0x40}, jz
M_BR bvs, {test ch, 0x40}, jnz
M_BR bcc, {test ah, 0x01}, jz
M_BR bcs, {test ah, 0x01}, jnz
M_BR bne, {test ah, 0x40}, jz
M_BR beq, {test ah, 0x40}, jnz

; --- the NOPs, documented and not (6510core.c) ------------------------------
o_nop:
    jmp c64_next
o_nop_imm:
    call c64_fetch
    jmp c64_next
o_nop_zp:
    call c64_ea_zp
    jmp c64_next
o_nop_zpx:
    call c64_ea_zpx
    jmp c64_next
o_nop_abs:
    call c64_ea_abs
    jmp c64_next
o_nop_absxp:
    call c64_ea_absxp
    jmp c64_next

; =============================================================================
; THE ILLEGAL OPCODES (6510core.c)
;
; The stable ones are exact. The six unstable ones - ANE, LXA, SHA, SHX, SHY
; and SHS - are the "magic constant" behaviour VICE compiles ($EF for ANE,
; $EE for LXA, 6510core.c:847 and :1389), and the four stores carry VICE's
; page-cross address corruption too (:699-711). c64cputest.asm's
; illegal-opcode row is written against VICE and not against silicon nobody
; agrees on.
; =============================================================================
%macro M_SLO 1
o_slo_%1:
    call c64_ea_%1
    call c64_rd_bx
    call c64_wr_bx                  ; THE 6502'S DUMMY WRITE of the OLD byte
    shl dh, 1
    lahf
    and ah, 0xC1
    call c64_wr_bx
    or  al, dh
    jmp c64_nz_al
%endmacro
M_SLO zp
M_SLO zpx
M_SLO abs
M_SLO absx
M_SLO absy
M_SLO indx
M_SLO indy

%macro M_RLA 1
o_rla_%1:
    call c64_ea_%1
    call c64_rd_bx
    call c64_wr_bx                  ; THE 6502'S DUMMY WRITE of the OLD byte
    shr ah, 1
    rcl dh, 1
    mov ah, 0
    jnc %%nc
    mov ah, 1
%%nc:
    call c64_wr_bx
    and al, dh
    jmp c64_nz_al
%endmacro
M_RLA zp
M_RLA zpx
M_RLA abs
M_RLA absx
M_RLA absy
M_RLA indx
M_RLA indy

%macro M_SRE 1
o_sre_%1:
    call c64_ea_%1
    call c64_rd_bx
    call c64_wr_bx                  ; THE 6502'S DUMMY WRITE of the OLD byte
    shr dh, 1
    lahf
    and ah, 0xC1
    call c64_wr_bx
    xor al, dh
    jmp c64_nz_al
%endmacro
M_SRE zp
M_SRE zpx
M_SRE abs
M_SRE absx
M_SRE absy
M_SRE indx
M_SRE indy

%macro M_RRA 1
o_rra_%1:
    call c64_ea_%1
    call c64_rd_bx
    call c64_wr_bx                  ; THE 6502'S DUMMY WRITE of the OLD byte
    shr ah, 1
    rcr dh, 1
    mov ah, 0
    jnc %%nc
    mov ah, 1
%%nc:
    call c64_wr_bx
    jmp c64_adc
%endmacro
M_RRA zp
M_RRA zpx
M_RRA abs
M_RRA absx
M_RRA absy
M_RRA indx
M_RRA indy

%macro M_DCP 1
o_dcp_%1:
    call c64_ea_%1
    call c64_rd_bx
    call c64_wr_bx                  ; THE 6502'S DUMMY WRITE of the OLD byte
    dec dh
    call c64_wr_bx
    cmp al, dh
    jmp c64_cmpfin
%endmacro
M_DCP zp
M_DCP zpx
M_DCP abs
M_DCP absx
M_DCP absy
M_DCP indx
M_DCP indy

%macro M_ISB 1
o_isb_%1:
    call c64_ea_%1
    call c64_rd_bx
    call c64_wr_bx                  ; THE 6502'S DUMMY WRITE of the OLD byte
    inc dh
    call c64_wr_bx
    jmp c64_sbc
%endmacro
M_ISB zp
M_ISB zpx
M_ISB abs
M_ISB absx
M_ISB absy
M_ISB indx
M_ISB indy

%macro M_SAX 1
o_sax_%1:
    call c64_ea_%1
    mov dh, al
    and dh, cl
    call c64_wr_bx
    jmp c64_next
%endmacro
M_SAX zp
M_SAX zpy
M_SAX abs
M_SAX indx

%macro M_LAX 1
o_lax_%1:
    OPR %1
    mov al, dh
    mov cl, dh
    jmp c64_nz_al
%endmacro
M_LAX zp
M_LAX zpy
M_LAX abs
M_LAX absyp
M_LAX indx
M_LAX indyp

o_anc:                              ; A &= imm; C is the sign of the result
    call c64_fetch
    and al, dh
    and ah, 0x01
    or  al, al
    jz  .z
    jns .p
    or  ah, 0x81                    ; N, and C follows N
    jmp c64_next
.p:
    and ah, 0xFE
    jmp c64_next
.z:
    mov ah, 0x40
    jmp c64_next

o_alr:                              ; A &= imm; LSR A
    call c64_fetch
    and al, dh
    shr al, 1
    lahf
    and ah, 0xC1
    jmp c64_next

; ARR - "the fanciest opcode ever" (6510core.c:895). A &= imm, then ROR A,
; and then the flags are nothing like a ROR's: C is bit 6 of the result and V
; is bit 6 exclusive-or bit 5.
;
; AND IT HAS A DECIMAL MODE, which this core used to discard. VICE's decimal
; branch (:900-:916) computes the shifted value, takes N from the INCOMING
; carry, Z from the result and V from `(tmp_2 ^ tmp) & 0x40` - all BEFORE the
; BCD fixups - then adds 6 to the low nibble when `(tmp & 0xf) + (tmp & 0x1)`
; passes 5, adds $60 to the high nibble when `(tmp & 0xf0) + (tmp & 0x10)`
; passes $50, and takes the carry from THAT test and nothing else. The high
; test sums to $100 at tmp = $Fx, which is why it is done with the 8086's own
; carry out and not with a compare alone.
o_arr:
    call c64_fetch
    and al, dh                      ; tmp = A & value
    test ch, 0x08                   ; D?
    jnz o_arr_dec
    shr ah, 1
    rcr al, 1
    mov ah, 0
    test al, 0x40
    jz  .c0
    or  ah, 0x01
.c0:
    and ch, 0xBF
    mov dh, al
    shr dh, 1
    xor dh, al
    test dh, 0x20
    jz  .v0
    or  ch, 0x40
.v0:
    or  al, al
    jz  .z
    js  .n
    jmp c64_next
.z:
    or  ah, 0x40
    jmp c64_next
.n:
    or  ah, 0x80
    jmp c64_next

; the decimal branch, transcribed (6510core.c:900-916)
o_arr_dec:
    mov bl, al                      ; BL = tmp
    mov bh, bl
    shr bh, 1
    test ah, 0x01                   ; the INCOMING carry, into bit 7
    jz  .noc
    or  bh, 0x80
.noc:                               ; BH = tmp_2
    mov dh, ah                      ; the old flag byte, for the carry test
    xor ah, ah
    test dh, 0x01
    jz  .n0
    or  ah, 0x80                    ; N = the incoming carry
.n0:
    or  bh, bh
    jnz .nz
    or  ah, 0x40                    ; Z = !tmp_2
.nz:
    and ch, 0xBF
    mov dh, bh
    xor dh, bl
    test dh, 0x40
    jz  .v0
    or  ch, 0x40                    ; V = (tmp_2 ^ tmp) & 0x40
.v0:
    mov dh, bl                      ; ((tmp & 0xf) + (tmp & 0x1)) > 5 ?
    and dh, 0x0F
    mov al, bl
    and al, 0x01
    add dh, al
    cmp dh, 6
    jb  .lo
    mov dh, bh                      ; ...then +6 into the low nibble
    add dh, 6
    and dh, 0x0F
    and bh, 0xF0
    or  bh, dh
.lo:
    mov dh, bl                      ; ((tmp & 0xf0) + (tmp & 0x10)) > 0x50 ?
    and dh, 0xF0
    mov al, bl
    and al, 0x10
    add dh, al                      ; $F0 + $10 is $100: the CARRY is the
    jc  .hi                         ; ninth bit and a compare alone loses it
    cmp dh, 0x51
    jb  .done                       ; C stays 0 - the else branch's only act
.hi:
    mov dh, bh                      ; ...then +$60 into the high nibble
    add dh, 0x60
    and dh, 0xF0
    and bh, 0x0F
    or  bh, dh
    or  ah, 0x01                    ; ...and THAT is where the carry comes from
.done:
    mov al, bh
    jmp c64_next

o_sbx:                              ; X = (A & X) - imm, with a CMP's carry
    call c64_fetch
    mov bl, al
    and bl, cl
    sub bl, dh
    mov cl, bl
    lahf
    and ah, 0xC1
    xor ah, 0x01
    jmp c64_next

o_ane:                              ; the magic constant is VICE's ANE_MAGIC,
                                    ; which is $EF (6510core.c:847). $EE is
                                    ; ANE_RDY_MAGIC (:848), the value on a
                                    ; cycle the RDY line is pulled, and LXA's
                                    ; magic below - "needs to be 0xee for
                                    ; wizball" (:1389). Using $EE here made
                                    ; ANE's bit 0 an AND of A instead of a 1
    call c64_fetch
    or  al, 0xEF
    and al, cl
    and al, dh
    jmp c64_nz_al

o_lxa:                              ; ...and LXA's IS $EE (LXA_MAGIC, :1389)
    call c64_fetch
    or  al, 0xEE
    and al, dh
    mov cl, al
    jmp c64_nz_al

o_las:                              ; A = X = S = M & S
    call c64_ea_absyp
    call c64_rd_bx
    mov bx, di
    and dh, bl
    mov al, dh
    mov cl, dh
    mov bl, dh
    xor bh, bh
    or  bx, 0x0100
    mov di, bx
    jmp c64_nz_al

; THE FOUR UNSTABLE STORES, AND BOTH HALVES OF WHAT MAKES THEM UNSTABLE.
;
; The mask is the UNINDEXED base's high byte plus one - `(tmp >> 8) + 1` where
; `tmp` is the address BEFORE the index is added (6510core.c:1769 SHA_ABS_Y,
; :1797 SHX_ABS_Y, :1806 SHY_ABS_X, :1815 SHS_ABS_Y, and :1778 SHA_IND_Y where
; `tmp` is the zero-page pointer). This core used the INDEXED high byte, which
; is the same number only while the index does not carry.
;
; And when it DOES carry, the address itself is corrupted: STORE_ABS_SH_Y
; (:699-711) writes `(tmp2 & 0xff) | (value << 8)` - the high byte of the
; target becomes the VALUE. That is the half this core called "the address
; corruption a real 6510 shows", said it was not carrying, and did not need
; to skip: VICE compiles it and it is four instructions here.
;
; The three EA helpers below are the ordinary indexed ones plus the two facts
; only these opcodes want: the unindexed high byte in CT_B, and whether the
; index carried in CT_T.
c64_ea_absy_sh:
    call c64_ea_abs
    mov [bp+CT_B], bh
    mov byte [bp+CT_T], 0
    add bl, dl
    jnc c64_ea_sh_ret
    inc bh
    mov byte [bp+CT_T], 1
c64_ea_sh_ret:
    ret

c64_ea_absx_sh:
    call c64_ea_abs
    mov [bp+CT_B], bh
    mov byte [bp+CT_T], 0
    add bl, cl
    jnc c64_ea_sh_ret
    inc bh
    mov byte [bp+CT_T], 1
    ret

c64_ea_indy_sh:
    call c64_ea_indp                ; the POINTER is the unindexed address
    mov [bp+CT_B], bh
    mov byte [bp+CT_T], 0
    add bl, dl
    jnc c64_ea_sh_ret
    inc bh
    mov byte [bp+CT_T], 1
    ret

o_sha_indy:
    call c64_ea_indy_sh
    mov dh, al
    and dh, cl
    jmp short c64_sh_and
o_sha_absy:
    call c64_ea_absy_sh
    mov dh, al
    and dh, cl
    jmp short c64_sh_and
o_shx:
    call c64_ea_absy_sh
    mov dh, cl
    jmp short c64_sh_and
o_shy:
    call c64_ea_absx_sh
    mov dh, dl
    jmp short c64_sh_and
o_shs:                              ; SHS/TAS: S = A & X, and then SHA
    call c64_ea_absy_sh
    mov dh, al
    and dh, cl
    push bx
    mov bl, dh
    xor bh, bh
    or  bx, 0x0100
    mov di, bx
    pop bx

c64_sh_and:                         ; DH = the register value, BX = the
    push bx                         ; INDEXED address, CT_B = the unindexed
    mov bl, [bp+CT_B]               ; high byte, CT_T = the index carried
    inc bl
    and dh, bl                      ; value = reg & ((base >> 8) + 1)
    pop bx
    cmp byte [bp+CT_T], 0
    je  .store
    mov bh, dh                      ; ...and on a page cross the target's HIGH
                                    ; byte becomes the value (:707-709)
.store:
    call c64_wr_bx
    jmp c64_next

; =============================================================================
; THE C-CALLABLE ENTRIES BESIDE _c64_run
; =============================================================================

; int c64_bread(unsigned a) - one BANKED byte, the way the 6510 would see it
; through the current map. The reset vector lives under the KERNAL, so the C
; cannot read it with c64_rd (which is RAM by construction, c64mem.inc), and
; the harness needs the same door.
_c64_bread:
    push bp
    mov bp, sp
    push si
    push di
    push ds
    push es
    push cx
    push dx
    mov bx, [bp+4]
    mov ds, [cs:_c64_m+CM_RAMSEG]
    call c64_rd_bx
    mov al, dh
    pop dx
    pop cx
    mov ah, 0
    pop es
    pop ds
    pop di
    pop si
    pop bp
    ret

; void c64_cut(void) - end the run that is in progress at once, WITHOUT
; disturbing the cycle accounting: the unspent countdown moves into CARRY and
; c64_expire puts it back, so `ran = asked - cnt` is still exact. c64io.c calls
; it when an I/O write raises a line, which is the only way an interrupt can
; become due in the middle of a run.
_c64_cut:
    push ds
    push bx
    mov bx, [cs:_c64_m+CM_RAMSEG]
    mov ds, bx
    mov bx, [C64_SCR_BASE + C64_SCR_DEAD]
    inc bx
    add [C64_SCR_BASE + C64_SCR_CARRY], bx
    mov word [C64_SCR_BASE + C64_SCR_DEAD], -1
    pop bx
    pop ds
    ret

; void c64_rebank(void) - the map changed under the core's feet ($00/$01, or a
; reset). Emptying the fetch region forces the next fetch through c64_rebias,
; so no instruction is ever fetched under a stale bias (C64-SPEC §4.3).
_c64_rebank:
    push ds
    push ax
    mov ax, [cs:_c64_m+CM_RAMSEG]
    mov ds, ax
    mov word [C64_SCR_BASE + C64_SCR_BOUND], 0
    mov word [C64_SCR_BASE + C64_SCR_BLO], 0
    pop ax
    pop ds
    ret

; =============================================================================
; THE TABLES
; =============================================================================

; the dirty bitmap's bit for a page - read-only and cs:-resident, which is not
; the TCG hazard of LESSONS.md 13 (only WRITES to a translated page are)
c64_dmask:
%assign c64_i 0
%rep 256
    db 0x80 >> (c64_i & 7)
%assign c64_i c64_i+1
%endrep

; --- the per-opcode cycle table (C64-SPEC §4.2, src/6510core.c) --------
; The base cost. The page-cross penalty on the indexed READ modes, the
; taken-branch penalty and the branch-page-cross penalty are charged by
; c64_ea_*p and M_BR, because they are conditions and not costs.
c64_cyc:
    db 7,6,2,8,3,3,5,5, 3,2,2,2,4,4,6,6      ; 00
    db 2,5,2,8,4,4,6,6, 2,4,2,7,4,4,7,7      ; 10
    db 6,6,2,8,3,3,5,5, 4,2,2,2,4,4,6,6      ; 20
    db 2,5,2,8,4,4,6,6, 2,4,2,7,4,4,7,7      ; 30
    db 6,6,2,8,3,3,5,5, 3,2,2,2,3,4,6,6      ; 40
    db 2,5,2,8,4,4,6,6, 2,4,2,7,4,4,7,7      ; 50
    db 6,6,2,8,3,3,5,5, 4,2,2,2,5,4,6,6      ; 60
    db 2,5,2,8,4,4,6,6, 2,4,2,7,4,4,7,7      ; 70
    db 2,6,2,6,3,3,3,3, 2,2,2,2,4,4,4,4      ; 80
    db 2,6,2,6,4,4,4,4, 2,5,2,5,5,5,5,5      ; 90
    db 2,6,2,6,3,3,3,3, 2,2,2,2,4,4,4,4      ; A0
    db 2,5,2,5,4,4,4,4, 2,4,2,4,4,4,4,4      ; B0
    db 2,6,2,8,3,3,5,5, 2,2,2,2,4,4,6,6      ; C0
    db 2,5,2,8,4,4,6,6, 2,4,2,7,4,4,7,7      ; D0
    db 2,6,2,8,3,3,5,5, 2,2,2,2,4,4,6,6      ; E0
    db 2,5,2,8,4,4,6,6, 2,4,2,7,4,4,7,7      ; F0

; --- the 256-entry dispatch table (C64-SPEC §4.1) ---------------------
c64_tab:
    dw o_brk,      o_ora_indx,  o_jam,       o_slo_indx
    dw o_nop_zp,   o_ora_zp,    o_asl_zp,    o_slo_zp
    dw o_php,      o_ora_imm,   o_asl_acc,   o_anc
    dw o_nop_abs,  o_ora_abs,   o_asl_abs,   o_slo_abs
    dw o_bpl,      o_ora_indyp, o_jam,       o_slo_indy
    dw o_nop_zpx,  o_ora_zpx,   o_asl_zpx,   o_slo_zpx
    dw o_clc,      o_ora_absyp, o_nop,       o_slo_absy
    dw o_nop_absxp,o_ora_absxp, o_asl_absx,  o_slo_absx
    dw o_jsr,      o_and_indx,  o_jam,       o_rla_indx
    dw o_bit_zp,   o_and_zp,    o_rol_zp,    o_rla_zp
    dw o_plp,      o_and_imm,   o_rol_acc,   o_anc
    dw o_bit_abs,  o_and_abs,   o_rol_abs,   o_rla_abs
    dw o_bmi,      o_and_indyp, o_jam,       o_rla_indy
    dw o_nop_zpx,  o_and_zpx,   o_rol_zpx,   o_rla_zpx
    dw o_sec,      o_and_absyp, o_nop,       o_rla_absy
    dw o_nop_absxp,o_and_absxp, o_rol_absx,  o_rla_absx
    dw o_rti,      o_eor_indx,  o_jam,       o_sre_indx
    dw o_nop_zp,   o_eor_zp,    o_lsr_zp,    o_sre_zp
    dw o_pha,      o_eor_imm,   o_lsr_acc,   o_alr
    dw o_jmp_abs,  o_eor_abs,   o_lsr_abs,   o_sre_abs
    dw o_bvc,      o_eor_indyp, o_jam,       o_sre_indy
    dw o_nop_zpx,  o_eor_zpx,   o_lsr_zpx,   o_sre_zpx
    dw o_cli,      o_eor_absyp, o_nop,       o_sre_absy
    dw o_nop_absxp,o_eor_absxp, o_lsr_absx,  o_sre_absx
    dw o_rts,      o_adc_indx,  o_jam,       o_rra_indx
    dw o_nop_zp,   o_adc_zp,    o_ror_zp,    o_rra_zp
    dw o_pla,      o_adc_imm,   o_ror_acc,   o_arr
    dw o_jmp_ind,  o_adc_abs,   o_ror_abs,   o_rra_abs
    dw o_bvs,      o_adc_indyp, o_jam,       o_rra_indy
    dw o_nop_zpx,  o_adc_zpx,   o_ror_zpx,   o_rra_zpx
    dw o_sei,      o_adc_absyp, o_nop,       o_rra_absy
    dw o_nop_absxp,o_adc_absxp, o_ror_absx,  o_rra_absx
    dw o_nop_imm,  o_sta_indx,  o_nop_imm,   o_sax_indx
    dw o_sty_zp,   o_sta_zp,    o_stx_zp,    o_sax_zp
    dw o_dey,      o_nop_imm,   o_txa,       o_ane
    dw o_sty_abs,  o_sta_abs,   o_stx_abs,   o_sax_abs
    dw o_bcc,      o_sta_indy,  o_jam,       o_sha_indy
    dw o_sty_zpx,  o_sta_zpx,   o_stx_zpy,   o_sax_zpy
    dw o_tya,      o_sta_absy,  o_txs,       o_shs
    dw o_shy,      o_sta_absx,  o_shx,       o_sha_absy
    dw o_ldy_imm,  o_lda_indx,  o_ldx_imm,   o_lax_indx
    dw o_ldy_zp,   o_lda_zp,    o_ldx_zp,    o_lax_zp
    dw o_tay,      o_lda_imm,   o_tax,       o_lxa
    dw o_ldy_abs,  o_lda_abs,   o_ldx_abs,   o_lax_abs
    dw o_bcs,      o_lda_indyp, o_jam,       o_lax_indyp
    dw o_ldy_zpx,  o_lda_zpx,   o_ldx_zpy,   o_lax_zpy
    dw o_clv,      o_lda_absyp, o_tsx,       o_las
    dw o_ldy_absxp,o_lda_absxp, o_ldx_absyp, o_lax_absyp
    dw o_cpy_imm,  o_cmp_indx,  o_nop_imm,   o_dcp_indx
    dw o_cpy_zp,   o_cmp_zp,    o_dec_zp,    o_dcp_zp
    dw o_iny,      o_cmp_imm,   o_dex,       o_sbx
    dw o_cpy_abs,  o_cmp_abs,   o_dec_abs,   o_dcp_abs
    dw o_bne,      o_cmp_indyp, o_jam,       o_dcp_indy
    dw o_nop_zpx,  o_cmp_zpx,   o_dec_zpx,   o_dcp_zpx
    dw o_cld,      o_cmp_absyp, o_nop,       o_dcp_absy
    dw o_nop_absxp,o_cmp_absxp, o_dec_absx,  o_dcp_absx
    dw o_cpx_imm,  o_sbc_indx,  o_nop_imm,   o_isb_indx
    dw o_cpx_zp,   o_sbc_zp,    o_inc_zp,    o_isb_zp
    dw o_inx,      o_sbc_imm,   o_nop,       o_sbc_imm
    dw o_cpx_abs,  o_sbc_abs,   o_inc_abs,   o_isb_abs
    dw o_beq,      o_sbc_indyp, o_jam,       o_isb_indy
    dw o_nop_zpx,  o_sbc_zpx,   o_inc_zpx,   o_isb_zpx
    dw o_sed,      o_sbc_absyp, o_nop,       o_isb_absy
    dw o_nop_absxp,o_sbc_absxp, o_inc_absx,  o_isb_absx
