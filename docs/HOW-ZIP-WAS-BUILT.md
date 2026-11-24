# How the Zip drive was built

A diary of `ZIP.DRV` (SPEC.md 138, `docs/plans/ZIP-PLAN.md`), kept as the work
happened. Observations and intentions are kept apart; what was tested is named.

## The ask (2026-11-23)

> Add Iomega Zip drive Parallel port driver so that we can read and write
> iomega zip disks through parallel ports. Use existing drivers for Linux or
> other operating systems to see how it is built.

The parallel Zip drive was the 1990s' way to give a machine with no SCSI card,
and often no hard disk, a 100 MB removable disk: plug it into the printer
port and go. That makes it a natural fit for an XT.

## What was found before any design

- **The protocol is documented by Linux, not by Iomega.** `ppa.c` (the Zip
  100's "PPA3"/VPI0 interface) says in its own comments that it is
  "based on a trace of what the Iomega DOS 'guest' driver does". `imm.c`
  covers the later MatchMaker (VPI2) in the Zip Plus and the Zip 250. Both
  speak ordinary SCSI once a command is on the wire, so a Zip disk is a
  SCSI direct-access disk reached through a printer port.
- **The drive needs no interrupt.** Everything is polled through the status
  register, which suits a driver that runs under the kernel's no-switching
  lock (`[sch_lock]`, SPEC.md 18.7) like `HDD.DRV`'s transfers do.
- **The XT can do it.** VPI0's slowest mode reads a byte as two nibbles
  through the status lines, which any output-only printer port supports.
  That is the mode an XT card gets.
- **One driver per class** (SPEC.md 51.2.1): `HDD.DRV` holds `DRVC_DISK`, so
  the Zip drive needs a class of its own to be attached beside a hard disk.
  `DRVC_JOY` = 11 was already reserved by the joystick work (PR #410), so the
  Zip drive took 12.
- **No emulator in the tree had a Zip drive on a parallel port**: not QEMU,
  not 86Box (its Zip drives are ATAPI and SCSI), not MartyPC. MartyPC did
  have an LPT card that a device can sit behind (the Covox, patch 10), so the
  instrument became a MartyPC patch: an emulated Zip drive written from the
  DRIVE's side of the same Linux sources, by a different agent than wrote the
  driver's side.

## Decisions

| decision | why |
|---|---|
| A block class, sectors not files | the kernel's FAT driver already reads FAT12/16 to 2 GB (SPEC.md 18.7.5); a Zip disk is FAT16 |
| The volume registered even with no disk in | an empty drive behaves like an empty A:, and a disk change is just the kernel's media-changed path (06h) |
| Medium learned on the first transfer | the drive tells us about a swap with UNIT ATTENTION; there is nothing to poll |
| Partition entry 4 scanned, not only 1 | IBM-formatted Zip disks put their FAT16 partition in entry 4 |
| Nibble and PS/2 only | EPP could not be seen working in any emulator here |

## Waves

*(Each agent's outcome, as integrated.)*
