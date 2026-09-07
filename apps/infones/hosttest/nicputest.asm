; =============================================================================
; os8088 - apps/infones/hosttest/nicputest.asm       THE 2A03 CORE'S GATE
;
; A BOOT SECTOR THAT RUNS THE SHIPPING 2A03 CORE ON A REAL x86, with nothing
; else running - `make nicputest`, MINUTES, and deliberately NOT in
; apps/infones/build.sh, exactly as c64cputest is not in C64's and rcz80test
; is not in RUNCPM's. apps/infones/nicpu.inc is %included, not copied, so what
; runs is the shipping text, under the one condition that makes this OS
; different from every other target it could be tested on: **SS != DS**.
;
; Here CS = DS = 0 stand in for the package's segment (nicpu.inc reaches its
; register file through CS and a package has CS = DS), SS = 0x1000 is the task
; stack, ES = 0x3000 stands in for KERNEL_SEG, 0x4000 is the machine claim and
; the ROM is read whole to 0x7000.
;
; -----------------------------------------------------------------------------
; THE STUB BUS'S CONTRACT, WRITTEN DOWN (SPEC.md 91.14.4)
; -----------------------------------------------------------------------------
; The core answers $0000-$1FFF, $6000-$7FFF and every PRG READ itself; what
; reaches the two entry points below is $2000-$5FFF and every write at $8000
; and above. This harness's answers, and each is a decision:
;
;   $2000-$3FFF   mirrored every 8. $2002 answers the VBLANK FLAG in bit 7 and
;                 CLEARS IT ON READ, and the flag is RAISED PERIODICALLY -
;                 every 29,780 cycles, which is one NTSC frame - because
;                 blargg's shell polls it repeatedly during its own start-up
;                 and a $2002 that is always 0 hangs before the test begins.
;                 A $2000 write with bit 7 set arms an NMI, which the periodic
;                 vblank then latches, for the same reason. Every other PPU
;                 register accepts a write and answers 0.
;   $4015         0. $4016/$4017 answer 0x40, which is a connected controller
;                 with nothing pressed.
;   $4000-$401F   otherwise a write-only sink.
;   $4020-$5FFF   0.
;   $8000+ writes accepted and answered "the mapping did not move", which is
;                 mapper 0's own answer: every ROM this gate runs is NROM.
;
; -----------------------------------------------------------------------------
; TWO MODES
; -----------------------------------------------------------------------------
;   -DNESTEST -DTRACEN=<n>
;       nestest.nes from $C000 in automation mode, ONE INSTRUCTION AT A TIME,
;       emitting a 12-byte record per instruction over the serial port: PC,
;       the opcode byte, A, X, Y, P, S and the running cycle count. The record
;       is emitted BEFORE the instruction runs, which is what nestest.log's
;       lines describe. tools/nitrace.py diffs the stream against the log's
;       8,991 lines on the host, and THE FIRST DIFFERING LINE NAMES THE EXACT
;       INSTRUCTION THAT IS WRONG - which no other test ROM does for you.
;
;       Running with a budget of ONE cycle is what makes the trace exact: the
;       countdown is checked BETWEEN instructions, so a budget of 1 executes
;       exactly one instruction and leaves `1 - cnt` = its true cost.
;
;   (default)
;       one blargg single through the $6000 protocol: run until $6000 leaves
;       $80, check the $DE $B0 $61 signature at $6001-$6003, print the result
;       byte and the NUL-terminated ASCII at $6004. $81 means the test wants a
;       RESET, which is performed after ~100 ms of emulated time and the run
;       continues.
;
; RUN IT:  apps/infones/hosttest/nicputest.sh   (or `make nicputest`)
; =============================================================================

cpu 8086
bits 16
org 0x7C00

