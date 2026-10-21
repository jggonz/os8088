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
