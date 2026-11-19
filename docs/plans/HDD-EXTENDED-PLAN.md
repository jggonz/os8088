# HDD-EXTENDED-PLAN — logical drives, and disks past 504MB and 8GB

Issue #386: an XT with an XTIDE CF card, partitioned by MS-DOS's FDISK, shows
C: and D: under DOS and only C: under os8088. D: is a logical drive inside an
extended partition, and `HDD.DRV` read only the four primaries.

The contract is SPEC.md §52.4.1 (logical drives), §52.1.5 (rung 1 past
504MB) and §52.1.6 (rung 0 past 8GB). This file is the story list and its
evidence. The third item that came out of the same discussion — FAT32 — is a
kernel project of its own: [FAT32-MEGA-PLAN.md](FAT32-MEGA-PLAN.md).

## Stories

- [x] **S1. A DOS-partitioned disk shows every letter DOS shows.** A primary
  C: and an extended partition (05h or 0Fh) with logical drives: each FAT12/16
  logical drive mounts as its own volume and desktop icon, after the
  primaries, up to six hard-disk volumes (the kernel's eight rows less the two
  floppies). §52.4.1.
  *Evidence:* `hdext` (QEMU 386): slots 0, 4, 5, parents 0, 1, 1, CALC.O88
  opened off E:. `hdextxt` (MartyPC 8088 + XT-IDE, the reporter's machine):
  the same through the card's ROM on rung 0.
- [x] **S2. A hostile chain cannot hang the boot or double a volume.** Links
  must move forward, the walk stops after 24 links, and a logical drive must
  end inside the extended partition. §52.4.1.
  *Evidence:* `hdextloop` — a link back to the first record; broken on
  purpose (forward test removed) it mounted six volumes over three partitions.
- [x] **S3. Delete and Format never strand a mounted logical drive.** Every
  volume inside an extended slot is that slot's (`HDV_PARENT`), and Format
  now unmounts its slot first, as Delete always did. §52.2.3, §52.4.1.
- [x] **S4. An IDE drive past 504MB is addressable on a 286 or better.**
  Rung 1's cylinder bound is the drive's own; past it, LBA mode on a drive
  whose IDENTIFY says it has it. §52.1.5.
  *Evidence:* `hdextlba` — a 1GB drive the BIOS reports as 1,024/16/63,
  taken by rung 1, its one partition past cylinder 1,023; broken on purpose
  (LBA arm refused) nothing mounts.
- [x] **S5. Lifting that bound never mounts a disk twice.** A BIOS that
  translates a big drive leaves it a BIOS row and an IDE row; the IDE row may
  pass cylinder 1,023 only when no unspent BIOS row shares its sectors per
  track (`HDF_XCYL`). §52.1.5.
  *Evidence:* `hdextebios` — broken on purpose (the scan skipped) the 9GB
  disk's one volume mounted twice, through both transports.
- [x] **S6. A partition past BIOS cylinder 1,023 is reachable through the
  ROM's packet calls.** AH=41h at attach, AH=42h/43h only where CHS cannot
  name the sector; never under kern_mega. §52.1.6.
  *Evidence:* `hdextebios` — 9GB, translated to 1,024/255/63, the volume at
  LBA 16,600,000 through row 0 with `HDF_EBIOS`.
- [x] **S7. The installer refuses a partition the BIOS cannot boot.**
  `Past BIOS Reach` for a slot ending past cylinder 1,023. §52.1.5.
  *Not covered by a row*: there is no emulator fixture here for an install
  past cylinder 1,023; the predicate is `hd_lba_chs`, which the S4/S6 rows do
  exercise through the mount path.

## Costs (the owner's decision, 2026-11-18)

`HDD.DRV`'s image 4,096 → **4,608** bytes: a ninth half-KB claimed on every
machine with a hard disk. `tests/unit/t_install.py`'s gate moved with it,
`DRVM_IMG_HDD` 8 → 9, `DRVM_CEIL_DISK` 4 → 5 (mirrored in the SDK; ABI lock
re-stamped, no bump — a quoted ceiling). `HDDTOOL.DRV` 16,153 → 16,665 while
it is open. **No kernel byte**, on `kern_big`, `kern_small` or `kern_mega`.

## What is not here

- Making logical drives in the disk tool. DOS's FDISK does that.
- Installing onto a logical drive. A BIOS boots a primary.
- Partitioning past 8.4GB: the tool sizes a disk from its CHS geometry.
- kern_mega's rung 0 packet calls: its int 13h reflection carries ES:BX, not
  DS:SI (§52.1.6). A ROM-only controller on kern_mega keeps the ten-bit reach.
