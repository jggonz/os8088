# os8088 imager

`make imager` launches an interactive terminal tool on macOS. It detects
attached USB floppy drives, removable USB flash drives and CD burners, lists
the available images, and lets you choose a device followed by an image that
fits. Python 3 and the built-in macOS disk utilities are the only requirements.

```sh
make imager
python3 tools/os88imager.py --scan       # read-only device/image inventory
python3 tools/os88imager.py --images /path/to/build-or-release
make imager BUILD=build-small           # use an alternate build directory
```

The default image directory is this checkout's `build/`. Worktrees have their
own build directories; use `--images ../os8088/build` to use another checkout's
existing images. Discovery is recursive and checks image headers and sizes,
so application disks, alternate kernel builds and test images are included
alongside the system disks. Invalid or unrelated `.img`/`.iso` files are omitted.
The tool offers existing files; it does not build or download images.
Run `make` for the standard floppies, or `make live` for USB and CD images.
`make live` automatically sets up its missing C compiler and RunCPM data;
the first build needs network access, Git and a host C compiler. These
prerequisites are local to each worktree.

| Medium | Images offered | Build examples |
| --- | --- | --- |
| Floppy | FAT floppy images matching the inserted disk's capacity: 360 KiB, 720 KiB, 1200 KiB or 1440 KiB | `make`, `make small`, `make worddisk`, `make allapps` |
| USB flash drive | Partitioned images with an active FAT16 type-04 partition, including `os8088-usb.img`, that fit the device | `make usb` or `make live` |
| Writable CD | ISO9660 `.iso` images, including `os8088.iso` | `make iso` or `make live` |

Choose a numbered device, then a numbered image. `r` refreshes the inventory;
`q` goes back or quits. Before writing, the tool shows the full image path,
size, SHA-256 and target identity. Type the target's identifier to confirm.
An empty answer cancels. A failure returns to the inventory so you can replace
the medium and retry.

**A partitioned image on a USB-bus device gets one more question: the
geometry** (SPEC.md §80.5). The live image is laid out for 16 heads × 63
sectors per track, which a PC booting a USB stick, QEMU and 86Box derive
from its partition table. A period ROM does not derive anything — an XTIDE
Universal BIOS reports a CompactFlash card's *own* geometry — and an image
written for 16 × 63 to a card the ROM sees otherwise answers `Not bootable`,
`Disk error` or a desktop whose files read wrong. The imager cannot read the
card's geometry through a USB reader (the bridge does not pass ATA IDENTIFY
through), so it tells you what XTIDE's Auto mode reports for a card of that
capacity — LARGE mode's 32, 64 or 128 heads above 504 MiB, LBA's 255 above
about 4 GiB, both at 63 sectors, assuming the card's own 16 × 63 — and asks.
Press Enter to keep 16 × 63, or type `heads/sectors`. The ten bytes that
name a geometry (the partition entry's CHS columns and the BPB's heads and
sectors) are rewritten in memory as the image is written; the file on disk
is untouched, and the SHA-256 printed second is the one the read-back is
checked against. Below 504 MiB XTIDE runs in NORMAL mode and reports the
card's own geometry, which the imager does not know: type it from the card's
datasheet (a 256 MB SanDisk reports 16 heads × 32 sectors, and a Book8088
boots the image written that way), or keep 16 × 63 and read the screen
against §80.5's table. The same rewrite
without the imager is
`python3 tools/os88disk.py --retarget os8088-usb.img --geometry 64/63 -o cf.img`,
for a `dd` user or another platform.

Floppy and USB writes unmount the selected disk, request administrator rights
with `sudo`, write the raw device with progress, flush it, read back the image
length and compare SHA-256, then eject. CD burns use `drutil` with the selected
burner's identifier and verification enabled; its output provides progress.
Insert a blank CD-R or CD-RW. DiscRecording rejects unsuitable or insufficient
media. The tool does not erase an already-used CD-RW automatically.

Internal disks, non-USB disks, nonremovable USB disks, read-only media and
physical disks backing the running system are excluded. APFS boot volumes
are resolved to their physical stores, including multiple stores. If that
lookup fails, disk targets are not offered. The selected device is checked
again before writing and after unmounting; an observed identity change aborts.
Do not unplug or exchange devices during a write. Device metadata is not a
hardware serial-number guarantee against a same-model device substitution.

macOS must expose a floppy as a writable disk device. Many USB floppy drives
only appear after a formatted disk is inserted; insert the disk and rescan.
Standard floppy capacity identifies bridges with generic media names; a name
containing `floppy` or `FDD` also identifies an empty exposed drive. The tool
writes sectors, not flux, and does not format media or change drive geometry.
Most USB floppy hardware supports only 1.44 MB and sometimes 720 KB; 360 KB
and 1.2 MB need a compatible controller exposed as a disk by macOS. Unknown
capacity cannot be selected for writing.

USB devices must report removable media to qualify as flash drives. Some
flash drives report fixed media and are intentionally omitted; macOS metadata
cannot perfectly distinguish flash drives from all other USB storage. Check
the displayed target before confirming. Linux and Windows device backends
are not implemented.

Run hardware-free tests with `python3 tests/unit/t_imager.py`. They exercise
inventory, media matching, boot-disk exclusions, cancellation, burner targeting
and write/read-back behavior using fixtures and temporary files. Physical
writes require testing with actual disposable media.
