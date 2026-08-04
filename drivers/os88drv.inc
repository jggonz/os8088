; =============================================================================
; os8088 driver SDK (SPEC.md 51.2)
;
;   %include "os88drv.inc"
;   OS88_DRIVER 'Sound', DRVC_SOUND, my_entry
;   ... code and data ...
;   OS88_DRV_END
;
; A driver is a package that is not an application. It is assembled at org 0,
; it owns the segment the kernel claims for it, and the kernel reaches every
; one of its procs through the same three-byte dispatcher a package uses - so
; every proc here is an ordinary NEAR proc with a near `ret`, and a driver
; author never writes `retf`.
;
; What is different from apps/os88api.inc, and why:
;
; - **There is no bss.** A driver's zeroed data is written as `db 0` in the
;   image and ships on the floppy. A package's bss is claimed by the loader
;   because a package's is tens of KB; a driver's is a few hundred bytes, and
;   paying for them on disk buys a load path with exactly one heap claim in
;   it - made at the size the directory entry already reported, before a byte
;   is read. Anything bulk (a DMA buffer, a ring) is your own
;   OSAPI_MEM_CLAIM at attach, freed at detach.
;
; - **You have no window, no instance and no menu.** You are not an app: no
;   dock tile, no Task Manager row, no callbacks from the window manager. The
;   Control Panel is your user interface and the kernel owns it.
;
; - **DS = CS on every entry**, and ES = KERNEL_SEG, exactly as for a package
;   callback. SS is still LOW_SEG, so `[bp+disp]` still addresses SS and a
;   BP-held pointer still needs a `ds:` override.
;
; - **Attach must be all-or-nothing.** If your hardware is not there, return
;   CF=1 having hooked NOTHING - no interrupt vector, no port left in a state
;   the machine did not boot in, no heap claim held. The kernel frees your
;   image the moment you say no, and anything you left behind outlives you.
;
; - **Detach cannot fail.** Silence the chip, unhook whatever you hooked,
;   restore the ports you saved at attach, free your claims, return. The user
;   turned you off; there is no answer but yes.
; =============================================================================

%include "os88api.inc"          ; the OSAPI_* far-call table: a driver reaches
                                ; the kernel exactly as a package does

; --- driver classes, mirrored from kernel/driver.inc -------------------------
DRVC_SOUND   equ 1

; --- the capability bits a sound driver may ADD (SPEC.md 34.2) ---------------
; os88api.inc publishes the two the resident kernel already has (TONE and
; PCM_EXCL, the PC speaker's); these are the ones only a card can offer, and
; the numbers are `main`'s so one source assembles for either fork.
SND_CAP_FM       equ 02h        ; 9-ch 2-op patch + note-on/off (OPL2 model)
SND_CAP_PCM_BG   equ 04h        ; self-clocked block PCM out - background-safe
SND_CAP_PCM_IN   equ 10h        ; block PCM input (recording)

; --- entry verbs (AL on the far call into your entry proc) -------------------
DRVV_ATTACH  equ 0              ; out CF=0 and SI = your service table;
                                ;     CF=1 = no hardware, nothing hooked
DRVV_DETACH  equ 1              ; out: nothing. Cannot fail.

; --- the service table you return in SI at attach ----------------------------
; Offsets in YOUR segment. The kernel copies the whole thing once, so it may
; live in read-only image data; a 0 means "I do not provide this".
;   DSV_CAPS    dw  capability bits you ADD to OSAPI_SND_CAPS (SND_CAP_*)
;   DSV_FM      dw  near proc behind OSAPI_SND_FM     (in: AL verb, DH = the
;                   requesting instance slot, other regs per SPEC.md 34.2)
;   DSV_STREAM  dw  near proc behind OSAPI_SND_STREAM (in: AL verb, DH = the
;                   requesting instance slot)
;   DSV_TICK    dw  near proc called from snd_tick - INSIDE IRQ0, at IF=0.
;                   Keep it short and touch no port that needs a long delay.
;   DSV_RELINST dw  near proc: AL = an instance slot that is being torn down;
;                   release every grant it holds.
;   DSV_TONE    dw  near proc: the tone tier's sink while you are loaded
;                   (in: AX = Hz or 0 = off, DL = voice; out CF). Publishing
;                   this MOVES tones off the PC speaker onto your hardware,
;                   which is what an OPL2 wants and a Sound Blaster does not.
;   DSV_NAME    dw  -> a NUL sink name in your segment, for the Control Panel
DSV_CAPS     equ 0
DSV_FM       equ 2
DSV_STREAM   equ 4
DSV_TICK     equ 6
DSV_RELINST  equ 8
DSV_NAME     equ 10
DSV_TONE     equ 12
DSV_SIZE     equ 14

; -----------------------------------------------------------------------------
; OS88_DRIVER 'NAME', class, entry - open the driver and emit its 32-byte
; header. Emits cpu 8086 / bits 16 / org 0 first, so it must be the first
; thing after the %include. The image-size word is a forward reference that
; OS88_DRV_END resolves.
; -----------------------------------------------------------------------------
%macro OS88_DRIVER 3
    cpu 8086
    bits 16
    org 0
    db 'O', '8'             ; +0  magic (word 0x384F), a package's own
    db 4                    ; +2  format 4 = DRIVER. A package is 3, so the
                            ;     application loader refuses this file even
                            ;     if one ever reaches it (SPEC.md 51.1)
    db %2                   ; +3  class: the kernel checks it against the
                            ;     table row it is filling
    dw 0                    ; +4  link base: 0 (org 0, like every package)
    dw (%3) - $$            ; +6  entry offset
    dw OS88_DRV_SIZE        ; +8  image size = the whole file, data included
    dw 0                    ; +10 reserved
    db 0FFh, 0D5h           ; +12 THE DISPATCHER: `call bp`...
    db 0CBh                 ; +14 ...`retf`. The kernel far-calls this fixed
    db 0                    ; +15 offset with BP = the proc it wants.
    %strlen %%len %1
    %if %%len > 15
        %error "OS88_DRIVER: name must be at most 15 characters"
    %endif
    db %1                   ; +16 driver name...
    times 16 - %%len db 0   ;     ...NUL-padded to 16 bytes
%endmacro

; -----------------------------------------------------------------------------
; OS88_DRV_END - close the driver: defines OS88_DRV_SIZE for the header and
; holds the image to what the kernel will claim for it (DRV_MAX_KB).
; -----------------------------------------------------------------------------
%macro OS88_DRV_END 0
os88_drv_end:
OS88_DRV_SIZE equ os88_drv_end - $$
    %if OS88_DRV_SIZE > 40 * 1024
        %error "driver too big: the image must fit DRV_MAX_KB (40KB)"
    %endif
%endmacro
