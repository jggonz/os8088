; =============================================================================
; kerndos/kdlaunch.inc - THE LAUNCH BLOCK (docs/plans/KERN-DOS-PLAN.md §7 step 4)
;
; What the running os8088 tells `kern_dos` about the program it is handing
; over, staged in the text framebuffer by the box and copied into `kern_dos`'s
; own image by the stub (§7 step 7).
;
; **IT IS A GATHER/SCATTER OF THE BOX'S OWN BSS, and that is the whole trick.**
; Both sides `%include "dos.asm"`, so `dos_name`, `dos_args`, `dos_ebuf` and
; the rest are the same offsets FROM `DOS_CBASE` in the box and in
; `kern_dos` - the images differ, the DBSS layout cannot. So the block is a
; list of (offset, length) rows that one side gathers from and the other
; scatters to, and adding a field is one line HERE rather than two copies of
; marshalling code that drift.
;
; The two sides emit the list separately and neither trusts the other's: the
; block carries the payload length, each side sums its own, and a mismatch is
; refused at the entry rather than scattered into the wrong fields. That is
; eight bytes against a machine where a DOS program starts with somebody
; else's environment in its PSP and no way to notice.
;
; **THE MAGIC IS NOT DECORATION EITHER.** The block lands in a segment the
; stub chose, and on the arm this wave builds there is nothing to come back
; to: a `kern_dos` that ran on a block nobody staged would build a PSP out of
; whatever the adapter's RAM held and hand it to a program. `KDL_MAGIC` is
; the one cheap thing that cannot happen by accident.
; =============================================================================

KDL_MAGIC   equ 0               ; dd 'KDL1'
KDL_VER     equ 4               ; dw  KDL_VER_NOW - bumped when a ROW MOVES, not
                                ;     one is added at the end
KDL_FLAGS   equ 6               ; dw  KDLF_*
KDL_TLEN    equ 8               ; dw  the payload's bytes, as the SENDER
                                ;     counted them
KDL_CAP     equ 10              ; dw  KB the machine has (the BDA's, banked by
                                ;     the box because kern_dos's own int 12h
                                ;     would answer the same and this is the
                                ;     one place a cap can be imposed)
KDL_UNIT    equ 12              ; dw  THE UNIT `int 19h` IS TO BOOT FROM, and
                                ;     like KDL_DPT it is the KERNEL's field:
                                ;     `hbm_dosrun` patches the boot volume's
                                ;     own int 13h unit into the staged block.
                                ;     The bootstrap takes no documented input
                                ;     and GLaBIOS reads the drive out of DL as
                                ;     it finds it (SPEC.md 96.40.2), so a 0
                                ;     here is drive A: - which is right on the
                                ;     machine 9 of docs/plans/KERN-DOS-PLAN.md
                                ;     is about and is an EMPTY DRIVE on the one
                                ;     8 is about. It read as the ROM's boot
                                ;     menu waiting for a key
KDL_MOUBASE equ 14              ; dw  THE SERIAL MOUSE'S UART BASE, 0 = this
                                ;     machine has none that `kern_dos` can
                                ;     drive (SPEC.md 96.45). KDL_UNIT's field
                                ;     and not the box's: `mouse_init` spends
                                ;     596 ms at boot deciding which port has a
                                ;     mouse and which LINE it actually fires,
                                ;     against a modem on the other one
                                ;     (SPEC.md 9.5), and that contest is not
                                ;     reconstructible from anything a package
                                ;     can see - so `hbm_dosrun` patches the
                                ;     answer into the STAGED block beside the
                                ;     boot unit and the DPT.
                                ;     **IT WAS `KDL_RSVD` AND THE SPARE IS
                                ;     WHY NO VERSION BUMP IS OWED**: KDL_VER
                                ;     moves when a ROW MOVES, and filling a
                                ;     reserved word in the header moves none.
                                ;     KDL_TLEN covers the gathered BODY alone,
                                ;     so both sides still sum the same list
KDL_MOULINE equ 27              ; db  ...and its bit in the master 8259's mask -
                                ;     `mouse.inc`'s MOU_L4 (0x10) or MOU_L3
                                ;     (0x08). **AT 27 AND NOT 15**, which is
                                ;     the free byte the DPT leaves (16..26)
                                ;     rather than the base's OWN HIGH BYTE:
                                ;     a word at 14 occupies 14 AND 15, so a
                                ;     line mask at 15 wrote 0x10 over the top
                                ;     of 0x03F8 and kern_dos drove a UART at
                                ;     0x10F8. The low byte still looked right,
                                ;     which is what made it read as a mouse
                                ;     that was simply never there.
                                ;     CARRIED RATHER THAN DERIVED from
                                ;     the base, because a card at 2F8 jumpered
                                ;     to IRQ4 is a machine this project has
                                ;     met (SPEC.md 9.5.2.1) and the kernel
                                ;     learned which line fires by watching
                                ;     packets arrive on it
