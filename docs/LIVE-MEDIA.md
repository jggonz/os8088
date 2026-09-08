# Live USB & Live CD — booting os8088 with no floppy drive

os8088 ships as floppy images, and most machines that can still boot
MS-DOS-era code no longer have a floppy drive. The live media close that
gap: **one bootable hard-disk image** carrying the whole operating system
and **every application** — both word processors, the Z-machine story
reader, the CP/M emulator, the Commodore 64, the Weave family, all the games
and tools — written to a USB stick or wrapped in a CD. Booted either way, the
machine starts straight into the desktop with everything on drive **C:** and
about 30MB free to save into.

| file | what it is |
|---|---|
| `os8088-usb.img` | a raw 32MB hard-disk image (33,546,240 bytes), written directly to a USB stick |
| `os8088.iso` | the **same image** wrapped in a bootable CD (El Torito hard-disk emulation) |

They are two wrappers around one set of bytes, so anything that works on
the stick works on the CD — with one difference: a CD is read-only, so
settings and saved files last until power-off there, while the USB stick
keeps them like a real hard disk (§80.3).

## Getting the images

**From a release:** both files are in the release zip on the
[releases page](https://github.com/jggonz/os8088/releases), when that
release built them. `SHA256SUMS` in the zip covers them.

**From source:** the live media are an on-demand build — they carry the
applications written in C. Make automatically fetches and builds the pinned
C compiler when missing, and downloads the RunCPM files it needs:

```
make live             # build/os8088-usb.img + build/os8088.iso
```

`make usb` and `make iso` build the two singly. The first run also fetches
RunCPM's command processor and master disk (`make runcpm-src`) and the CP/M
software collection (`make cpmsw`, from Google Drive) — the RUNCPM package's
prerequisites, though only the master disk goes on the live image. Nothing
fetched is committed, and a tree behind a proxy that blocks either fetch
stops there.

The build is deterministic: the same source produces byte-identical images,
so a checksum comparison against a release is meaningful.

For a device-first chooser covering all built floppy, USB and CD images,
run `make imager`. See [os8088 imager](IMAGER.md) for discovery, media
requirements and verification. The original `make burn` guide below remains
available for the two live images.

## Writing the USB stick

> **Writing the image erases the entire stick.** Everything on it is
> destroyed. Double-check which device you are writing to — this is the one
> step where a typo costs somebody their backup drive.

### On a Mac — the guided way (recommended)

```
make burn
```

`tools/os88burn.py`, an interactive guide that makes the dangerous step
hard to get wrong:

```
os8088 live media writer (SPEC.md 80)

  1) Write the live USB image to a flash drive
  2) Burn the live CD
  q) Quit
  > 1

  Image: build/os8088-usb.img (33.5MB)

  USB flash drives attached now - CHOOSING ONE ERASES IT COMPLETELY:

    1) disk4      15.5GB  SanDisk Cruzer  (UNTITLED)

  Number to choose, r to rescan, q to go back.
  > 1

  About to ERASE disk4: SanDisk Cruzer, 15.5GB (UNTITLED).
  Everything on it is destroyed. This cannot be undone.

  Type the disk's identifier (disk4) to confirm, anything else aborts:
```

Three things it does that a raw copy command does not:

- **It only lists disks that could be the right answer.** A drive appears
  only if it is on the USB bus, external, and not the disk macOS is
  running from. Your internal drive and your Thunderbolt enclosure are not
  shown-with-a-warning — they are not shown at all. A stick smaller than
  the image is refused.
- **The erase is confirmed by typing the disk's identifier**, not by
  pressing `y`. Pressing `y` is muscle memory; typing `disk4` is a
  decision.
- **It verifies the write by reading it back.** After writing, it reads
  the full image length back off the stick and compares SHA-256 checksums.
  A stick that silently drops bytes — fake-capacity flash is sold every
  day — looks like a successful write and fails exactly this check.

Only the write itself runs privileged (`sudo` asks once); listing and
choosing do not. `python3 tools/os88burn.py --scan` lists the attached
drives and burners without touching anything, and `--image PATH` /
`--iso PATH` point it at images from an unpacked release zip instead of
`build/` (it asks for a path anyway when `build/` has none). It is macOS
only — `diskutil`, `drutil`, `hdiutil` — and refuses elsewhere.

### On a Mac or Linux — by hand

Find the stick, unmount it, write it, eject it. **On macOS:**

```
diskutil list external physical          # find it - say it is disk4
diskutil unmountDisk /dev/disk4
sudo dd if=os8088-usb.img of=/dev/rdisk4 bs=1m
diskutil eject /dev/disk4
```

(`rdisk4`, with the `r`, is the raw device — several times faster than
`disk4`.) **On Linux:**

```
lsblk -o NAME,SIZE,TRAN,MODEL,MOUNTPOINTS   # find it - say it is /dev/sdb
sudo umount /dev/sdb?                        # any mounted partitions
sudo dd if=os8088-usb.img of=/dev/sdb bs=1M conv=fsync status=progress
```

### On Windows

Use [Rufus](https://rufus.ie/) or
[balenaEtcher](https://etcher.balena.io/) and give it `os8088-usb.img`.
In Rufus choose **DD Image mode** if it asks — the image must be written
raw, byte for byte, not "converted".

## Burning the CD

**On a Mac**, `make burn` again — option 2 burns when a burner is attached
(`drutil list` sees it) and says so when none is. It uses Apple's own
`hdiutil burn`, which verifies the disc after burning. By hand, the same
thing is:

```
hdiutil burn os8088.iso
```

**On Windows**, right-click `os8088.iso` → *Burn disc image*. **On
Linux**, any ISO burner (`wodim -v dev=/dev/sr0 os8088.iso`, or the
desktop's disc writer). Burn it as an *image*, not as a data disc with one
file on it.

The ISO also carries the raw image as an ordinary file, `OS8088HD.IMG`,
beside a plain-text `README.TXT` — so a host that mounts the CD can copy the
USB image off it without a second download.

## Booting it

The live media boot through the **legacy BIOS** path (also called CSM or
"legacy boot"), the same way DOS did — os8088 is a real-mode 8086 operating
system, and UEFI-only machines cannot start it.

- **Real hardware:** plug the stick in (or insert the disc), enter the
  boot menu (commonly F12, F11, F8 or Esc during power-on), and pick the
  USB drive or the CD. If the stick does not appear, look in BIOS setup
  for *Legacy boot / CSM* and enable it, and prefer *USB-HDD* mode if the
  BIOS offers a choice of USB emulation types.
- **QEMU:**

  ```
  qemu-system-i386 -drive file=os8088-usb.img,format=raw -boot c \
    -chardev msmouse,id=m0 -serial chardev:m0

  qemu-system-i386 -cdrom os8088.iso -boot d \
    -chardev msmouse,id=m0 -serial chardev:m0
  ```

  The second line of each command is the mouse: os8088 drives a serial
  mouse, and that is QEMU's way of attaching one.
- **86Box:** add `os8088-usb.img` as an existing hard-disk image; when it
  asks for a geometry, the image is **65 cylinders, 16 heads, 63 sectors**
  (§80.1). **VirtualBox** does not take a raw image directly — convert it
  first (`VBoxManage convertfromraw os8088-usb.img os8088.vdi`) — or attach
  `os8088.iso` as a CD and boot from that.

What you should see: the boot splash, then the desktop with **A:** and
**C:** drive icons down the right-hand edge. Open C: — `APPS`, `GAMES`,
`WORD`, `CWORD`, `RUNCPM`, `C64`, `WEAVE`, `LOOM`, `MEDIA` and `SYSTEM` are
there, and `README.TXT` in the root is the on-disk manual. The typefaces are
inside `SYSTEM` — `SYSTEM/FONTS` (§19.8.1) — with the machine's own files
rather than in the root.
`DOCS/` is an empty folder for your own saves (on the USB stick they
survive reboots; on the CD, saving refuses — the disc is read-only).

## If it does not boot

1. **Check the image first.** `shasum -a 256 os8088-usb.img` against the
   `SHA256SUMS` in the release zip rules out a bad download in one line.
2. **A black screen or "No boot device"** usually means the machine booted
   in UEFI mode. Enable Legacy/CSM boot, or use the boot menu entry *not*
   labelled "UEFI:".
3. **The stick boots on one machine and not another:** try the BIOS's
   *USB-HDD* emulation setting, and try a different (smaller, older)
   stick — some BIOSes refuse large drives on the legacy path.
4. **It boots but C: is missing or empty:** the bytes on the stick are not
   the image — an interrupted write, or a fake-capacity stick that dropped
   the tail. Rewrite it; `make burn`'s read-back verify catches this class
   of failure at write time.

## How it works

There is no live-media-specific code in the OS at all. The image is what
os8088's own hard-disk installer writes to a real drive — a master boot
record (`boot/mbr.asm`), a volume boot record (`boot/boothd.asm`) and one
FAT16 partition with `KERNEL.SYS` first and contiguous, where the 512-byte
VBR reads it as a flat run — built by `tools/os88disk.py --hdd` at release
time instead of by the installer at install time, and checked by the same
`--verify-hdd`. The BIOS presents a USB stick (or an El Torito emulated CD,
`tools/os88iso.py`) through the same int 13h interface as a 1980s hard
disk, so the kernel adopts the partition as C: exactly as it does on a
machine it was installed onto, before any driver loads. §80 is the design
record, and §52.10 the hard-disk boot chain it reuses.
