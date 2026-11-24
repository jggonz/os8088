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

Five agents started at once from the foundation commit (the contract in
SPEC.md 138, the transport contract in `drivers/zip/ziptrans.inc`, the story
list), each in its own worktree. Every one of them built against a CONTRACT,
not against the others' code: the two protocol agents never saw a drive, the
instrument agent never saw the driver, and the driver core never saw either
protocol until the merge.

**The instrument came first, and it is the reason the rest could be tested.**
No emulator models a Zip drive, so MartyPC patch 18 adds one: a VPI0 and a
VPI2 target on the LPT card over a host disk image, an optional bidirectional
port, and debug-server verbs to eject, insert and write-protect a disk from a
test. The model was checked against itself before anything ran on it: nine
Rust tests replay Linux's host sequence (probe, TEST UNIT READY, READ(10),
WRITE(10)) for both protocols in both port modes, and swapping the nibble
order turned two of them red. `tools/os88zip.py` writes the disks the rows use
(IBM's layout puts the one partition in entry 4, which a real Zip disk does)
and is the host's second reader for every byte the guest writes; macOS's own
`fsck_msdos` and a real mount agreed with it.

**The protocols were read, never translated.** `zipppa.inc` (VPI0, 904 bytes)
and `zipimm.inc` (VPI2, 1,203 bytes) were written from Linux's `ppa.c` and
`imm.c` for the WIRE behaviour - the pulse sequences, the phases, which
status bits mean what - and each agent listed its deliberate departures:
waits in BIOS ticks rather than microsecond spins (a probe of three empty
ports must not cost three seconds on an XT), a PS/2 check that reads
`IOMEGA` out of an INQUIRY rather than trying EPP, and a bounded wait where
Linux reschedules forever. Both listed the protocol points they were unsure
of, for the QA run to check first. Neither needed a change when it met the
drive.

**The kernel side cost 65 resident bytes and one decision.** A new driver
class (`DRVC_ZIP` = 12) is a publication slot, a row in the driver table, a
media-change gate and a removable test. kern_emu had 16 bytes left before its
rung; the owner decided kern_emu gets no Zip drive (no emulator it targets has
a parallel-port Zip), so everything is behind `OS88_ZIP` and kern_emu is byte
for byte what it was. Two size folds the class agent found to make room on
kern_emu were withdrawn with it.

**The driver core** (`zip.asm` and four includes) is the SCSI layer, the
volume, the Control Panel page and the package door. Its one rule that
mattered later is the lazy medium: the driver learns what disk is in the drive
from the first transfer after a change, and a media change on a known disk
answers int 13h's 06h, which shuts the kernel's mount gate rather than
reading a new disk through an old FAT.

**Integration** found one cross-branch break: two test scripts each carried
their own copy of a kernel constant, which `t_mirror` refuses; it moved into
`tools/os88geom.py`. The image's real size set `DRVM_IMG_ZIP` (10 blocks
where 13 had been budgeted).

## The QA run (2026-11-24)

One pass in emulation after the last merge, through the real UI, on MartyPC
XTs with the emulated drive: every protocol (VPI0, VPI2, Zip 250), both port
modes (nibble on the XT's output-only card, PS/2 on a bidirectional one), a
superfloppy, a write-protected disk, a pulled disk, and no drive at all on CGA
and on Hercules with a Covox on the same lines. Every byte copied was
compared with what `os88zip.py` read out of the host image afterwards.

The driver itself had no defect. The run found **one real bug, and it was the
kernel's, not the driver's, and older than the Zip**: a Disk window showing a
volume that a driver drops stays open with the old listing, and opening the
letter again after a new disk goes in shows the OLD disk's files. The RAM
disk's unmount and the CD-ROM's detach had the same path all along; Eject
made it easy to see. `dsk_vol_del` now posts a close for every Disk window on
the dropped volume, through the deferred close the window manager already
had, so it is safe from a page click and from a driver's detach (SPEC.md
22.29). It is 26 bytes on kern_big and kern_mega, written inline because the
first version, a called routine, moved a hot routine in the next file by an
odd number of bytes, and `t_hotpar` caught it.

The other four defects were in the tests: a page selected by its list ordinal
where the kernel answers a record number (they differ by one on CGA, where
the Display page is hidden), text drawn in separate runs looked for as one
string, an editor's text looked for before it had been read, and a list that
scrolls on VGA never scrolled.

**Seen**: Zip 100 and Zip 250 sizes, the page on CGA, Hercules and VGA,
copies both ways byte-exact, eject and re-insert, write protection, `No
hardware found` with the port left as a printer wants it. **Not seen**: a
real drive, a real port, EGA (MartyPC has none), the page in the wide
Control Panel face.
