# How INSTALL was built

A diary kept while the work happened, for later reuse. Observations and
intentions are kept apart; anything called *tested* names what ran it. The
contract is SPEC.md §117 and the stories are `docs/plans/INSTALL-PLAN.md`.

## The ask (2026-10-20)

A *smart install* package called INSTALL, on the system disk's root, that
switches the hard-disk driver on, finds the drives, and walks a person through
putting os8088 on one — or upgrading what is there — using the same flow the
Control Panel's installer already runs.

## Day 1 — reading before designing

**Observed.** The Control Panel's installer is not a thin dialog over a small
routine: `drivers/hdd/inst.inc` is 4,385 lines and §52.10 is about two
thousand lines of SPEC, most of it closed field defects — a booted-from volume
offered as a target, a kernel written behind another build's boot sector, an
apps copy that threw away a finished system install, images that could not be
used as sources. Any second implementation of that walk would have to
rediscover every one of them.

**Observed.** Every drawing call in the installer goes through `hd_iorg`, which
answers *no window* and draws nothing when the Install window is not on
screen. The engine was already headless; it only lacked a caller.

**Observed.** Two doors were missing. A package has no way to switch a driver
on — the Drivers page's tick runs `cdl_load` inside `CTRL.DRV`, and the attach
half of a load is deliberately not resident. And `HDD.DRV`'s package door
reaches two XMS verbs and nothing else, in a driver whose last byte of data is
at 4,092 of a 4,096-byte rung.

**Decided.** INSTALL owns pages and words and nothing else. It reaches the
engine through one new kernel cell (`OSAPI_DRV_USE`, the tick as a door) and
one new `HDD.DRV` verb (`HDV_TOOL`, a forwarder into `HDDTOOL.DRV`), and the
forwarder's bytes are found inside the driver rather than paid for with a
fifth kilobyte on every hard-disk machine.

**Decided.** The door loads and does not record: no `SYSTEM.CFG` answer is
written for a user who never answered the Drivers page.

## The doors, built (wave 1)

**Measured.** `OSAPI_DRV_USE` is a six-byte rare cell and a `.cold` body;
kern_big paid 77 bytes at first, no rung crossed, and kern_small six (the cell
alone). `HDD.DRV` stayed at 4,096 bytes: its XMS arm had been turning a
segment into a 32-bit linear address with two shift pairs, and keeping the
segment and letting the 386 half do the shift in one instruction paid for the
forwarder with bytes to spare. `hd_mbr_end` did not move.

**Learned on the way.** `.cold` may not name a segment at all, its own
included — `push cs` and `call COLD_SEG:` were both refused, by
`os88ovlchk` and then by the ROM builder, because cold code can run from ROM
or the HMA. The way to call a far-returning cold routine from cold code was to
give it a near body (`drv_cp_closed`) under a four-byte far entry.

## The first look (wave 2)

**Observed, QEMU, blank 32 MB disk.** The desktop showed no C:: `HDD.DRV` had
loaded itself at boot because the BIOS knows a disk, found nothing to mount,
and taken itself back down — the commonest install target is exactly the case
the driver door exists for. The wizard's first Next switched it on through
`OSAPI_DRV_USE` and `HDI_DEVS` found `Disk 1  31 MB  BIOS drive 80h`.

**Two defects the first boot found.**

1. *The window never appeared and the machine spun on one `div`.*
   `OSAPI_VOL_SYS` answers in BL and the page builder uses BX; between them
   they replaced the window handle, `os88ui_btninit` installed callbacks on a
   window that did not exist, and `wm_ptr2idx` divided a garbage pointer by
   the record size. The register contract was in the SDK comment; the entry
   proc had not read it.
2. *The install stopped on `KERNEL.SYS`, after the format.* The engine ran,
   formatted the slot, mounted it as C: — and wrote `KERNEL.SYS` into
   INSTALL's own folder on the floppy. §19.2.1 resolves every file name in
   the CALLING INSTANCE's folder, and a call through `OSAPI_DRV_CALL` is still
   INSTALL's call. The kernel's own entries into a driver have cleared that
   stamp since §51.2.3; the package door never needed to. Fixing the door for
   everyone would have changed what SOUND.DRV's, the sockets', EMS's and the
   RAM disk's verbs are answered as, so the fix is a third verb on the new
   cell, `DRVU_CALL`: the door, asked as the machine, and only by the task
   that owns the stamp. The wizard's own KERNEL.SYS probe had the same defect
   in miniature and now uses `OSAPI_FILE_GOTO_QM`, which moves the instance.

