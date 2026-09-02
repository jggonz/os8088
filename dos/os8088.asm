; OS8088.COM - leave FreeDOS and go back to os8088 (SPEC.md 86.5).
;
; NOT called EXIT.COM, which is the obvious name and does not work: EXIT is a
; FreeCOM INTERNAL command, and an internal command always shadows a .COM of
; the same name. COMMAND.COM is the primary shell here (SHELL=... /P), and for
; a primary shell EXIT is defined to do nothing at all - so `EXIT` at the
; prompt silently returns to the prompt and the user concludes the machine is
; stuck. Measured, not theorised. The name also says where it goes.
;
; os8088 and FreeDOS are two BOOTS of one machine, not two programs: the 8088
; has no protection hardware of any kind, so DOS is given the whole machine and
; getting out means booting again. This is the way back.
;
; WHY A WARM BOOT AND NOT `int 19h`.
;   int 19h is the tempting one - FreeDOS hooks it, restores the interrupt
;   vectors it saved at startup, and chains to the ROM bootstrap, which is
;   faster than what we do here. But int 19h RESETS NO HARDWARE, and os8088's
;   own reboot path says so in as many words (kernel/ui.inc, drv_shutdown).
;   os8088 can rely on it because os8088 cleans up after itself. A DOS game
;   does not: it leaves the PIT reprogrammed to its own tick rate, IRQs masked
;   in the 8259, the CRTC in a mode of its own devising and, with a sound card,
;   an auto-init DMA transfer still running. Chaining to the bootstrap in that
;   state hands os8088 a machine with, variously, no timer tick (a scheduler
;   that never runs), no keyboard, or a display it cannot probe.
;
;   Setting the BIOS reset flag to 1234h and jumping to the ROM entry re-runs
;   POST, which reinitialises the PIT, the 8259s, the DMA controller, the
;   keyboard controller and the video card. 1234h is specifically the WARM
;   value: it tells POST to skip the memory test, which is the slow part. The
;   cost over int 19h is a second or two; what it buys is that the return works
;   after ANY DOS program rather than after well-behaved ones.
;
;   This is also exactly what Ctrl-Alt-Del does, which is the fallback when a
;   DOS program has hung hard enough that this one cannot be typed.
;
; The BIOS then boots drive 0 - the os8088 system disk - because the DOS floppy
; is in drive 1 (SPEC.md 86.4). Nothing survives the switch.

    cpu 8086
    org 0x100

start:
    mov ah, 0x09
    mov dx, msg
    int 0x21                    ; say what is about to happen: the screen is
                                ; about to be cleared by POST and a machine
                                ; that reboots with no explanation reads as a
                                ; crash

    ; Flush anything DOS still has buffered. We are about to reset the machine
    ; out from under the file system, and a dirty buffer is a corrupt floppy.
    mov ah, 0x0D
    int 0x21                    ; DISK RESET - commit every write buffer

    cli
    mov ax, 0x0040
    mov ds, ax
    mov word [0x0072], 0x1234   ; BIOS reset flag: 1234h = WARM (skip the
                                ; memory test). Any other value is a cold boot
                                ; and works too, just slower.
    jmp 0xFFFF:0x0000           ; the ROM entry point

msg db 13,10,'Returning to os8088...',13,10,'$'
