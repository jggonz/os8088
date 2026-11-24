# ZIP - an Iomega Zip drive on the parallel port (SPEC.md 138)

The owner's brief (2026-11-23):

> Add Iomega Zip drive Parallel port driver so that we can read and write
> iomega zip disks through parallel ports. Use existing drivers for Linux or
> other operating systems to see how it is built. Use a worktree.

The contract is SPEC.md 138; the transport contract is
`drivers/zip/ziptrans.inc`. The build diary is `docs/HOW-ZIP-WAS-BUILT.md`.
The protocol reference is Linux's `drivers/scsi/ppa.c` (VPI0, the Zip 100)
and `drivers/scsi/imm.c` (VPI2, the Zip Plus and Zip 250), read for the
protocol and never translated.

Built by fan-out (`/story-fanout`): five agents at once from the foundation
commit, each in its own worktree. One QA run in emulation follows the last
merge, then the fix round, then the final pass once.

Status: ` ` not started, `B` built on its branch, `I` integrated into
`feat/zip`, `S` seen working in the QA run, `X` deferred.

## Wave 1 (all five at once)

| | story | done means | owner |
|---|---|---|---|
| [ ] | **Z1 - the class** (SPEC 138.1). `DRVC_ZIP` = 12, `DRVC_MAX` 12 with 11 held for `DRVC_JOY`; a block class in `dsk_vol_add`; removable in `dsk_vol_fixed`; 06h on the driver arm shuts the mount gate; `OSAPI_VOL_DEL` writes back first; the `drv_tab` row (bit 14, "Zip Drive", `ZP` pin blob, `DRVM_IMG_ZIP`), the DOS skip list, the catalog entry; kern_big, kern_emu and kern_mega | `make`, `make emu`, `make mega`, `make KERN_SMALL=1` byte-identical, kernsize on all, `make budget-check`, no rung crossed; a QEMU boot whose Drivers page lists "Zip Drive" (missing file is fine until Z2 lands) | kclass |
| [ ] | **Z2 - the driver core** (SPEC 138.3-138.7). `drivers/zip/zip.asm` and its includes: attach (port list, bidirectional test, VPI0 then VPI2 probe, INQUIRY), detach, the SCSI layer, `DSV_BLK` with chunking and the length fence, the lazy medium, media change and write protect, the page, `ZIPV_INFO`/`ZIPV_EJECT`; `ZIP.DRV` built and on every driver kernel's system disks | `make`, `make mega`; the image under its budget; a QEMU boot with the row ticked refuses cleanly with `No drive` (QEMU has no Zip) and leaves the printer port idle | core |
| [ ] | **Z3 - VPI0** (SPEC 138.2.1). `drivers/zip/zipppa.inc`: probe, command, reset; nibble and PS/2 | assembles into the core (or a scratch harness before Z2 lands); under 1,024 bytes; cycle estimate of the nibble read loop in 138.2.1 | ppa |
| [ ] | **Z4 - VPI2** (SPEC 138.2.2). `drivers/zip/zipimm.inc`: the same three procs for the MatchMaker | assembles; under 1,280 bytes; cycle estimate in 138.2.2 | imm |
| [I] | **Z5 - the instrument** (SPEC 138.8). MartyPC patch 18: a VPI0 and VPI2 Zip drive on the LPT card over a host image, a bidirectional LPT option, debug-server verbs, `tools/os88marty.py` helpers, Zip machine configs; `tools/os88zip.py` and its unit test; the soak rows written and registered | `make marty` builds; the model's own Rust tests replay Linux's host sequence (probe, TEST UNIT READY, READ(10), WRITE(10)) against it for both protocols in both modes; `t_zipimg` green in the fast tier | marty |

## After the last merge

| | story | done means |
|---|---|---|
| [ ] | **Z6 - QA in emulation**: MartyPC XT with a VPI0 drive (nibble), a 5160-class with VPI2 (nibble), a bidirectional-port machine (PS/2); QEMU for `No drive` on VGA, EGA, CGA and Hercules | every claim below seen, screenshots cropped and zoomed, defects listed in *QA* |
| [ ] | **Z7 - the fix round, the rows once, `test-full` once** | rows green, `test-full` green, `budget-check` exact |
| [ ] | **Z8 - the guide**: a user's page on the Zip drive (`apps/guide` manual) and the README's hardware list | written to what was seen, not what was planned |

The claims Z6 checks, end to end through the real UI:

1. Tick *Zip Drive* in the Drivers page: the row loads, a `Zip` zone appears
   on the desktop (D: or later), the page says `VPI0 nibble` and `96 MB`.
2. Open the zone: the disk's root lists; a text file opens in an editor and
   reads as the host wrote it (`tools/os88zip.py` is the reference).
3. Save a file to it, then read it back on the host with `tools/os88zip.py`,
   byte for byte; copy a 200 KB file both ways.
4. Eject from the page: the window closes, the zone shows an empty drive;
   insert another disk (debug verb): it mounts with ITS files, not the old
   disk's FAT.
5. A write-protected disk: open works, a save says write-protected.
6. No drive on the port: the row refuses `No drive`, nothing hangs, and a
   printer on the port would still print (the port is left disconnected).
7. The same on a VPI2 drive, and in PS/2 mode on a bidirectional port.
8. A superfloppy disk and a Zip 250 disk (VPI2) mount.
9. On the XT: the time to read and to write 64 KB, written in 138.9.

## QA

*(Filled by the QA run.)*

## Deferred, and why

| | item | why |
|---|---|---|
| X | EPP and ECP modes | no emulator here models an EPP port with a Zip behind it; nibble and PS/2 cover every port, the XT's included |
| X | Two drives, or a printer daisy-chained behind the drive while the driver is attached | one drive is the brief; the probe already leaves the port printer-idle |
| X | Booting from a Zip disk, formatting one | not in the brief; a format is `FORMAT.DRV`'s and would need a SCSI FORMAT UNIT |
| X | A desktop icon of its own for the Zip zone | the removable icon is used; new art is a separate small story if the owner wants one |
