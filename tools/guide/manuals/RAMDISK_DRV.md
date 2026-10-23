# RAM disk

`RAMDISK.DRV` makes a drive out of memory. Files on it open and save at the
speed of memory rather than of a floppy, which makes it a good place for
work in progress and temporary files. What is on it lasts until you unmount
it or switch off, unless you preserve it to a file first.

![The RAM disk mounted as D:, with a file copied onto it.](shot:ramdisk-drv-window)

## What is inside it

The drive itself: keeping files and folders in memory and answering the
system's questions about them. Its Control Panel page is in a second file,
`RAMPAGE.DRV`, which is read when you first show the page and freed when
the Control Panel closes.

## Why it is on the system disk

A RAM disk uses memory that programs could otherwise have, so it is a
driver you ask for. Loading it costs about 6K. Mounting a drive then takes
the size you chose, plus a few K to keep track of the files.

## Turning it on

1. Open the **Control Panel**, choose **Drivers**, and tick **Ram Disk**.
   This loads the driver and adds a **Ram Disk** page to the list. It does
   not make a drive yet.
2. Choose the **Ram Disk** page, set the size, and click **Mount**. The
   drive appears on the desktop with the next free letter, labelled `RAM`.

![The Ram Disk page before mounting.](shot:ramdisk-drv-page){left}
![The same page with the drive mounted.](shot:ramdisk-drv-mounted){right}

## The Ram Disk page

| Control | What it does |
|---|---|
| **Size** | The drive's size in K. **-** and **+** step it by 8K, or click the number, type a size and press Enter. It cannot be larger than the memory free at the time, and it is greyed while the drive is mounted |
| **Store: Conv / Xms** | Where the drive is kept: in ordinary memory, or in memory above 1MB. **Xms** needs a 286 or better with extended memory, and both are greyed on a machine without it |
| **Mount** | Makes the drive. It reads **Unmount** while the drive is mounted. **Unmount throws the contents away** and gives the memory back |
| **Preserve** | Saves the whole drive to its image file |
| **Preserve As** | Saves it to a file you name, with the Save dialog. The name offered is `RAMDISK.RAM` |
| **Load** | Opens a `.RAM` image with the Open dialog and mounts it |
| **Clear** | Forgets the boot image. The file itself is kept |

The lines at the bottom say whether the drive is mounted, how much of it is
free, and which file is the **Boot image**.

## Keeping its contents

Preserve and Load make the RAM disk survive a restart. After you preserve
the drive or load an image, that file becomes the **boot image**: when the
machine next starts with the driver ticked, it reads the image back and
mounts the drive by itself. If the file cannot be found then, the machine
starts with no RAM disk and says nothing; the page tells you when you next
open it.

An image is the size of the whole drive, full or not, plus 8K.

## Good to know

- One file on the RAM disk can be at most 63.5K, and the whole drive holds
  up to 96 files and folders.
- Preserve As opens on the drive you were last working on, which may be the
  RAM disk itself. An image never fits on the drive it copies, so choose
  another drive; otherwise it ends with **Disk error on the image**.
- Other messages the page can give: **Not enough memory**, **No free drive
  letter**, **Not a Ram Disk image** and **That image is too big**.
- Close the Control Panel to keep the size, the store and the boot image.
- The small system for 128KB machines has no RAM disk.
