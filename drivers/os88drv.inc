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

; --- driver classes ----------------------------------------------------------
; DRVC_* IS IN apps/os88api.inc, which this file includes above, and it is
; there rather than here because a PACKAGE names one too: OSAPI_DRV_CALL takes
; a class in BH (SPEC.md 20.11). It was mirrored in both for as long as only
; drivers could name one, and a mirror that nothing checks is a second opinion
; waiting to differ - the kernel's copy in kernel/driver.inc is the third and
; is the one that cannot be shared, a driver never including a kernel header.
;
; A class is also a PUBLICATION SLOT: one driver of each class may be
; attached at a time, and each has its own copy of the service table below,
; so a sound driver and a disk driver do not disconnect one another.
DRVC_OVL     equ 0x40           ; NOT A KERNEL CLASS, so it stays here. A
                                ; driver's own loadable half (OS88_OVERLAY
                                ; below): the kernel never loads one and
                                ; drv_check would refuse it, and the number is
                                ; here so that saying so is a property of the
                                ; file rather than a comment.
                                ; It WAS 4 and moved when DRVC_NET took that
                                ; number: a real class costs a publication
                                ; slot (42 bytes) and burning one to leave a
                                ; hole would have been the dearer half of the
                                ; choice. Well clear of DRVC_MAX, so
                                ; drv_cls_idx refuses it by range alone

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
                                ; fit a card, this means move a jumper. The
                                ; worked example was the serial monitor's
                                ; refusal to take a port the mouse had already
                                ; answered on; that driver is gone (SPEC.md
                                ; 58) and the code is not - any driver whose
                                ; hardware is THERE but SPOKEN FOR wants it
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
DSV_FS       equ 26             ; dw: DRVC_FILE only - the FSV_* verb table,
                                ; an offset in YOUR OWN segment (SPEC.md
                                ; 62.9.1). A pointer rather than thirteen more
                                ; cells here, because DSV_* is copied per
                                ; class into the kernel's .bss and thirteen
                                ; would be charged to every machine including
                                ; the ones with no driver at all
DSV_CPKEY    equ 28             ; dw: near proc: that page's KEY (SPEC.md
                                ; 31.9.2). **BL = ASCII, BH = scan code** and
                                ; the gfx lock is HELD exactly as it is for a
                                ; click. 0 = this page takes no keys, which is
                                ; every page that existed before this cell -
                                ; and until it, the Control Panel's own window
                                ; had no W_ONKEY, so a page could not be typed
                                ; into at all.
                                ;
                                ; **BX AND NOT AX**, unlike int 16h and unlike
                                ; W_ONKEY, because drv_cp_call carries the
                                ; CLASS in AL (SPEC.md 31.9) - AX is the one
                                ; register no page ABI argument may use. In AX
                                ; this assembles, boots, and hands the page a
                                ; class number as a keystroke.
                                ;
                                ; YOU MUST REPAINT WHAT YOU CHANGE, as with a
                                ; click: the kernel does not repaint after you
                                ; return. Answer CF = 0 when you took the key
                                ; and CF = 1 when you did not - an unclaimed
                                ; key is not an error, it is a key that belongs
                                ; to nobody, and the panel drops it
DSV_CPUP     equ 30             ; near proc: that page's RELEASE, with the
                                ; same DI/DX/CX/BX as DSV_CPCLICK. 0 = I do
                                ; not want releases, and my page acts on the
                                ; press as it always did (SPEC.md 13.8.4)
DSV_CPDRAG   equ 32             ; near proc: the TRACKING edge - the pointer
                                ; moved while the press is still down, same
                                ; arguments, and only when the point CHANGED.
                                ; What it is for is un-drawing a pressed
                                ; control the pointer has left (SPEC.md 13.8.1)
