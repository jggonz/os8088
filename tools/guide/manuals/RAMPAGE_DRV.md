# RAM disk settings

`RAMPAGE.DRV` is the **Ram Disk** page of the Control Panel. The RAM disk
itself, a drive made of memory that is very fast and forgets everything when
the machine is switched off, is `RAMDISK.DRV`. This file holds only the page
you set it up with: its size, where its memory comes from, and saving its
contents to a file and loading them back.

![The Ram Disk page, before the RAM disk is mounted.](shot:rampage-drv-page)

## Reaching it

![Ticking Ram Disk on the Drivers page.](shot:rampage-drv-drivers){right}

Open **Control Panel > Drivers** and tick **Ram Disk**. A **Ram Disk** item
appears at the bottom of the Control Panel's list; scroll down to it.
Ticking the driver does not make a drive by itself: that is the page's
**Mount** button.

## The page

- **Size**: how big the RAM disk is, in KB. `-` and `+` change it in steps
  of 8K, or type a number. It is greyed while the RAM disk is mounted.
- **Store**: **Conv** takes the memory from the machine's own 640KB;
  **Xms** takes it from memory above 1MB, on a machine that has it. Both are
  greyed when there is no such memory.
- **Mount** puts a RAM disk on the desktop with its own drive letter. It
  reads **Unmount** while mounted. Unmounting gives the memory back and
  throws away what was on it.
- **Preserve** writes everything on the RAM disk to its image file.
  **Preserve As** asks for a name first (the suggestion is `RAMDISK.RAM`).
- **Load** opens a `.RAM` image file, fills the RAM disk from it and mounts
  it.
- **Clear** forgets the boot image. The file itself is left alone.

Under the buttons the page says what is mounted and how much is free, and
names the **boot image**. The last image you preserved or loaded is the
boot image: when the machine starts, the RAM disk is filled from it and
mounted, so your files come back. If that disk is not in a drive, the
machine starts with no RAM disk and the page says why next time.

Settings are saved when you close the Control Panel.

## Why it is a separate file

The page only matters while you are looking at it, and many people tick the
RAM disk once and never open the page again. So it stays on the system disk
until you select **Ram Disk** in the list, and its memory goes back when the
Control Panel closes. Without the system disk the page says
`Ram Disk needs the system disk`.

It is a hidden system file on every system disk. Leave it where it is.
