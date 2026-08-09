; chainb - a 512-byte boot sector that chain-boots the DOS floppy in drive B:.
;
; A CAPABILITY GATE (SPEC.md 59.7), not shipped software. It answers the one
; question the FreeDOS handover rests on, with nothing else in the frame: can
; the machine boot the disk in unit 1, and does FreeDOS then come up with B: as
; its boot drive? Everything else in the handover - the teardown, the driver
; unhook, the video reset - is os8088 machinery that is already known to work.
; This isolates the part that is not.
;
; Boot it as drive A: with the DOS floppy as drive B:. Expected: the FreeDOS
; banner, then a B:\> prompt.
;
; It is deliberately the SAME code as the kernel's ui_hstub, so a failure here
; is a failure there. Keep them in step.

    cpu 8086
    org 0x7C00

BOOT_UNIT   equ 1              ; BIOS unit for the second floppy = B:
STUB_AT     equ 0x0500         ; where we run from once relocated - see below
LOAD_AT     equ 0x7C00         ; where a boot sector must be entered

start:
    ; ---- 1. a known machine state -------------------------------------------
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, LOAD_AT            ; the conventional boot stack: grows down, away
                               ; from the sector about to land at 0x7C00
    cld
    sti

    ; ---- 2. get out of the way ----------------------------------------------
    ; THE WHOLE REASON THIS CODE IS SHAPED LIKE THIS. The sector we are about
    ; to read has to be entered at 0000:7C00 because that is the contract - and
    ; that is exactly where WE are executing. Reading it in place overwrites
    ; the instruction doing the reading.
    ;
    ; 0x0500 is the first byte above the BIOS data area and below every use the
    ; machine makes of low memory - print-screen at 0x500 is dead once DOS
    ; owns the machine, and DOS's own single-drive byte at 0x504 is written
    ; later, long after this code has stopped existing. boot/boot.asm makes the
    ; same argument for 0x0580 and the kernel already relies on it.
    mov si, LOAD_AT
    mov di, STUB_AT
    mov cx, 256                ; 512 bytes
    rep movsw
    jmp 0:(STUB_AT + relocated - start)

relocated:
    ; ---- 3. read the DOS boot sector ----------------------------------------
    mov dl, BOOT_UNIT
    xor ax, ax
    int 0x13                   ; AH=00 reset the controller: the drive has not
                               ; been touched this power-on and the first seek
                               ; on an untouched unit is the one that fails

    mov ax, 0x0201             ; AH=02 read, AL=1 sector
    mov cx, 0x0001             ; CH=cylinder 0, CL=sector 1
    xor dh, dh                 ; head 0
    mov dl, BOOT_UNIT
    mov bx, LOAD_AT            ; ES:BX, ES already 0
    int 0x13
    jc .rom

    cmp word [LOAD_AT + 510], 0xAA55
    jne .rom                   ; unformatted, or no disk in the drive

    ; ---- 4. enter it --------------------------------------------------------
    ; DL is the entire boot protocol. FreeDOS's boot sector stores it, passes
    ; it to the kernel, and the kernel makes it the boot drive - so DL=1 here
    ; is what puts the user on B:.
    mov dl, BOOT_UNIT
    jmp 0:LOAD_AT

.rom:
    ; Deliberately benign: an empty or unformatted B: restarts the machine
    ; rather than jumping into whatever was in the buffer.
    int 0x19

    times 510 - ($ - $$) db 0
    dw 0xAA55