KDL_DPT     equ 16              ; db[11] THE DISKETTE PARAMETER TABLE, and it
KDL_DPT_LEN equ 11              ;     is the KERNEL's rather than the box's -
                                ;     the one field below that no package can
                                ;     fill, so `hbm_dosrun` patches it into the
                                ;     STAGED copy after the box has gathered
                                ;     the rest. SPEC.md 18.92: `int 1Eh` is a
                                ;     POINTER the BIOS re-reads on every floppy
                                ;     operation, os8088 points it at its own
                                ;     patched copy at boot, and `kern_dos`
                                ;     lands on the same segment with that copy
                                ;     at a DIFFERENT offset - so a handover
                                ;     that does not carry these eleven bytes
                                ;     leaves the ROM reading whatever kern_dos
                                ;     happens to have there. It reads as "the
                                ;     disk could not be mounted", which is a
                                ;     true sentence about the wrong thing.
                                ;     ZERO = leave the vector alone, which is
                                ;     the W4 gate's case: it boots through a
                                ;     loader that never touched it
; --- THE VOLUME TABLE, WHICH IS THE KERNEL'S AND WAS HARD-CODED (96.46) -----
; `kern_dos` carried a STATIC `dsk_vtab` - A: and B: as BIOS units 0 and 1,
; every other row free - and that is an assumption about a table os8088
; BUILDS AT BOOT. What the initialiser is FOR is being the starting point a
; boot probe then edits; over here nothing ever edits it. So:
;
;   - THE HARD DISK IS NOT THERE AT ALL. SPEC.md 18.7.1 pins the boot
;     partition at ROW 2 (`dsk_flop_add` skips that row so a floppy cannot
;     take it first), and row 2 of the initialiser is DVK_FREE - so a machine
;     with a fixed disk hands `kern_dos` a volume index it reads as no volume;
;   - AND ROW 1 IS LIVE ON A MACHINE WITH NO B:. `dsk_fdd_retire` frees it in
;     the kernel when the probe finds no second drive (SPEC.md 18.97), and
;     nothing frees it here - so drive B: exists, answers, and is a unit the
;     ROM has nothing on.
;
; The volume INDEX is the identity DOS drive (SPEC.md 96.6) and the box hands
; over `dos_vol` as an index, so an index resolved against a different table
; is a program reading a drive that is not its own. Carrying the table is
; what makes the identity true rather than hoped for.
KDL_NVOL    equ 28              ; db  rows that follow, 0 = LEAVE THE BUILT-IN
                                ;     TABLE ALONE. That is the W4 gate's case
                                ;     (kerndos/kdosgate.inc stages its own
                                ;     block and knows no volumes) and it is
                                ;     KDL_DPT's zero one field along: a
                                ;     sender that cannot fill it says so, and
                                ;     is not mistaken for one that filled it
                                ;     with zeros - which here would read as
                                ;     eight copies of BIOS unit 0
KDL_BOOTV   equ 29              ; db  ...and which index the KERNEL booted
                                ;     from, for `[dsk_bootvol]`
KDL_HDSPT   equ 30              ; dw the FIXED DISK's sectors per track, and
KDL_HDHDS   equ 32              ; dw its heads - `int 13h AH=08h`'s answer,
                                ;    which `dsk_boot_from_x` asks ONCE at boot
                                ;    and banks. It is in the BOOT OVERLAY, so
                                ;    `kern_dos` does not carry the question -
                                ;    and the mount that needs the answer is
                                ;    the read that FETCHES the BPB, so it
                                ;    cannot come from there either. Zero
                                ;    leaves the floppy fallback and the mount
                                ;    fails honestly (kernel/disk.inc), which
                                ;    is what a hard disk did here before this
