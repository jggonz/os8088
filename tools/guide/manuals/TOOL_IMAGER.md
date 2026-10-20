# Disk imager

The os8088 imager writes os8088 disk images onto real media from a Mac: a
floppy in a USB floppy drive, a USB flash drive, a CompactFlash card in a
USB card reader, or a blank CD. It finds the drives attached to your Mac,
lists the images that fit each one, and walks you through the write. It
then reads the disk back and checks that every byte arrived.

It runs in a terminal and asks its questions there. It changes nothing
until you type the target's name to confirm.

## What it needs

- A Mac. The imager uses the disk tools built into macOS and nothing else;
  on Linux or Windows it stops with **Device detection and writing currently
  require macOS.**
- Python 3.
- Your administrator password, for the write itself.
- Built disk images. The imager offers files that already exist; it does not
  build or download them. In an os8088 source tree, `make` builds the
  floppies and `make live` builds the USB and CD images. The images in an
  unpacked release work too.

## Starting the imager

In the os8088 source tree, type `make imager`. Or run the tool itself:
`python3 tools/os88imager.py`

| Option | What it does |
|---|---|
| `--scan` | List the attached drives and the images that fit them, write nothing, and exit |
| `--images FOLDER` | Look for images in this folder and every folder inside it. The default is the source tree's `build` folder |

`make imager BUILD=build-small` uses another build folder.

## What it offers

The imager lists three kinds of target, and for each one the images that
fit it:

| Medium | Images offered |
|---|---|
| Floppy disks | Floppy images of exactly the inserted disk's size: 360 KiB, 720 KiB, 1200 KiB or 1440 KiB. System disks, application disks and test disks all qualify |
| USB flash drives | Hard disk images with an os8088 partition that fit the drive, such as the live image `os8088-usb.img`. A CompactFlash card in a USB reader counts as one |
| CD burners | CD images, such as the live CD `os8088.iso` |

It looks at each file's contents and size, so unrelated `.img` and `.iso`
files are left out.

## Writing a disk

1. Choose a device by its number.
2. Choose an image by its number.
3. The imager shows the image's full path, its size, its SHA-256 checksum
   and the target.
4. For a hard disk image on a USB device, it asks about the disk's
   geometry (see below).
5. For a USB drive or card, it asks whether to keep the old settings (see
   below).
6. Type the target's identifier, such as `disk4`, to write. Anything else,
   or nothing, cancels with **Cancelled. Nothing was written.**

At any list, `r` looks for drives and images again and `q` goes back or
quits.

For a floppy or USB drive, the imager unmounts the disk, asks for your
password with `sudo`, writes it with a progress line, reads the whole image
length back and compares the checksum (**Verified: SHA-256 match.**), and
ejects it: **Ejected. The medium is ready to remove.** If anything fails, it
returns to the list so you can change the medium and try again.

For a CD, insert a blank CD-R or CD-RW. The imager burns with the burner you
chose and has macOS verify the disc. It does not erase a used CD-RW for you.

## CompactFlash cards for an XTIDE or a Book8088

A PC that boots a USB stick, QEMU and 86Box all work out a disk's layout
from the image itself. An XT with an XTIDE card does not: its XTIDE
Universal BIOS reports the CompactFlash card's own geometry (its heads and
sectors per track), and an image written for a different geometry answers
**Not bootable**, **Disk error**, or reaches a desktop whose files read
wrong. A Book8088 boots the same way.

The live image is laid out for 16 heads and 63 sectors per track. A card
reader on USB cannot tell the imager what the card reports, so when you
write a hard disk image to a USB device the imager asks:

- For a card above 504 MiB it says which mode XTIDE's Auto setting runs in
  and the heads and sectors that mode reports: 32, 64 or 128 heads in LARGE
  mode, 255 in LBA mode above about 4 GiB, with 63 sectors. The XTIDE boot
  menu names the mode, so you can check.
- For a smaller card XTIDE runs in NORMAL mode and reports the card's own
  geometry. Type it from the card's datasheet. A 256 MB SanDisk card is
  16 heads and 32 sectors, and a Book8088 boots the image written that way.

Press `Enter` to keep 16/63, type `heads/sectors` such as `64/63`, or `q`
to cancel. The imager changes the geometry in the copy it writes; your image
file is not changed. It then prints the new checksum, which the read-back
is checked against. It refuses a geometry too small to hold the image.

Another way is to set the card to 16 heads and 63 sectors in XTIDE's
configuration program, under User specified CHS, and keep 16/63 here.

## Keeping your settings

Every time you write to a CompactFlash card or USB drive, the imager asks
**Save and restore SYSTEM.CFG settings? [Y/n/q]**. It is for writing a new
os8088 over an old one.

- `Y` or `Enter` keeps your Control Panel settings. The imager reads
  `SYSTEM.CFG` off the old disk, saves a recovery copy in `/var/tmp` (it
  prints the exact path and keeps the copy whatever happens), and puts the
  settings into the new image as it writes. Every other file is replaced.
- `n` writes a fresh image with the default settings. Choose this for a new
  or blank card, which has no settings to keep.
- `q` cancels.

If the old disk has no settings file, or it cannot be read, or the new image
has no room, the imager stops before writing anything. Floppies and CDs do
not ask this question.

## Good to know

- **Writing destroys everything on the target.** Check the name and size
  the imager shows before you type the identifier.
- The imager only offers external USB disks that report removable media.
  Internal disks, other external disks, read-only media and the disk your
  Mac runs from are never listed. Some flash drives report fixed media and
  so do not appear.
- It checks the device again before writing and after unmounting, and stops
  if it has changed. Do not unplug or swap drives while it writes.
- Many USB floppy drives only appear once a formatted disk is inserted:
  insert one and press `r`.
- Most USB floppy drives handle only 1.44 MB disks, and some 720 KB. A 360 KB
  or 1.2 MB disk needs a controller that macOS shows as a disk.
- The imager writes the sectors of a disk; it does not format a floppy or
  change its geometry. A disk of unknown size cannot be chosen.
- `Ctrl+C` stops it. If writing had started, the medium may be incomplete:
  write it again.