SEG_STACK   equ 0x1000
SEG_KERNEL  equ 0x3000              ; "KERNEL_SEG": it must come back
SEG_MACH    equ 0x4000              ; the machine claim (13KB)
SEG_ROM     equ 0x7000              ; the whole .nes file, read here
IMG_SECTORS equ 64                  ; 32KB of harness; the ROM follows
ROMSEC      equ 64

%ifndef ROMSECS
%define ROMSECS 48
%endif
%ifndef TRACEN
%define TRACEN 8991
%endif

; one NTSC frame of CPU cycles - the period the stub raises vblank on
VBL_PERIOD  equ 29780
; ...and ~100 ms of emulated time, which is what the $81 protocol asks for
RESET_DELAY equ 179000

section .text
section .rodata follows=.text
section .data   follows=.rodata
section .bss    follows=.data nobits
section .text

; -----------------------------------------------------------------------------
; STAGE 1 - the boot sector reads the harness AND the ROM in
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
    jae .rom
    call rdsec
    add bx, 512
    inc word [lba]
    jmp .rd
.rom:
    mov ax, SEG_ROM                 ; ...and the ROM, straight to 0x7000:0
    mov es, ax
    xor bx, bx
    mov word [lba], ROMSEC
.rd2:
    mov ax, [lba]
    cmp ax, ROMSEC + ROMSECS
    jae .go
    call rdsec
    add bx, 512
    inc word [lba]
    jmp .rd2
.go:
    xor ax, ax
    mov es, ax
    jmp 0x0000:body
lba: dw 0

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
    jmp halt

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
    mov es, ax                      ; ES = "KERNEL_SEG"
    sti
    cld

    call setup

%ifdef NESTEST
    jmp do_trace
%else
    jmp do_blargg
%endif