KDL_VTAB    equ 34              ; the rows themselves, KDL_VROW bytes each
KDL_VROW    equ 6               ; db kind, db unit, dd the partition base
KDL_VMAX    equ DVOL_CAP        ; DVOL_MAX's widest arm (kernel/disk.inc), so
                                ; the block's shape does not depend on which
                                ; kernel staged it - which is exactly what
                                ; `apps/dos/doscall.inc` calls DVOL_CAP, so it
                                ; is that and not a second literal 8 (SPEC.md
                                ; 96.44.2.1: two copies of one fact is how the
                                ; core's bss came to be laid out two ways)
; **ONLY THREE FIELDS OF SIXTEEN COME OVER**, and the rest are not an
; oversight: `DV_FLAGS` bit 0 is a DESKTOP ZONE and there is no desktop,
; `DV_CLASS` is 0 on every BIOS row, `DV_SECS` and `DV_SEG` are what a MOUNT
; fills, and `DV_LBL` is a label nothing here draws.
;
; **A `DVK_DRV` ROW COMES OVER AS `DVK_BIOS` WHEN - AND ONLY WHEN - THE ROM
; CAN REACH IT** (SPEC.md 96.46.1). The old rule was that it could not come at
; all, *"there is no driver layer on the other side"*, and that is true of the
; DRIVER and not of the DISK: `DSV_GEOM` sub-function 2 answers CF=1 for a
; volume `int 13h` cannot reach and a unit, a base and a geometry for one it
; can, so the ones that answer are BIOS volumes as far as this host is
; concerned and the ones that do not are still written DVK_FREE. That is
; KERN-DOS-PLAN 6.1 lever 4 asked as a QUESTION rather than assumed, and it is
; the same question SPEC.md 87.5 step 1's stub has always asked. `DVK_FILE`
; still cannot come: a redirected volume has no sectors at all (SPEC.md 62.9).

; --- AND WHAT A LIVE RESUME NEEDS (SPEC.md 96.49) ---------------------------
; `kd_leave` puts the session back itself instead of `int 19h`-ing and letting
; a fresh boot do it. Four things only the KERNEL knows, and none of them is
; reconstructible on a machine it is no longer running on.
KDL_WAKE    equ 82              ; dd where the restored kernel comes back in -
                                ; `hbm_wake` as off:seg, the same pair the
                                ; hibernation POINTER carries. Zero = a kernel
                                ; that does not offer a live resume, which
                                ; `kd_resume` reads as "reboot instead"
KDL_KSEG    equ 86              ; dw ...and the DS it comes back WITH, which
                                ; is NOT the segment above: that one is the
                                ; hibernate module's CS. The stub used to
                                ; spell this as `KERNEL_SEG`, an assembly
                                ; constant that means 0x0060 in this host
KDL_HBVOL   equ 88              ; db the volume HIBERNAT.IMG is on - `hb_pick`'s
KDL_CYLRUN  equ 89              ; db MAY A TRANSFER RUN CROSS A HEAD on this
                                ; machine? (SPEC.md 96.44.14) 1 = the KERNEL's
                                ; loader crossed one and its canary said the
                                ; bytes came back right (18.93.1); 0 = every
                                ; other case, which is the cautious answer and
                                ; the one an absent field gives. kern_dos has
                                ; no loader and no canary, so it cannot ask
                                ; this question itself and its own
                                ; `boot_cylrun` is never written - carrying the
                                ; finding is what keeps 18.91.1's cylinder run
                                ; on the machines that earned it. It was
                                ; KDL_HBPAD, a pad nothing read, so the word
                                ; below still starts even and no row moved
KDL_CLK     equ 90              ; db[10] the kernel's OWN clock block, because
                                ; `hbm_wake` copies these ten into `clk_sec`
                                ; and two of them are `clk_rtc`/`clk_dirty` -
                                ; whether a usable RTC was found and whether it
                                ; is owed a write (kernel/clock.inc). kern_dos
                                ; overwrites only the TIME fields, from the
                                ; ROM; a zeroed block would tell the resumed
                                ; machine it has no clock hardware
KDL_CLK_LEN equ 10
KDL_TICK    equ 100             ; dd ...AND THE BIOS TICK IT WAS READ AT, which
                                ; is the only clock that runs on BOTH sides of
                                ; the handover (SPEC.md 96.49.5). 0040:006C
                                ; advances at 18.2065 Hz under os8088 and under
                                ; this host alike - SPEC.md 8.1's PIT change is
                                ; a MODE change and `sch_isr` chains the ROM
                                ; every tick - so the difference against it says
                                ; how long the DOS program ran, and `hbm_wake`
                                ; hands that to `clk_tick`. Without it the
                                ; resumed clock is the launch's, late by the
                                ; whole session
KDL_STAGE   equ 104             ; dw WHERE THE KERNEL WILL LOOK for everything
                                ; staged on the way back - `hbm_stageseg`'s own
                                ; answer, out of `[vid_kind]`. `kd_stageseg`
                                ; asks the BDA's mode byte instead, and that
                                ; byte belongs to the DOS PROGRAM: a `.COM`
                                ; that sets mode 2 on a Hercules machine makes
                                ; the two hosts disagree, and everything staged
                                ; is then written to a page the kernel never
                                ; reads (SPEC.md 96.49.6). 0 = not carried, and
                                ; `kd_resume` asks for itself

; **THESE TWO ARE `%assign` AND THE REST ARE `equ`**, which is not a style
; slip: the fit assert below runs at PREPROCESSOR time and `%if` cannot see an
; assembler symbol. A `%assign` reads the same everywhere an `equ` does and
; loses only its line in the map file.
%assign KDL_BODY 106             ; ...and the gathered fields from here. It was
                                ; 100 until KDL_TICK and KDL_STAGE, which is a
                                ; ROW MOVING and so a KDL_VER bump - see the
                                ; note under KDL_VER_NOW
%assign KDL_SIZE 512            ; one sector, which is what the stub moves and
                                ; what `kern_dos` reserves in its own image.
                                ; The payload is 377 bytes today; the assert
                                ; each side writes is what keeps it true

KDL_SIG     equ 0x314C444B      ; 'KDL1', little-endian
KDL_VER_NOW equ 5               ; what this tree writes and checks. It is a
                                ; CONSTANT and not a literal at three sites,
                                ; because a version that has to be bumped in
                                ; three files is a version that gets bumped in
                                ; two. 1 -> 2 is KDL_DPT moving the body,
                                ; 2 -> 3 is the volume table doing it
                                ; again (96.46), 3 -> 4 the live
                                ; resume's four fields (96.49) and 4 -> 5 the
                                ; two that make its CLOCK and its STAGING
                                ; SEGMENT right (96.49.5, 96.49.6) - both in
                                ; the header, so the body moved 100 -> 106.
                                ; **THE BUMP IS OWED EVEN
                                ; THOUGH BOTH SIDES SHIP IN ONE FILE**:
                                ; kern_dos is part 2 of DOS.O88 and the box
                                ; is part 0, so those two are always in step -
                                ; but the third writer is the KERNEL, which
                                ; patches the header and is a different
                                ; artefact on a different disk. An old
                                ; DOS.O88 under a new kernel is exactly what
                                ; this word refuses

KDLF_REBOOT equ 1               ; bit 0: `int 19h` when the program exits.
                                ; docs/plans/KERN-DOS-PLAN.md §9 - there is no
                                ; hibernate image to come back to, so the
                                ; machine restarts. W6 is the other arm and
                                ; this bit is where it hangs
KDLF_HIBER  equ 4               ; bit 2: THE KERNEL HAS WRITTEN A HIBERNATION
                                ; IMAGE, so the exit code has somewhere to go
                                ; (docs/plans/KERN-DOS-PLAN.md 8). It is not
                                ; the box's to set and never arrives from it:
                                ; `hbm_dosrun` patches it into the STAGED
                                ; block, because whether there is a fixed disk
                                ; to come back to is `hb_pick`'s question and
                                ; the kernel is the only side that can ask it
KDLF_NOMOUSE equ 8              ; bit 3: **THE USER DOES NOT WANT A POINTER**
                                ; (SPEC.md 96.36.8). `KDL_MOUBASE` is the
                                ; KERNEL's field and a package cannot zero it
                                ; by writing there - `hbm_dosrun` patches the
                                ; word AFTER the box has gathered the block -
                                ; so the box says so in a bit and the kernel's
                                ; patch is what tests it. Zero base is already
                                ; "this machine has none that kern_dos can
                                ; drive", so the refusal needs nothing new on
                                ; this side: `kd_mou_start` reads the same 0
                                ; either way and INT 33h answers 96.10's still
                                ; pointer.
                                ;
                                ; It is worth a bit because the pointer is not
                                ; free: `mou_isr` draws the arrow out of the
                                ; interrupt on every packet, which on a
                                ; 4.77 MHz machine a DOS program can feel, and
                                ; a program that never asks INT 33h anything
                                ; is paying for a cursor nobody reads
; **WHY THE PROGRAM DID NOT RUN** (SPEC.md 96.40.7), in KDH_CODE's HIGH byte.
; Zero means the low byte is a real exit code, which is what every program that
; actually ran reports; this bit set means the low byte is MEANINGLESS and the
; other seven carry the reason.
;
; It exists because every refusal in `kd_entry` reported 0xFF and nothing else.
; `kd_puts` wrote the sentence to the DOS SCREEN, which the user never sees -
; `kd_leave` hands straight back - so `NOSUCH.COM` that was never there came
; home as `ended, exit code 255` and read as a program that ran and failed.
; 255 cannot be the signal either: it is a legal `INT 21h AH=4Ch` code.
;
; **THE REASON IS A `DER_*`, the BOX's own**, and not a code of kern_dos's to
; be translated. `apps/dos/dos.asm` defines them ungated, so this build sees
; them and the window half needs no mapping table - one `and` and a store into
; [dos_err], and `dos_err_line` says the sentence it already had. The DER_*
; range is 0..8, so `KDC_FAIL | DER_*` can never collide with KDH_NOCODE.
KDC_FAIL    equ 0x80
KDLF_RAHSH  equ 8               ; bits 8..10: **HOW MUCH READ-AHEAD THE PROGRAM
KDLF_RAHM   equ 0x0700          ; DOES NOT GET** (SPEC.md 96.36.6), as
                                ; runs + 1, so that ZERO is "kern_dos's own
                                ; KD_RAH_KEEP" and 1..8 are widths 0..7. The
                                ; +1 is what makes the absent field mean the
                                ; default rather than "shed the lot", which is
                                ; KDL_NVOL's rule and KDL_DPT's: a sender that
                                ; cannot fill a field must not be mistaken for
                                ; one that filled it with zeros.
                                ;
                                ; IN THE FLAGS WORD AND NOT A ROW OF ITS OWN:
                                ; the header runs to KDL_BODY with no spare
                                ; byte in it, and KDL_VER moves when a ROW
                                ; MOVES - filling reserved bits of a word that
                                ; is already there moves none, which is
                                ; exactly KDL_MOUBASE's own precedent
KDLF_EXE    equ 2               ; bit 1: the box already knows it is an .EXE.
                                ; A HINT and not an instruction - kern_dos
                                ; still asks `dos_is_exe`, which reads the
                                ; FILE (SPEC.md 96.8) - carried so the two
                                ; answers can be compared on the glass

; -----------------------------------------------------------------------------
; KDL_FIELDS - the list, ONCE.
;
; ORDER IS THE WIRE FORMAT. Append, never insert, and bump KDL_VER if you must
; move one.
; -----------------------------------------------------------------------------
%macro KDL_FIELDS 0
    KDL_F dos_name,    16                       ; the program's 8.3 name
    KDL_F dos_args,    DOS_ARGSZ                ; ...and its command tail
    KDL_F dos_ebuf,    DOS_ENVN * DOS_ENVBUF    ; the user's environment rows
    KDL_F dos_blaster, 32                       ; and the machine's own row
                                                ; (SPEC.md 96.17) - the sound
                                                ; driver is unloaded by the
                                                ; time this is read, so this
                                                ; string is the ONLY record
                                                ; left of where the card was
    KDL_F dos_dir,     2                        ; the folder it was launched
    KDL_F dos_vol,     1                        ; from, and that folder's drive
    KDL_F dos_memkb,   2                        ; the arena cap, 0 = all of it
%endmacro

; -----------------------------------------------------------------------------
; **A CONSUMER DEFINES `KDL_F` AND INVOKES `KDL_FIELDS`.** Two of them do, and
; neither is a macro that writes a macro: nasm cannot nest those without
; escaping every parameter, and the five lines each side writes read better
; than the escape would.
;
;   ; the table, for the walk
;   %macro KDL_F 2
;       dw (%1 - DOS_CBASE), %2
;   %endmacro
;   kd_lbtab:
;       KDL_FIELDS
;       dw 0, 0
;   %unmacro KDL_F 2
;
;   ; ...and this side's own idea of the payload's length
;   %assign KDL_ACC 0
;   %macro KDL_F 2
;       %assign KDL_ACC KDL_ACC + %2
;   %endmacro
;   KDL_FIELDS
;   %unmacro KDL_F 2
;   KDL_MINE equ KDL_ACC
;
; `KDL_ACC` is `%assign` and not `%define` because the sum is arithmetic: a
; `%define` of itself plus a term is a recursive expansion, not an addition.
; -----------------------------------------------------------------------------
