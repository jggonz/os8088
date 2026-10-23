# Hard disk tool

`HDDTOOL.DRV` is everything you see of a hard disk in the Control Panel:
the **Hard Drive** page, the **Format** window that partitions and formats
the disk, and the **Install** window that puts os8088 on it so the machine
can start from the hard disk. It is read off the system disk when you open
that page, and put away when you close the Control Panel.

![The Control Panel's Hard Drive page, showing a 31MB disk.](shot:hddtool-drv-page)

## Reaching it

The hard disk driver, `HDD.DRV`, starts by itself when the machine's BIOS
reports a hard disk. If it did not, open **Control Panel > Drivers** and
tick **Hard Drive**; a blank disk, or an IDE drive the BIOS does not know,
needs this. Then pick **Hard Drive** at the bottom of the Control Panel's
list.

The page lists each disk with its size, and its cylinders, heads and sectors
in the **C**, **H** and **S** boxes. A disk whose shape is unknown says
`Geometry unknown: type it`; type the numbers or use `-` and `+`. Pick a
disk, then a button:

- **Format** opens the Format window.
- **Install** opens the Install window.
- **Mount** gives each usable partition a drive letter and a desktop icon
  (C:, D: and on). It reads **Unmount** while they are mounted.

## The Format window

![Four partition slots, the first holding a 31MB FAT16 volume.](shot:hddtool-drv-format){right}

A disk has four slots. Each shows its size and its state: `FAT16` or `FAT12`
for a volume os8088 can use, `Not Formatted` for a free or empty slot,
`Unmountable` for one that belongs to another system, `Wrong Geometry`, or
`Booted From` for the volume the machine is running from. The bar under the
list is a map of the disk, and the line below it says how much space is
free.

- **Format** makes a usable volume in the selected slot. For a free slot,
  type the size in the **Size** box first. The window asks
  `Erase slot 1? Click Format again`; a second click does it, and the line
  then says `Formatted - now Mount it`.
- **Delete** gives the slot back to free space, after
  `Delete slot 1? Click Delete again`.
- **Close** closes the window.

Each volume can be up to 32MB.

## The Install window

![Choosing where to install os8088.](shot:hddtool-drv-install){left}

Pick a partition and click **Install**. Leave **Erase the partition first**
clear to keep the files already on it, or tick it to start with an empty
volume. The window asks you to click **Install** again to confirm, then
copies the system disk (`Copying - this takes a few minutes`). Next it asks
for the apps disk and the button reads **Copy Apps**. When everything is
copied it says `Done - remove the floppy, Restart`, and the button reads
**Restart**. From then on the machine starts from the hard disk.

On a machine with one floppy drive you swap disks when asked. Taking the
system disk out after its copy is safe.

[[INSTALL.O88]], in the root of the system disk, does the same install one
page at a time, and switches the hard disk driver on for you first.

## Why it is a separate file

`HDD.DRV` stays in memory as long as the disk is in use, but the page and its
windows are only needed while you are setting the disk up. Keeping them in
their own file leaves that memory free. If the system disk cannot be read
when you open the page, it says `Need the system disk`.

It is a hidden system file on every system disk. Leave it where it is.
