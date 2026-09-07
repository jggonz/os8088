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