DSV_PKGCALL  equ 34             ; dw: near proc reached by OSAPI_DRV_CALL -
                                ; THE FENCE (SPEC.md 20.11). Publish it and a
                                ; package can call you; leave it 0 and it
                                ; cannot. HDD.DRV leaves it 0, so no package
                                ; can reach DSV_BLK and write a raw sector.
                                ;
                                ;   in  BL = a verb YOU define, BH = your class
                                ;       AX/CX/DX/SI/DI = yours to define
                                ;       ES = THE CALLING PACKAGE'S SEGMENT, so
                                ;            a buffer it hands you is ES:SI or
                                ;            ES:DI - this is the one driver
                                ;            entry point where ES is not the
                                ;            kernel's
                                ;       DS = yours, BP spent by the dispatcher
                                ;   out whatever you define; CF and AX reach
                                ;       the package unchanged
                                ;
                                ; **IT WAS 28 AND MOVED TO 34 IN A MERGE**, because the three page cells
                                ; above were appended at the same numbers on
                                ; the integration branch. A driver's service
                                ; table is copied by OFFSET, so two tables that
                                ; disagree about a cell hand the kernel one
                                ; routine where it asked for another - which is
                                ; why every driver here pads to DSV_SIZE rather
                                ; than ending where its last row does.
                                ;
                                ; WORKER-SAFE IS YOUR CLAIM, NOT THE KERNEL'S
                                ; (SPEC.md 20.6 rule 7): the cell itself takes
                                ; no lock and raises no sch_lock, so a verb of
                                ; yours is callable from a package's worker
                                ; only if what it does is. Say so per verb.
DSV_GEOM     equ 36             ; dw: near proc, DRVC_DISK only (SPEC.md 51.8,
                                ; 87.5): the TRANSPORT FACTS of one volume,
                                ; for a caller that has to reach its sectors
                                ; without you - the resume stub, which runs
                                ; after every driver has been detached.
                                ;   in  AH = your volume handle
                                ;   out CF = 0: DL = the int 13h drive,
                                ;       CX:BX = the partition's first LBA,
                                ;       SI = sectors per track, DI = heads;
                                ;       CF = 1: no such volume, or one the
                                ;       ROM's int 13h does not reach (52.1's
                                ;       rung 1). 0 = never, which is CF = 1
DSV_SIZE     equ 38

; --- DSV_FS: the file redirector's verbs (SPEC.md 62.9) ----------------------
; A DRVC_FILE volume has NO SECTORS - no BPB, no FAT, no directory to walk -
; and dsk_xfer refuses one outright. What the kernel asks instead is these,
; and the staged SPEC.md 19.1 entry's first-cluster word at offset 18 becomes
; AN OPAQUE HANDLE YOU ASSIGN: only dsk_chdir and the loader ever interpret
; it, and both are already redirect points.
;
; THE KERNEL STILL SORTS (19.4) AND STILL SYNTHESIZES `..` (19.5). Do neither
; - two places deciding a listing's order is how a display index stops meaning
; what the hit-test thinks it means.
;
; YOU OWE THE PROGRESS BAR (SPEC.md 12.8.1). The kernel arms 12.8's file
; activity widget from FSV_STAT's size and is then ONE FAR CALL DEEP inside
; your FSV_READ, blind, until you return - where a floppy is stepped from
; dsk_xfer's own per-sector loop. So call OSAPI_FS_PROG as the bytes move, AX
; = HOW MANY SINCE YOUR LAST REPORT (a running total advances the bar by the
; whole file every call). Unarmed it is a compare and a return, so call it
; unconditionally and never test; a short count is ordinary and the remainder
; below 512 bytes is carried, not rounded. Skip it and the bar sits at 0 for
; the length of the transfer, which is worse than the nothing it showed before
; the widget learned to count in bytes.
FSV_LIST     equ 0              ; APPEND this folder's entries, one at a time,
                                ; through OSAPI_FS_ENT. You do not stage them
                                ; and you cannot: dsk_put_dir knows whether
                                ; this volume's listing is the kernel's floor
                                ; or a claim YOU donated, and the count is
                                ; disk_nfiles'
