; =============================================================================
; tests/dostrap/call5.asm - CALL 5, CP/M's way into DOS (KERN-MEGA-DOS-PLAN
; 10.5 defect 3)
;
;   nasm -f bin -w+error -o CALL5.COM tests/dostrap/call5.asm
;
; A CP/M-style program puts the function in CL and does a NEAR `call 5`; PSP:5
; is a far call whose target DOS builds so that segment x 16 + offset is the
; PSP's own dispatcher. For a program loaded in the first 64KB the segment
; wraps, and with A20 OPEN - kern_mega's kern_dos lives in the HMA, so it is
; open for the whole run - an unfixed box lands inside kern_dos's image.
;
; It prints "C5" through function 2 (CL = 2, DL = the character) twice, then
; asks function 19h (current drive) the same way and exits with ITS answer:
; run from C: that is exit code 002, which only a call that dispatched on CL
; and came back to the program can produce. A box that sends the call astray
; hangs or faults instead; under a real DOS it runs unchanged.
;
; It reaches dos_cpm three ways, and tests/call5.py drives the first two on
; kern_big from B: (exit 001): the windowed box, whose segment fits a word;
; kern_dos low in memory, through the 1MB wrap; and kern_mega's HMA run,
; under Bochs.
; =============================================================================
    cpu 8086
    bits 16
    org 0x100

start:
    mov cl, 2
    mov dl, 'C'
    call 5
    mov cl, 2
    mov dl, '5'
    call 5
    mov cl, 0x19
    call 5                          ; AL = the current drive, 0 = A:
    mov ah, 0x4C
    int 0x21