**Then it worked, end to end, in QEMU:** erase-install from the 1.44 MB pair
(the apps disk in B: was found by the engine, one confirm); boot from C:;
INSTALL from C: offered the UPGRADE — `C: FAT16, Booted From`, Erase greyed,
*os8088 is on it already* — kept the files, and Restart came back up from the
upgraded disk; on CGA every page fits the 640 × 157 band and the greyed
choices read as the 1bpp checkerboard; Escape cancels and closes; with no
apps disk in the machine the wizard stops on its Apps page, and swapping the
apps disk into A: and pressing Continue put 22 programs and 15 games on the
disk (112 paths, read back on the host with `tests/instdeep.py`'s reader).

**Open, and recorded rather than chased.** Swapping the apps disk into **B:**
in QEMU (`change floppy1`) was not seen: the apps phase found no apps disk,
fell back to the banked volume as §52.10.5.1 says it must ("a miss may not
become a refusal"), and copied the system disk again. Every image here carries
the same pinned volume serial (`os88disk.py`), which is one candidate; it is
the engine's behaviour from the Control Panel as well, and the real
single-floppy flow swaps A:, which works — so the wizard says *drive A:*.

## The gate (wave 3) and the final pass

**Built.** `tests/install.py` drives the wizard on MartyPC's XT-IDE XT by its
own buttons, reading the page after every click out of the package's memory
(the build now writes `build/install.map`): Erase over the DOS fixture with
the Down key, both disks copied, the partition read back on the host; Close,
and HDDTOOL.DRV's claim given back; a user file planted with mtools; boot C:,
the wizard again - UPGRADE, os8088 found and picked, Erase greyed - and with
no apps disk in the machine, Skip; the file kept, the kernel the floppy's,
the MBR rewritten; boot C: again.

**Measured, the final pass (2026-10-20).** 16 of 17 soak rows passed,
`install` among them; `instimg` failed before its emulator started, because
its fixture cannot add a file to an `apps360.img` that is full on `main` too.
`test-full` 60 passed, 1 skipped (no C compiler here). Budgets exact on all
three kernels.

**A small joke kept.** README.TXT must pack to exactly 8,088 bytes; the new
hard-disk paragraph was tuned until it did.

## Wave 4 — "so that we can see everything" (2026-10-21)

**The ask.** The owner installed onto 86Box's ST-225 XT (`make xt-mfm`, a
blank 20MB image) - it worked, 40 files and an active partition read back on
the host - and then asked for the Installing step to be verbose: *"so that we
can see everything that is happening behind the scenes."*

**The constraint that shaped it.** The install is ONE call. `HDI_GO` runs the
whole system phase - and the apps phase too when the apps disk is already in
B: - inside the wizard's button handler, under the frozen interface (§52.10.4).
Nothing the wizard draws after the call returns can show what happened during
it. So the engine has to hand the wizard each line *as it starts the step*,
and the wizard has to draw it there and then, from inside the engine.

**The decision: a callback, not a drawing engine.** The package boundary
already has a shape for "call a package proc": far to `segment:PKG_DISP` with
BP = the proc and DS switched (§20.2) - which is how the kernel calls every
`W_PAINT`. The engine uses exactly that, with the line copied into the
package's own block so no pointer crosses a segment. The alternative - the
engine drawing into the wizard's window - was rejected: the wizard would not
own its pixels, and a repaint after the call would have nothing to repaint
from.

**What the Control Panel pays: nothing it can see.** Its Install window runs
the same engine and never arms the hook; every log site is a compare and a
branch, and the hook is cleared at the end of every verb, so a Control Panel
run can never reach a package's code.

**Two defects on the way, both found by looking.**

- *Every file in a folder was 32 KB.* The indent routine left `' '` (32) in
  AL - the register holding the size. Root files were right only because
  they have no indent.
- *The first fix did not run.* `build/hddtool.bin`'s rule never listed
  `ipkg.inc`, the file wave 1 added, so `make` kept the old tool - and the
  floppy carried it - while every check stayed green. The rule names it now.

**The page.** Nine lines of 41 cells in a frame, each line one opaque run
padded to the band; once the box is full, one `OSAPI_GFX_SCROLL` and one run
a line. When the verb answers, the wizard adds its own last line, the heading
becomes *Finished - click Next* (or *It stopped*), and Next goes on to Done,
the apps-disk page or Stopped. That extra click is the point: the end of the
log is what the owner wants to read.

**Measured.** An erase-install on MartyPC's XT-IDE XT logs 67 files, 711 KB.
`HDDTOOL.DRV` grew 672 bytes, which crosses its claim from 17 to 18KB (only
while the tool is loaded); INSTALL grew 385 bytes of image and 428 of bss;
no kernel byte.