FSV_CHDIR    equ 2              ; AX = a handle out of an entry, 0 = the root.
                                ; out CF=1 refuses; CF=0 and DX = THE PARENT'S
                                ; HANDLE - the kernel synthesizes `..` itself
                                ; and has no directory sector to read one out
                                ; of, and only you know what your handles mean.
                                ; Answered HERE and not by a verb of its own
                                ; because this is the only call that ever moves
                                ; the current folder
FSV_STAT     equ 4              ; SI = name -> exists, size, attributes
FSV_READ     equ 6              ; SI = name, DX:BX = buffer (NOT ES:BX: ES is
FSV_WRITE    equ 8              ; KERNEL_SEG on entry to everything the kernel
                                ; far-calls into you, so it cannot also be the
                                ; buffer's segment - DSV_BLK's rule, SPEC.md
                                ; 51.8, and `mov es, dx` is yours to do)
FSV_APPEND   equ 10             ; the chunked pair (18.4.4), so a copy streams
FSV_READAT   equ 12             ; ...and its read half
FSV_DELETE   equ 14             ; SI = name
FSV_RENAME   equ 16             ; SI = old, DI = new
FSV_MKDIR    equ 18             ; SI = name
FSV_RMDIR    equ 20             ; SI = name
FSV_DFREE    equ 22             ; -> DX:AX = free BYTES, BX = the granule
FSV_ENUM     equ 24             ; AX = a folder's handle (0 = the root),
                                ; CX = the ORDINAL, DX:BX = a 32-byte buffer.
                                ; out CF=0 and the buffer is a SPEC.md 19.1
                                ; staged entry (the same one FSV_LIST hands
                                ; to OSAPI_FS_ENT); CF=1 with AX = 0 past the
                                ; end, or AX = FERR_IO.
                                ; ONE ENTRY, into the CALLER's buffer - not
                                ; FSV_LIST, which appends into the kernel's
                                ; single global listing and is legal only
                                ; during a mount. This is what a folder COPY
                                ; walks with, and the folder it walks is not
                                ; the one on screen.
                                ; THE ORDINAL IS DENSE AND MUST BE STABLE:
                                ; 0,1,2,... over the entries you would list,
                                ; and the same (handle, ordinal) naming the
                                ; same entry for as long as that folder is
                                ; unmodified. The copy engine RE-WALKS to it
                                ; on every call, and between two of them it
                                ; will have written a file on another volume
                                ; and been through the event loop. Enumerate
                                ; a live structure with a cursor of its own
                                ; and the copy silently skips or duplicates
FSV_COPY     equ 26             ; AX = source folder, DX = destination folder,
                                ; SI = the name (both ends), in KERNEL_SEG.
                                ; out CF=0; CF=1 and AX = FERR_*
                                ;
                                ; BOTH FOLDERS ARE YOURS, so the file need not
                                ; cross the transport at all (SPEC.md 62.9.8).
                                ; The kernel asks only when both ends are the
                                ; same volume, and PUBLISHING IT IS OPTIONAL:
                                ; leave the row 0 and the copy engine streams
                                ; through FSV_READAT and FSV_WRITE as before,
                                ; which is what makes this an optimisation
                                ; rather than a duty. Implement it when the
                                ; far side can copy faster than the wire can
                                ; carry - which for anything with a real disk
                                ; behind it, it can
FSV_RMTREE   equ 28             ; SI = a folder's name in the CURRENT folder:
                                ; remove it AND EVERYTHING INSIDE IT (SPEC.md
                                ; 62.10.7). out CF=0; CF=1 and AX = FERR_*
                                ;
                                ; FSV_COPY's argument in the other direction,
                                ; and OPTIONAL for the same reason: a
                                ; kernel-side walk of your tree is one
                                ; FSV_ENUM plus one FSV_DELETE per file and an
                                ; FSV_CHDIR per level, and this is one call.
                                ; Leave the row 0 and a folder with anything
                                ; in it is refused by FSV_RMDIR exactly as it
                                ; was before - which is a real answer and not
                                ; a failure, so decline it unless you can
                                ; recurse more cheaply than the kernel can ask
                                ;
                                ; IF YOU IMPLEMENT IT, REFUSE WHAT YOU CANNOT
                                ; TAKE BACK. This is the most destructive verb
                                ; in the table and the kernel cannot see what
                                ; it is about to remove: what a whole VOLUME
                                ; is, and what is protected, are questions
                                ; only your side can answer