; -----------------------------------------------------------------------------
; setup - zero the machine claim, read the iNES header, map the PRG windows.
;
; THE HEADER IS PARSED RATHER THAN ASSUMED, because it is what says how many
; 16KB banks there are and a 16KB NROM mirrors itself into $C000 where a 32KB
; one does not - and the two ROM families this gate runs are exactly those.
; -----------------------------------------------------------------------------
setup:
    ; --- THE NEGATIVE CONTROLS, and they reach the core's TABLE AT RUNTIME
    ; and never its source: what is assembled here is byte for byte what
    ; ships (c64cputest's rule). A check that cannot fail is not a check.
%ifdef NEGADC
    mov ax, [ni_tab + 0x09 * 2]     ; ORA #imm's handler...
    mov [ni_tab + 0x69 * 2], ax     ; ...dispatched from ADC #imm
%endif
%ifdef NEGLDA
    mov ax, [ni_tab + 0xEA * 2]     ; NOP's handler...
    mov [ni_tab + 0xA9 * 2], ax     ; ...dispatched from LDA #imm
%endif

    ; --- the machine claim, zeroed ---------------------------------------
    push es
    mov ax, SEG_MACH
    mov es, ax
    xor di, di
    mov cx, 13 * 1024
    xor al, al
.z: mov [es:di], al
    inc di
    loop .z
    pop es
    mov word [_ni_m + NIM_MACHSEG], SEG_MACH

    ; --- the PRG windows -------------------------------------------------
    ; The PRG image starts 16 bytes into the file, which is exactly ONE
    ; paragraph, so its segment base is SEG_ROM + 1 and no shifting is needed
    ; here (the PACKAGE has to shift, because its staging claim is read off a
    ; floppy - SPEC.md 91.13.2).
    push ds
    mov ax, SEG_ROM
    mov ds, ax
    mov al, [4]                     ; byRomSize: 16KB banks
    pop ds
    mov [prg16], al

    mov ax, SEG_ROM + 1
    mov bx, ax                      ; window 0: $8000-$9FFF
    call win0
    add ax, 0x200
    mov bx, ax                      ; window 1: $A000-$BFFF
    call win1
    cmp byte [prg16], 1
    jne .big
    mov ax, SEG_ROM + 1             ; a 16KB ROM MIRRORS into $C000-$FFFF
    mov bx, ax
    call win2
    add ax, 0x200
    mov bx, ax
    call win3
    ret
.big:
    add ax, 0x200
    mov bx, ax                      ; window 2: $C000-$DFFF
    call win2
    add ax, 0x200
    mov bx, ax                      ; window 3: $E000-$FFFF
    call win3
    ret

; the four window setters, each writing one word of the core's scratch at the
; stride nicpu.inc reads it at (NI_S_PRGSTR = 32)
win0:
    push es
    push ax
    mov ax, SEG_MACH
    mov es, ax
    pop ax
    mov [es:NI_SCR + NI_S_PRG + 0 * NI_S_PRGSTR], bx
    pop es
    ret
win1:
    push es
    push ax
    mov ax, SEG_MACH
    mov es, ax
    pop ax
    mov [es:NI_SCR + NI_S_PRG + 1 * NI_S_PRGSTR], bx
    pop es
    ret
win2:
    push es
    push ax
    mov ax, SEG_MACH
    mov es, ax
    pop ax
    mov [es:NI_SCR + NI_S_PRG + 2 * NI_S_PRGSTR], bx
    pop es
    ret
win3:
    push es
    push ax
    mov ax, SEG_MACH
    mov es, ax
    pop ax
    mov [es:NI_SCR + NI_S_PRG + 3 * NI_S_PRGSTR], bx
    pop es
    ret

; -----------------------------------------------------------------------------
; MODE 1 - the nestest trace
; -----------------------------------------------------------------------------
%ifdef NESTEST
do_trace:
    ; nestest's automation entry: PC = $C000, and the state its own log's
    ; first line records (other/nestest.txt)
    mov word [_ni_m + NIM_PC], 0xC000
    mov word [_ni_m + NIM_A], 0
    mov word [_ni_m + NIM_X], 0
    mov word [_ni_m + NIM_Y], 0
    mov word [_ni_m + NIM_S], 0xFD
    mov word [_ni_m + NIM_P], 0x24
    mov word [cyc_lo], 7            ; ...and its CYC column starts at 7
    mov word [cyc_hi], 0

    mov si, msg_trace
    call puts

    mov word [icount], TRACEN
.loop:
    ; --- the record, BEFORE the instruction runs -------------------------
    mov ax, [_ni_m + NIM_PC]
    call putw
    push word [_ni_m + NIM_PC]
    call _ni_bread                  ; the opcode byte, through the banked read
    add sp, 2
    call putc_raw
    mov al, [_ni_m + NIM_A]
    call putc_raw
    mov al, [_ni_m + NIM_X]
    call putc_raw
    mov al, [_ni_m + NIM_Y]
    call putc_raw
    mov al, [_ni_m + NIM_P]
    call putc_raw
    mov al, [_ni_m + NIM_S]
    call putc_raw
    mov ax, [cyc_lo]
    call putw
    mov ax, [cyc_hi]
    call putw

    ; --- one instruction --------------------------------------------------
    mov ax, 1
    push ax
    call _ni_run
    add sp, 2
    mov ax, 1
    sub ax, [_ni_m + NIM_CNT]       ; what it actually cost
    add [cyc_lo], ax
    adc word [cyc_hi], 0

    dec word [icount]
    jnz .loop

    mov si, msg_tend
    call puts
    jmp halt
%endif

; -----------------------------------------------------------------------------
; MODE 2 - one blargg single through the $6000 protocol
; -----------------------------------------------------------------------------
%ifndef NESTEST
do_blargg:
    call reset_cpu
    mov si, msg_blargg
    call puts

    ; THE RUNNER IS A BOUNDED STATE MACHINE, and phase 1 is the one a first
    ; draft leaves out: at power-on $6000 holds 0, which is neither $80 nor
    ; $81, so a loop that only waits for "$6000 leaves $80" decides the test
    ; has finished with a result of 0 BEFORE IT HAS STARTED - and then reports
    ; a missing signature, which reads as a broken emulator.
    ;
    ;   phase 1  STARTUP: run until the $DE $B0 $61 signature is at
    ;            $6001-$6003 AND $6000 is $80
    ;   phase 2  RUNNING: run until $6000 leaves $80. $81 means the test wants
    ;            a RESET, delayed at least 100 ms
    ;   then     done ( < $80 is the result code) or TIMEOUT, printed
    mov word [caph], 12000          ; the cap: 12,000 x 20,000 cycles is 240
                                    ; million, about two emulated minutes
.start:
    call slice
    call sigok
    jz  .st2
    dec word [caph]
    jnz .start
    mov si, msg_nostart
    call puts
    jmp halt
.st2:
    call read6000
    cmp al, 0x80
    je  .run
    dec word [caph]
    jnz .start
    mov si, msg_nostart
    call puts
    jmp halt

.run:
    call slice
    call read6000
    cmp al, 0x80
    je  .more
    cmp al, 0x81
    jne .done
    inc word [rstwait]              ; the test wants a reset, at least 100 ms
    cmp word [rstwait], 9           ; from now: 9 x 20,000 cycles is ~100 ms
    jb  .more
    mov word [rstwait], 0
    call reset_cpu
.more:
    dec word [caph]
    jnz .run
    mov si, msg_tmo
    call puts
    jmp halt

.done:
    mov [result], al
    call sigok
    jnz .nosig
    mov si, msg_res
    call puts
    mov al, [result]
    xor ah, ah
    call putnum
    mov si, msg_txt
    call puts
    mov bx, 4
.txt:
    call read6000x
    or al, al
    jz .txtend
    call putc
    inc bx
    cmp bx, 300
    jb .txt
.txtend:
    mov si, msg_nl
    call puts
    cmp byte [result], 0
    jne .fail
    mov si, msg_pass
    call puts
    jmp halt
.fail:
    mov si, msg_fail
    call puts
    jmp halt
.nosig:
    mov si, msg_nosig
    call puts
    jmp halt

; slice - one 20,000-cycle run, with the periodic vblank charged against it
slice:
    mov ax, 20000
    push ax
    call _ni_run
    add sp, 2
    mov ax, 20000
    sub ax, [_ni_m + NIM_CNT]
    call vbl_tick
    ret

; sigok - ZF set when $DE $B0 $61 is at $6001-$6003 (the readme's own words,
; and the plan's `$G1` was a documentation typo for $61)
sigok:
    mov bx, 1
    call read6000x
    cmp al, 0xDE
    jne .no
    mov bx, 2
    call read6000x
    cmp al, 0xB0
    jne .no
    mov bx, 3
    call read6000x
    cmp al, 0x61
    ret
.no:
    or  al, 0xFF                    ; ZF clear, and it is the answer
    ret

; read6000 / read6000x - the SRAM window is at NI_O_SRAM in the machine claim,
; so $6000 + BX is claim offset NI_O_SRAM + BX.
read6000:
    xor bx, bx
read6000x:
    push es
    push ax
    mov ax, SEG_MACH
    mov es, ax
    pop ax
    mov al, [es:bx + NI_O_SRAM]
    pop es
    ret

; vbl_tick - AX = the cycles just run. Raise the vblank flag every VBL_PERIOD,
; and latch an NMI edge with it when $2000 armed one.
vbl_tick:
    sub [vblcd], ax
    jns .no
    add word [vblcd], VBL_PERIOD
    mov byte [h_vbl], 0x80
    test byte [h_ctrl], 0x80
    jz .no
    push es
    push ax
    mov ax, SEG_MACH
    mov es, ax
    or byte [es:NI_SCR + NI_S_IRQ], 0x02    ; the NMI EDGE, which is what the
    pop ax                                  ; core consumes when it takes it
    pop es
.no:
    ret

reset_cpu:
    mov word [_ni_m + NIM_A], 0
    mov word [_ni_m + NIM_X], 0
    mov word [_ni_m + NIM_Y], 0
    mov word [_ni_m + NIM_S], 0
    mov word [_ni_m + NIM_P], 0x24
    call _ni_boot                   ; S -= 3, I set, PC from $FFFC
    ret
%endif

halt:
    mov si, msg_nl
    call puts
    cli
.h: hlt
    jmp .h

; =============================================================================
; THE STUB BUS - the two cdecl entry points nicpu.inc calls
; =============================================================================
; int ni_io_rd(unsigned a)
_ni_io_rd:
    push bp
    mov bp, sp
    push bx
    mov bx, [bp+4]
    xor ax, ax
    cmp bx, 0x4000
    jae .apu
    and bx, 7
    cmp bx, 2
    jne .out
    mov al, [h_vbl]                 ; $2002: the flag, and READING CLEARS IT
    mov byte [h_vbl], 0
    jmp short .out
.apu:
    cmp bx, 0x4016
    jb  .out
    cmp bx, 0x4018
    jae .out
    mov al, 0x40                    ; a connected pad with nothing pressed
.out:
    pop bx
    pop bp
    ret

; int ni_io_wr(unsigned a, int v) -> the re-bias flag
_ni_io_wr:
    push bp
    mov bp, sp
    push bx
    mov bx, [bp+4]
    mov ax, [bp+6]
    cmp bx, 0x4000
    jae .out
    and bx, 7
    or  bx, bx
    jnz .out
    mov [h_ctrl], al                ; $2000, for the NMI arm
.out:
    xor ax, ax                      ; mapper 0: the mapping never moves
    pop bx
    pop bp
    ret

; =============================================================================
; SERIAL
; =============================================================================
putc_raw:                           ; one RAW byte (the trace's records)
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

putw:                               ; AX, little-endian
    push ax
    call putc_raw
    pop ax
    push ax
    mov al, ah
    call putc_raw
    pop ax
    ret

puts:
    push ax
    push si
.l:
    mov al, [cs:si]
    inc si
    or al, al
    jz .e
    call putc
    jmp .l
.e:
    pop si
    pop ax
    ret

putnum:
    push ax
    push bx
    push cx
    push dx
    mov cx, 0
    mov bx, 10
.dv:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .dv
.pr:
    pop ax
    add al, '0'
    call putc
    loop .pr
    pop dx
    pop cx
    pop bx
    pop ax
    ret

section .rodata
msg_trace:  db "NICPUTRACE", 10, 0
msg_tend:   db 10, "NICPUEND", 10, 0
msg_blargg: db 13, 10, "nicputest: running", 13, 10, 0
msg_res:    db "nicputest: result ", 0
msg_txt:    db 13, 10, "nicputest: text ", 0
msg_nosig:  db 13, 10, "nicputest: NO $DE $B0 $61 SIGNATURE at $6001", 13, 10, 0
msg_tmo:    db 13, 10, "nicputest: TIMEOUT while running", 13, 10, 0
msg_nostart: db 13, 10, "nicputest: TIMEOUT - $6000 never reached $80 with the signature", 13, 10, 0
msg_pass:   db "nicputest: PASS", 13, 10, 0
msg_fail:   db "nicputest: FAIL", 13, 10, 0
msg_nl:     db 13, 10, 0

section .bss
cyc_lo:  resw 1
cyc_hi:  resw 1
icount:  resw 1
prg16:   resb 1
h_vbl:   resb 1
h_ctrl:  resb 1
vblcd:   resw 1
capl:    resw 1
caph:    resw 1
rstwait: resw 1
result:  resb 1

section .text

; =============================================================================
; THE SHIPPING TEXT, %included and never copied
; =============================================================================
%include "nicpu.inc"
%include "nimem.inc"
