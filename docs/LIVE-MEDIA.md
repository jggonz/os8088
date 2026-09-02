# Live USB & Live CD — booting os8088 with no floppy drive

os8088 ships as floppy images, and most machines that can still boot
MS-DOS-era code no longer have a floppy drive. The live media close that
gap: **one bootable hard-disk image**, carrying the whole operating system
and **every application** — both word processors, the Z-machine story
reader, the CP/M emulator, the Commodore 64, all the games and tools —
written to a USB stick or wrapped in a CD. Booted either way, the machine
starts straight into the desktop with everything on drive **C:** and about
30MB free to save into.

| file | what it is |
|---|---|
| `os8088-usb.img` | a raw 32MB hard-disk image, written directly to a USB stick |
| `os8088.iso` | the **same image** wrapped in a bootable CD (El Torito hard-disk emulation) |

They are two wrappers around one set of bytes, so anything that works on
the stick works on the CD — with one honest difference: a CD is read-only,
so settings and saved files last until power-off there, while the USB stick
keeps them like a real hard disk.

## Getting the images

**From a release:** both files are in the release zip on the
[releases page](https://github.com/jggonz/os8088/releases), when that
release built them. `SHA256SUMS` in the zip covers them.

**From source:** the live media are an on-demand build — they carry the
applications written in C, so they need the compiler the shipped floppies
deliberately do not:

```
tools/setup-cc.sh     # one-time: fetch and build the C compiler into build/cc
make runcpm-src       # one-time: fetch the CP/M command processor and master disk
make live             # build/os8088-usb.img + build/os8088.iso
```

The build is deterministic: the same source produces byte-identical images,
so a checksum comparison against a release is meaningful.

## Writing the USB stick

> ⚠️ **Writing the image erases the entire stick.** Everything on it is
> destroyed. Double-check which device you are writing to — this is the one
> step where a typo costs somebody their backup drive.

### On a Mac — the guided way (recommended)

```
make burn
```

An interactive guide that makes the dangerous step hard to get wrong:

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
  shown-with-a-warning — they are not shown at all.
- **The erase is confirmed by typing the disk's identifier**, not by
  pressing `y`. Pressing `y` is muscle memory; typing `disk4` is a
  decision.
- **It verifies the write by reading it back.** After writing, it reads
  the full image length back off the stick and compares SHA-256 checksums.
  A stick that silently drops bytes — fake-capacity flash is sold every
  day — looks like a successful write and fails exactly this check.

Only the write itself runs privileged (`sudo` asks once); listing and
choosing do not. `python3 tools/os88burn.py --scan` lists the attached
drives and burners without touching anything.

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

**On a Mac**, `make burn` again — option 2 appears live when a burner is
attached and says so when none is. It uses Apple's own `hdiutil burn`,
which verifies the disc after burning. By hand, the same thing is:

```
hdiutil burn os8088.iso
```

**On Windows**, right-click `os8088.iso` → *Burn disc image*. **On
Linux**, any ISO burner (`wodim -v dev=/dev/sr0 os8088.iso`, or the
desktop's disc writer). Burn it as an *image*, not as a data disc with one
file on it.

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
- **86Box / VirtualBox / others:** attach `os8088-usb.img` as a hard disk
  image, or `os8088.iso` as a CD, and boot from it.

What you should see: the boot splash, then the desktop with a **C:** drive
icon on the right. Open it — `APPS`, `GAMES`, `WORD`, `CWORD`, `RUNCPM`,
`C64`, `MEDIA` and the rest are all there, and `README.TXT` in the root is
the on-disk manual. `DOCS/` is an empty folder for your own saves (on the
USB stick they survive reboots; on the CD, saving politely refuses —
the disc is read-only).

## If it does not boot

1. **Check the image first.** `shasum -a 256 os8088-usb.img` against the
   `SHA256SUMS` in the release zip rules out a bad download in one line.
2. **A black screen or "No boot device"** usually means the machine booted
   in UEFI mode. Enable Legacy/CSM boot, or use the boot menu entry *not*
   labelled "UEFI:".
3. **The stick boots on one machine and not another:** try the BIOS's
   *USB-HDD* emulation setting, and try a different (smaller, older)
   stick — some BIOSes refuse large drives on the legacy path.
4. **It boots but C: is missing or empty:** the write was incomplete.
   Rewrite the stick; `make burn`'s read-back verify catches this class of
   failure at write time.

## How it works, for the curious

There is no live-media-specific code in the OS at all. The image is the
same thing os8088's own hard-disk installer writes to a real drive — a
master boot record, a boot sector, and one FAT16 partition with the kernel
laid out where 512 bytes of loader can find it — built at release time
instead of at install time. The BIOS presents a USB stick (or an El Torito
emulated CD) through the same interface as a 1980s hard disk, so the
kernel adopts the partition as C: exactly as it does on a machine it was
installed onto. SPEC.md §80 is the design record, and §52.10 the hard-disk
boot chain it reuses.