FSV_SIZE     equ 30

; PUT A `%if` ON YOUR TABLE'S OWN LENGTH. A trailing backslash in a NASM
; comment is a LINE CONTINUATION, so an ASCII-art brace drawn down the right
; of a table of `dw 0` rows silently swallows the row below it: the table
; assembles one word short under -w+error, every verb past the gap is read
; from the wrong cell, and `call bp` in your dispatcher jumps into whatever
; follows. drivers/ramdisk shipped that and it presented as an unrelated
; window refusing to open (SPEC.md 62.9.4).

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
; the drive letter and the file manager all fall out of it. The fence on the
; ones that CHANGE something is the same identity test OSAPI_DRV_TASK uses:
; your segment must be the segment of the driver whose DISK services are
; published.
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
;   OSAPI_VOL_AT     in  DL = an int 13h drive number, BX:CX = a partition's
;                            32-bit base LBA (BX = the high word)
;                    out CF = 0 and AL = the volume index ALREADY covering it
;                        ASK IT BEFORE EVERY OSAPI_VOL_ADD. On a machine
;                        installed to its hard disk the kernel adopted the
;                        partition it booted from before you were loaded - it
;                        had to, your image being a file on that volume
;                        (SPEC.md 52.10.3) - and nothing in your own table
;                        says so, so mounting it again puts a second drive
;                        icon on the same sectors. Unfenced, changing nothing;
;                        it answers about BIOS-transport volumes alone,
;                        yours being yours to recognise
; The five slots themselves are published by os88api.inc, which this file
; includes: a slot number belongs in one place (SPEC.md 20.8).

; The listing claim a mounted volume wants (SPEC.md 22.6): DSK_VENT entries
; followed by the same number of 64-byte icon slots. Claim it before
; OSAPI_VOL_ADD and hand over the segment; pass 0 instead and the volume lists
; into the kernel's own 32-entry buffers, which works and shows fewer files.
; **Claim 6KB whatever the kernel's own stride is.** The entry a driver hands
; over through OSAPI_FS_ENT is DSK_DE_SIZE (32) bytes wide and always will be;
; what the kernel STORES in the claim is narrower (SPEC.md 19.1), so this
; number is a ceiling the kernel writes under rather than a layout to mirror -
; which is what keeps it off a driver's rebuild list.
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
    dw FSV_SIZE             ; +10 THE FSV_* TABLE LENGTH THIS DRIVER WAS BUILT
                            ;     WITH (SPEC.md 62.9.1.1). Every class writes
                            ;     it and only DRVC_FILE is read: the kernel
                            ;     refuses a verb past it instead of reading
                            ;     whatever follows your table. Was reserved
                            ;     (0), which the kernel reads as 28 - the
                            ;     length every driver before this word had
    db 0FFh, 0D5h           ; +12 THE DISPATCHER: `call bp`...
    db 0CBh                 ; +14 ...`retf`. The kernel far-calls this fixed
                            ;     offset with BP = the proc it wants.
    db DSV_SIZE             ; +15 THE DSV_* TABLE LENGTH THIS DRIVER WAS BUILT
                            ;     WITH (SPEC.md 51.2) - the FSV word's rule one
                            ;     level up. drv_publish copies only this many
                            ;     bytes and publishes 0 for the cells past it,
                            ;     instead of reading whatever you put after
                            ;     your table. Was the dispatcher's pad byte
                            ;     (0), which the kernel reads as 28 - the
                            ;     length every driver before it had
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
