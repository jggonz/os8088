# How the CD-ROM driver was built

A diary of `CDROM.DRV` and the CD Player (SPEC.md §119,
[plan](plans/CDROM-PLAN.md)), kept as the work happened. Observations and
intentions are kept apart; what was tested is named.

## The ask

"Add CD-ROM data and audio support to the operating system as a driver."

## Wave 0 — mapping the OS first

The first question was not "how do I read ISO 9660" but "where does a file
system that is not FAT go in this OS". The answer was already in the tree:
§62.9's **file redirector**. A `DRVC_FILE` driver answers `FSV_*` questions
about files - list this folder, stand in that one, read these bytes - and
the kernel never reads a sector of the volume. The parallel-port cable and
the RAM disk are its two clients. A CD-ROM is the third: the kernel learns
nothing about ISO 9660, and the Disk window, the desktop zone, `..`, the
loader, the file dialog and the copy engine work on a CD unchanged.

Decisions taken here, each against what the tree already does:

- **A class of its own, `DRVC_CD` = 9**, not a share of `DRVC_FILE` or
  `DRVC_FILE2`: a class is one publication slot, and sharing one would make
  the CD and the cable (or the RAM disk) mutually exclusive - the exact
  defect §62.9.19 fixed for the RAM disk.
- **ATAPI on the IDE task file, on a 286 or better**, the same refusal as
  HDD.DRV's rung 1 (§52.1): a 16-bit data port on an XT's 8-bit bus loses
  every high byte.
- **Audio is the drive's.** `PLAY AUDIO MSF` and `READ SUB-CHANNEL`; the
  drive does the rest and the CPU does nothing. Ripping (`READ CD`) to a
  Sound Blaster is out of scope and written down as such.
- **Handles are positions.** A §62.9 handle is 16 bits; a CD has ~330,000
  sectors. A directory record is at least 34 bytes and starts on an even
  offset, so `offset >> 5` is unique within a sector: a handle is
  `(LBA - lowest directory LBA) x 64 + offset / 32`. It is stable for as
  long as the disc is in the drive, which is what the loader and the copy
  engine need, and it needs no table in the driver.

## Wave 1 — the row, the drive, the volume

**Kernel cost** (`tools/kernsize.py`, kern_big): `.text` +38 (the row, its
two strings, `drv_fptr9`'s pair, a `drv_svcix` byte), `.bss` +38 (the class's
owner word and its service-table copy), `.ovl` +1 (`drv_cfgbit`). The image
rung went from 121 bytes left to 45 - **no rung crossed**, `KERN_SIZE`
unchanged at 97,792. The `DVK_FILE` test in `dsk_vol_add` became `jae`
instead of `je`, which covers every file class from `DRVC_FILE2` up at no
cost. kern_small has no driver layer and is byte-identical.

**The driver**: 8,335 bytes of image (about 3 KB of it the sector buffer and
the TOC, zero state), a 9 KB claim when loaded, 4,774 bytes on the floppy
packed.

**The first boot** showed the CD-ROM zone on the desktop at once: the probe
found QEMU's drive at 170h, `IDENTIFY PACKET DEVICE` gave `QEMU DVD-ROM`,
and the volume registered as D: (C: is reserved for a hard disk). Opening it
listed nothing. Reading the driver's state out of the guest showed the TOC
right and the first `READ (10)` failed with **a timeout** - in microseconds.
The wait's safety net (a poll count under the tick bound, for a machine
whose tick has stopped) was initialised to zero, so the first poll that saw
BSY ended it. `TEST UNIT READY` and `READ TOC` had worked only because QEMU
answers them synchronously and never shows BSY. The driver now keeps the
last refusal's sense key and command (`cd_lkey`, `cd_lop`) for the next
diagnosis of this kind.

![The CD's root](cdrom/w1-root.png)

With that fixed the root listed what `tools/os88cdiso.py list` says it
must: `Long File Name.text` as `LONGFI~K.TEX`, `A.B.C` as `AB~2.C`,
`lower.txt` as `LOWER.TXT`. Two folders down and back:

![DOCS\DEEP](cdrom/w1-deep.png)

and Minesweeper launched off the disc:

![Minesweeper off the CD](cdrom/w1-mines.png)

**The fixture's sixty-name folder** was eighty at first, and listed 63: the
kernel's listing holds `DSK_NENT` = 64 rows, `..` included, on every volume.
That is the listing's cap, not the driver's, so the fixture was cut to sixty
- still a directory two sectors long, which is what it is there to test.

**Tested** (`tests/cdrom.py`, QEMU): the row loads and finds the drive; the
root, `DOCS`, `DOCS\DEEP`, `..` twice and `MANY` all list what the reference
lists; `MINES.O88` launches off the CD; `BIG.BIN` (100,001 bytes, over 64 KB
and not a whole number of sectors) copied to B: with Edit > Copy and Paste
is byte-identical on the host; an empty drive mounts nothing and A: still
opens after it.
