; =============================================================================
; os8088 - apps/c64/c64sid.inc
;
; SID VOICE 3's OSCILLATOR, READABLE (C64-SPEC 11.4.1): $D41B is the top
; eight bits of voice 3's waveform output, and the C64's standard random
; number is voice 3 on the NOISE waveform read from there - the Portland CUG
; Christmas demo spreads its snow with it, and with $D41B answering a stored
; constant every flake landed in column 0. The oscillator is computed when it
; is READ, from the cycles since the last read (c64io.c's c64_osc3_now), the
; way VICE's sid_read() clocks the chip up to the CPU's clock first.
;
; Derived from VICE (GPL-2-or-later, apps/c64/COPYING) by behaviour only: the
; 6581's 24-bit phase accumulator, its 23-bit noise LFSR clocked on the rise
; of accumulator bit 19 with feedback bit 22 ^ bit 17, and the noise output's
; eight taps (22 20 16 13 11 7 4 2), as reSID documents them.
; =============================================================================

section .bss
c64_o3acc:  resw 2                  ; the accumulator: low word, high byte
c64_o3nz:   resw 2                  ; the LFSR: bits 0-15, bits 16-22

section .text

%macro C64_NZTAP 3                  ; reg, its bit, the output bit
    test %1, %2
    jz %%n
    or bl, %3
%%n:
%endmacro

; -----------------------------------------------------------------------------
; unsigned c64_osc3(unsigned dt, unsigned freq, unsigned ctrl, unsigned pw)
; Voice 3 advanced by DT cycles at FREQ ($D40E/$D40F), then its output for
; the waveform bits of CTRL ($D412) and the 12-bit pulse width PW. TEST
; (ctrl bit 3) holds the accumulator at 0 and the LFSR at all ones. Several
; waveforms AND together; none answers 0.
; -----------------------------------------------------------------------------
_c64_osc3:
    push bp
    mov bp, sp
    push si
    push di
    test byte [bp+8], 0x08
    jz .run
    xor ax, ax                      ; TEST: the oscillator held at reset
    mov [c64_o3acc], ax
    mov [c64_o3acc+2], ax
    mov word [c64_o3nz], 0xFFFF
    mov word [c64_o3nz+2], 0x007F
    jmp .wave
.run:
    mov ax, [bp+4]
    mul word [bp+6]                 ; DX:AX = what the accumulator gains
    cmp dx, 0x0FFF                  ; (capped: 2^28 is 256 noise clocks,
    jbe .ok                         ;  and the sum below must not wrap)
    mov dx, 0x0FFF
    mov ax, 0xFFFF
.ok:
    ; bit 19 RISES once per 2^20 of travel: offset the accumulator by
    ; 2^24 - 2^19 so that every rise is a carry past a multiple of 2^20
    mov si, [c64_o3acc]
    mov di, [c64_o3acc+2]
    add di, 0x00F8
    mov bx, di
    mov cl, 4
    shr bx, cl                      ; BX = the old count of 2^20s
    add si, ax
    adc di, dx                      ; DI:SI = the new offset sum
    mov ax, di
    mov cl, 4
    shr ax, cl                      ; AX = the new count
    sub ax, bx                      ; = the rises of bit 19
    sub di, 0x00F8                  ; the accumulator itself, 24 bits
    and di, 0x00FF
    mov [c64_o3acc], si
    mov [c64_o3acc+2], di
    mov cx, ax
    jcxz .wave
    cmp cx, 256
    jbe .clk
    mov cx, 256                     ; (the cap's own figure)
.clk:
    mov ax, [c64_o3nz]
    mov dx, [c64_o3nz+2]
.step:
    mov bl, dl                      ; bit 22 is DL bit 6, bit 17 DL bit 1
    rol bl, 1
    rol bl, 1                       ; bit 6 -> bit 0
    mov bh, dl
    shr bh, 1                       ; bit 1 -> bit 0
    xor bl, bh
    and bl, 1
    shl ax, 1
    rcl dx, 1
    or al, bl
    and dx, 0x007F
    loop .step
    mov [c64_o3nz], ax
    mov [c64_o3nz+2], dx
.wave:
    mov bh, 0xFF                    ; BH = the AND of every waveform's byte
    mov cl, [bp+8]
    test cl, 0xF0
    jnz .w
    xor ax, ax                      ; no waveform: nothing
    jmp .out
.w:
    test cl, 0x80                   ; NOISE: the eight taps
    jz .nnz
    mov ax, [c64_o3nz]
    mov dx, [c64_o3nz+2]
    xor bl, bl
    C64_NZTAP dl, 0x40, 0x80        ; 22
    C64_NZTAP dl, 0x10, 0x40        ; 20
    C64_NZTAP dl, 0x01, 0x20        ; 16
    C64_NZTAP ah, 0x20, 0x10        ; 13
    C64_NZTAP ah, 0x08, 0x08        ; 11
    C64_NZTAP al, 0x80, 0x04        ; 7
    C64_NZTAP al, 0x10, 0x02        ; 4
    C64_NZTAP al, 0x04, 0x01        ; 2
    and bh, bl
.nnz:
    mov si, [c64_o3acc]
    mov di, [c64_o3acc+2]
    test cl, 0x20                   ; SAWTOOTH: bits 23-16
    jz .nsw
    mov ax, di
    and bh, al
.nsw:
    test cl, 0x10                   ; TRIANGLE: bits 22-15, folded by 23
    jz .ntr
    mov ax, si
    shl ax, 1                       ; bit 15 into CF
    mov al, byte [c64_o3acc+2]
    rcl al, 1                       ; bits 22-15
    test byte [c64_o3acc+2], 0x80
    jz .tf
    not al
.tf:
    and bh, al
.ntr:
    test cl, 0x40                   ; PULSE: the top 12 bits against PW
    jz .npu
    mov ax, di
    mov dx, si
    push cx
    mov cl, 4
    shl ax, cl                      ; bits 23-16 to 11-4
    mov cl, 12
    shr dx, cl                      ; bits 15-12 to 3-0
    pop cx
    or ax, dx
    and ax, 0x0FFF
    cmp ax, [bp+10]
    mov al, 0xFF
    jae .pa
    xor al, al
.pa:
    and bh, al
.npu:
    mov al, bh
    xor ah, ah
.out:
    pop di
    pop si
    pop bp
    ret
