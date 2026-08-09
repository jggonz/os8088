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

; The dispatcher's fixed offset inside the image the OS88_DRIVER macro emits.
; The kernel far-calls it, and so may you: HDD.DRV loads HDDTOOL.DRV and calls
; into it exactly as wm_pkgcall calls a package (SPEC.md 52.11).
PKG_DISP     equ 12

; --- driver classes, mirrored from kernel/driver.inc -------------------------
; A class is also a PUBLICATION SLOT: one driver of each class may be
; attached at a time, and each has its own copy of the service table below,
; so a sound driver and a disk driver do not disconnect one another.
DRVC_SOUND   equ 1
DRVC_DISK    equ 2              ; block volumes: hard disks (SPEC.md 51.8)
DRVC_DEBUG   equ 3              ; the serial monitor (SPEC.md 58): no service
                                ; the kernel dispatches, only a Control Panel
                                ; page - but a class of its own, because a
                                ; class is the publication slot and sharing
                                ; one disconnects the driver already in it
DRVC_OVL     equ 4              ; NOT A KERNEL CLASS. A driver's own loadable
                                ; half (OS88_OVERLAY below): the kernel never
                                ; loads one and drv_check would refuse it, and
                                ; the number is here so that saying so is a
                                ; property of the file rather than a comment

