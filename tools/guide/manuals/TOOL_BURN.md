# Media burner

The media burner puts os8088's live media onto real media from a Mac. It
does two things: it writes the live USB image, `os8088-usb.img`, to a USB
flash drive, and it burns the live CD image, `os8088.iso`, to a blank CD.
The result boots os8088 on a PC that can start from USB or CD in legacy BIOS
mode.

It runs in a terminal. It shows only drives that could be the right one,
asks you to type the drive's name before it erases anything, and reads the
whole image back afterwards to check it.

> The newer **Disk imager** does everything this burner does, and more: it
> also writes floppies, asks about the geometry a CompactFlash card for an
> XTIDE or a Book8088 needs, can keep your settings when you re-image a
> card, and lets you choose among several CD burners. Use the imager unless
> you need one of the two things below that only the burner does.

Only the burner:

- lists a USB flash drive that reports itself as fixed media. The imager
  leaves those out on purpose.
- takes an image by its file name (`--image` or `--iso`), and asks you for
  a path when the source tree has none built.

## What it needs

- A Mac. On any other system it stops and suggests `lsblk` and `dd` for
  Linux.
- Python 3. Nothing else: it uses the disk tools built into macOS.
- Your administrator password, for writing a USB drive.
- The live images. In an os8088 source tree `make live` builds them;
  the same files come in a release download.

## Starting the burner

In the os8088 source tree, type `make burn`. Or run the tool itself:
`python3 tools/os88burn.py`

| Option | What it does |
|---|---|
| `--scan` | List the attached USB flash drives and CD burners, change nothing, and exit |
| `--image FILE` | The live USB image to write. The default is `build/os8088-usb.img` |
| `--iso FILE` | The live CD image to burn. The default is `build/os8088.iso` |

The burner opens with a menu:

- **1) Write the live USB image to a flash drive**
- **2) Burn the live CD**. With no burner attached it says so.
- **q) Quit**

## Writing a USB flash drive

1. Choose **1**. The burner shows the image and its size.
2. It lists the USB flash drives attached now, each with its identifier, size,
   model and the names of its volumes. Type a drive's number. `r` looks
   again, `q` goes back. A drive smaller than the image is refused.
3. It says **About to ERASE** and names the drive. Type the drive's
   identifier, such as `disk4`, to confirm. Anything else stops with **Not
   confirmed. Nothing was written.**
4. It unmounts the drive and asks for your password. It writes the image
   with a progress line, then reads the whole image back and compares
   checksums: **Verified: N bytes, SHA-256 match.**
5. It ejects the drive. Plug it into the PC and boot it in legacy BIOS
   mode.

## Burning a CD

1. Insert a blank CD-R or CD-RW in the burner.
2. Choose **2**, then press `Enter` to burn, or `q` to go back.
3. macOS burns the disc and verifies it.

If the burn does not complete, because there is no blank disc or the disc
was refused, fix that and press `Enter` to try again. A CD is read-only, so
os8088 started from one keeps its settings only until it is switched off.

## Good to know

- **Writing a USB drive erases all of it.** Check the name and size before
  you type the identifier.
- Only drives on the USB bus that are external and are not the disk your
  Mac runs from are listed. Internal disks and Thunderbolt enclosures are
  never shown.
- The burner checks the file before it writes: a USB image must be a hard
  disk image with an os8088 partition, and a CD image must be a CD image.
  For floppy images, use the Disk imager.
- If the read-back does not match, suspect the stick before the image.
  Fake-capacity flash drives fail exactly this way.
- The burner does not let you choose among several CD burners. The Disk
  imager does.
- `Ctrl+C` at any question stops with **Nothing was written.**