; --- the capability bits a sound driver may ADD (SPEC.md 34.2) ---------------
; os88api.inc publishes the two the resident kernel already has (TONE and
; PCM_EXCL, the PC speaker's); these are the ones only a card can offer.
SND_CAP_FM       equ 02h        ; 9-ch 2-op patch + note-on/off (OPL2 model)
SND_CAP_PCM_BG   equ 04h        ; self-clocked block PCM out - background-safe
SND_CAP_PCM_IN   equ 10h        ; block PCM input (recording)

; --- sound tiers (SPEC.md 34.8): what DRVV_TIER's AH carries -----------------
; The kernel's own [snd_route], mirrored here so a driver can compare against
; it without reaching into kernel headers. A driver that implements DRVV_TIER
; only has to know "is this at least my expensive tier?".
SND_RT_AUTO  equ 0              ; unset: whatever answered at boot
SND_RT_SPK   equ 1              ; the PC speaker
SND_RT_FM    equ 2              ; AdLib - FM only
SND_RT_SB    equ 3              ; Sound Blaster - FM and streams

; --- entry verbs (AL on the far call into your entry proc) -------------------
DRVV_ATTACH  equ 0              ; out CF=0 and SI = your service table;
                                ;     CF=1 = no hardware, nothing hooked
DRVV_DETACH  equ 1              ; out: nothing. Cannot fail.
DRVV_READY   equ 3              ; "your services are published; the kernel can
                                ; take your calls now". Sent ONCE, right after
                                ; attach returns, and it is the earliest point
                                ; at which you may call anything that comes
                                ; back through a fence - OSAPI_DRV_TASK,
                                ; OSAPI_VOL_*, anything keyed on the
                                ; publication slot. From ATTACH those all
                                ; refuse, because the slot is armed afterwards
                                ; and deliberately: a worker or a volume
                                ; created by an attach that then says no would
                                ; outlive the image. Optional - return if you
                                ; have nothing to do here.
DRVV_TIER    equ 2              ; in AH = how much of yourself the user wants
                                ; (SPEC.md 34.8); out CF=0 and SI = your
                                ; service table, which the kernel RE-COPIES
                                ; because a tier change alters it; CF=1 and
                                ; AL = a DRVE_* saying why not, with the table
                                ; untouched.
                                ;
                                ; For a driver whose tiers cost different
                                ; amounts of memory. The sound driver's Sound
                                ; Blaster tier holds a 12KB DMA buffer that
                                ; its AdLib tier does not, and on a 128KB
                                ; machine that is a fifth of the heap - so
                                ; which one is running is the USER's choice,
                                ; persisted, not a consequence of what the
                                ; probe found. A driver with one tier need
                                ; not implement this verb at all: the kernel
                                ; only sends it when a setting says to.
                                ;
                                ; Turning a tier ON may need memory and may
                                ; therefore FAIL, which is what CF is for.
                                ; Turning one OFF cannot fail, for the same
                                ; reason DRVV_DETACH cannot.

; --- refusal reasons, mirrored from kernel/driver.inc (SPEC.md 51.3) ---------
; What DRVV_TIER answers in AL when it says no. They are the SAME codes the
; loader banks in the row, so the Control Panel names a refused tier change
; with the string it already has for a refused LOAD - one vocabulary for both.
DRVE_MEM     equ 4              ; the heap cannot fund that tier right now
DRVE_HW      equ 5              ; the hardware that tier needs is not there
DRVE_BUSY    equ 6              ; ...or it IS there and something the kernel
                                ; already owns has its port or its IRQ. A
                                ; separate code because the two send the user
                                ; to different places: DRVE_HW means buy or
                                ; fit a card, this means move a jumper
                                ; (SPEC.md 58.2)
DRVE_TWICE   equ 7              ; YOU were asked to attach while you were
                                ; already attached. The one code here that
                                ; blames the software rather than the machine,
                                ; and the one to answer whenever a second
                                ; attach would re-claim something you already
                                ; hold: refuse, touch nothing, and return this
                                ; - the Drivers page and the boot notice print
                                ; it beside your name, so the bug arrives with
                                ; a sentence instead of as a heap that is
                                ; quietly short (SPEC.md 51.2.2)

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
;   DSV_TIERS   dw  bitmap of 1 << SND_RT_* : which tiers you COULD provide,
;                   whatever you are running right now. Every cell above says
;                   what you ARE; this one says what you could BE, so DRVV_TIER
;                   must NOT change it - decide it at attach, from what your
;                   probes actually found, and clear it at detach.
;
;                   It exists so the Control Panel can grey a tier your machine
;                   does not have WITHOUT asking you to probe again. The Sound
;                   Blaster scan is six ISA base addresses with a reset timeout
;                   each and then a slow retry on 220h - about a sixth of a
;                   second on a machine that has no card, spent under the gfx
;                   lock, which the user sees as the desktop freezing on a
;                   click that then does nothing. You already know the answer;
;                   this is where you put it. Leave it 0 and the panel assumes
;                   you can do everything and finds out the slow way.
DSV_CAPS     equ 0
DSV_FM       equ 2
DSV_STREAM   equ 4
DSV_TICK     equ 6
DSV_RELINST  equ 8
DSV_NAME     equ 10
DSV_TONE     equ 12
DSV_TIERS    equ 14
DSV_BLK      equ 16             ; DRVC_DISK: the block transport (below)
DSV_CPNAME   equ 18             ; -> a NUL Control Panel page name, in YOUR
                                ; segment; 0 = no page. The kernel STAGES it,
                                ; because cp_list draws it with font_str and
                                ; font_str reads through DS
DSV_CPPAINT  equ 20             ; near proc: that page's paint
DSV_CPCLICK  equ 22             ; near proc: that page's click
DSV_CPCLOSE  equ 24             ; near proc: "the Control Panel has been
                                ; closed" (SPEC.md 31.9.1). No arguments, no
                                ; answer, gfx lock HELD - so you may draw, and
                                ; you may destroy your own windows. 0 = do not
                                ; tell me.
                                ;
                                ; For state worth holding while the panel is up
                                ; and not after it. It is NOT a detach: you are
                                ; still loaded, whatever you published is still
                                ; published, and the panel may be back in a
                                ; second - so free what the PAGE was using, not
                                ; what you are
DSV_SIZE     equ 26

; --- DSV_BLK: one driver-backed volume's sectors (SPEC.md 51.8) -------------
;   in   AL = 0 read / 1 write
;        AH = YOUR volume handle, the one you passed to OSAPI_VOL_ADD
;        SI = the VOLUME-RELATIVE LBA, 16-bit - the kernel knows nothing
;             about partitions, so you add your own 32-bit base
;        CX = sector count
;        DX:BX = the buffer. DX and NOT ES, because the buffer is in the
;             kernel's low segment, its FAT window or a heap claim and never
;             in your segment or the kernel's - so ES cannot carry its usual
;             meaning here, and `mov es, dx` is yours to do
;   out  CF = 0 done; CF = 1 and AL = an int 13h status byte (03h = write
;             protected, anything else = a hard error)
;
; A volume is capped at 65,535 sectors (31.99MB) by the kernel's own BPB
; rule 8, which is also the DOS 3.3 partition limit these machines ran. More
; capacity is more partitions, each its own volume and its own drive letter.

; --- the Control Panel page you own (SPEC.md 31.9) --------------------------
; Publish DSV_CPNAME and your page appears in the panel's list - and only
; while you are loaded, which falls out of the publication slot being cleared
; at detach rather than being arranged anywhere.
;
;   paint   DI = pane left, DX = pane top
;   click   DI = pane left, DX = pane top, CX = rel x, BX = rel y
;
; The pane is 221 x 121 px - 27 characters by 13 rows - and the panel is not
; resizable, so anything bigger belongs in a window of your own.
;
; Everything a package callback must obey applies: the gfx lock is HELD (draw
; straight through the primitives, and NEVER take it), ES is KERNEL_SEG, and
; you must repaint yourself because the kernel does not repaint after you
; return. A greyed control follows SPEC.md 47 - a FLAG, not a colour.

; --- a driver's worker task (SPEC.md 51.7) -----------------------------------
; OSAPI_TASK_SPAWN is not available to you: its fence is a chain of tests on
; an INSTANCE RECORD and you have none. This is the driver's own slot, with
; the same identity fence against your segment.
;
;   AX = a near entry in YOUR segment -> spawn. DH is yours and reaches the
;                                       new task's DH, which is how you give
;                                       a worker something PER-TASK (a
;                                       generation, say) that a variable
;                                       cannot carry safely. out CF=0 and
;                                       AL = the slot, CF=1 refused.
;   AX = 0                            -> "this IS the worker, exiting". NEVER
;                                        RETURNS.
;
; Two rules, and the second one is not optional:
;   1. You cannot spawn from ATTACH - the kernel arms the fence only after
;      attach returns. Spawn from a service call.
;   2. YOUR DETACH MUST NOT RETURN UNTIL YOUR WORKER IS GONE. The kernel
;      frees your image the moment detach returns, and a worker still on the
;      run queue leaves the scheduler holding a stack frame whose CS points
;      at memory the next heap claim takes.
%define OSAPI_DRV_TASK      KERNEL_SEG:0x0248

; --- volumes a DRVC_DISK driver mounts (SPEC.md 18.7/51.8) -------------------
; A volume is a small INDEX - 0 = A:, 1 = B:, 2 = C: - and the desktop zone,
; the drive letter and the file manager all fall out of it. The fence on all
; three is the same identity test OSAPI_DRV_TASK uses: your segment must be
; the segment of the driver whose DISK services are published.
;
;   OSAPI_VOL_ADD    in  AL = your own volume handle (whatever you like: it
;                            comes back in DSV_BLK's AH)
;                        CX = the volume's sector count, for the kernel's own
;                            "does this BPB claim to be bigger than its
;                            partition" check
;                        DX = a listing claim's segment, or 0 for the kernel's
;                            32-entry floor. HDD_LISTKB below is its size
;                        SI = a NUL desktop label in YOUR segment (0 = the
;                            kernel derives 'Disk X' from the index)
;                    out CF = 0 and AL = the volume index; CF = 1 = no room
;   OSAPI_VOL_DEL    in  AL = a volume index you registered
;   OSAPI_VOL_MOUNT  in  AL = a volume index; out CF = 1 = not a readable
;                        FAT12/16 volume. UI-TASK CONTEXT ONLY, like every
;                        file slot: it takes the scheduler lock around int 13h
;   OSAPI_VOL_PAINT  repaint the desktop's drive zones. It takes the gfx lock
;                        ITSELF, so never call it from a page click - defer it
;                        the way the Control Panel defers its own repaints
; The four slots themselves are published by os88api.inc, which this file
; includes: a slot number belongs in one place (SPEC.md 20.8).

; The listing claim a mounted volume wants (SPEC.md 22.6): DSK_VENT entries of
; 32 bytes followed by the same number of 64-byte icon slots. Claim it before
; OSAPI_VOL_ADD and hand over the segment; pass 0 instead and the volume lists
; into the kernel's own 32-entry buffers, which works and shows fewer files.
HDD_LISTKB   equ 6

; --- memory a bus-master can reach (SPEC.md 50.3/51.6) -----------------------
; Everything in the app SDK's memory section applies to you unchanged - your
; IMAGE segment is your owner word, exactly as a package's region is its own -
; so OSAPI_MEM_CLAIM and OSAPI_MEM_FREE need no driver variant. One thing does:
; a buffer the ISA DMA controller addresses must not cross a 64KB PHYSICAL
; boundary, because the 8237 has no register for address bits 16..19 and never
; carries into the page port that does. Ask for that up front with
; OSAPI_MEM_CLAIM_DMA (AX = KB, CX = KB of the head the chip will see) rather
; than claiming, testing the address, and claiming again while holding the
; block that failed - that pattern can hold four buffers to find one, and
; refuse on a machine that had the room all along.
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
; OS88_OVERLAY 'NAME', abi, entry - a driver's own loadable half (SPEC.md
; 52.11). Same header, same dispatcher, same one-claim load discipline, and
; NOT A DRIVER: it has no class the kernel knows, it is not in drv_tab, and
; the kernel never sees it. Its owner is another driver, which reads it off
; the system volume when the user asks for something that needs it and frees
; it again afterwards - HDD.DRV and HDDTOOL.DRV are the worked example.
;
; Two differences from OS88_DRIVER, both deliberate:
;
;   class DRVC_OVL   so nothing can mistake it for a driver of a real class.
;                    The kernel would refuse it; os88drv.py names it 'overlay'.
;   +10 = the ABI    the reserved word carries the version of the private
;                    interface it was built against, because there is nothing
;                    that makes a machine's overlay the same vintage as the
;                    driver that loads it - a half-copied floppy is exactly
;                    that. Its owner checks this word before the first call.
;
; The version byte stays 4, so the APPLICATION loader refuses this file too
; (SPEC.md 51.1) and it can never be double-clicked into the package loader.
; -----------------------------------------------------------------------------
%macro OS88_OVERLAY 3
    cpu 8086
    bits 16
    org 0
    db 'O', '8'             ; +0  magic
    db 4                    ; +2  format 4: driver-shaped, not an application
    db DRVC_OVL             ; +3  ...but no class the kernel will load
    dw 0                    ; +4  link base: 0
    dw (%3) - $$            ; +6  entry offset
    dw OS88_DRV_SIZE        ; +8  image size = the whole file
    dw %2                   ; +10 the private ABI version its owner checks
    db 0FFh, 0D5h           ; +12 the dispatcher: `call bp`...
    db 0CBh                 ; +14 ...`retf`
    db 0
    %strlen %%len %1
    %if %%len > 15
        %error "OS88_OVERLAY: name must be at most 15 characters"
    %endif
    db %1                   ; +16 name, NUL-padded to 16
    times 16 - %%len db 0
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
