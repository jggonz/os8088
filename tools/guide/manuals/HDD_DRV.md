# Hard disk driver

`HDD.DRV` lets os8088 use a hard disk. With it, each FAT partition on the
disk appears on the desktop as a drive, starting at C:, and opens in a Disk
window like a floppy. It is also the way to partition and format a new disk
and to install os8088 on it so the machine starts from the hard disk.

![The Control Panel's Hard Drive page, with one disk found through the BIOS and mounted.](shot:hdd-drv-page)

## What is inside it

The driver reaches a disk in two ways:

- **Through the BIOS.** An MFM controller card in an XT, such as a Seagate
  ST11M, an XT-IDE card, or the hard disk support of an AT's BIOS. This
  works on any machine, the 8088 included, because the controller's own ROM
  does the work. Up to two disks are listed.
- **Straight to an IDE drive** that the BIOS does not know about, on a 286
  or better.

It also holds the Control Panel's **Hard Drive** page and the Mount button.
The disk tool that partitions and formats, and the installer, are in a
second file, `HDDTOOL.DRV`, which is read only while you use them.

## Why it is on the system disk

Most floppy-only machines have no hard disk, so the driver stays a file on
the system disk and costs about 4K of memory only where it is loaded.

You rarely need to load it yourself. When os8088 starts, it asks the BIOS
whether there is a hard disk. If there is, the driver loads and mounts the
disk's partitions. If it then finds nothing to mount, such as a blank disk,
it unloads again and the Drivers page says **Nothing to mount**.

## Turning it on

Open the **Control Panel**, choose **Drivers**, and tick **Hard Drive**. That
is the way to reach a blank disk, or an IDE drive the BIOS does not know.
Once it is loaded, the **Hard Drive** page appears in the Control Panel's
list, below Floppy. Close the Control Panel to keep the setting.

## The Hard Drive page

The list at the top shows each disk the driver found: **BIOS** or **IDE**
with its number, its cylinders, heads and sectors, and its size. Click a
disk, then a button:

| Button | What it does |
|---|---|
| **Format** | Opens the disk tool, which shows the disk's four partition slots and makes a usable drive in one, or deletes one |
| **Install** | Opens the installer, which copies the system disk and the software disk onto a partition and makes the hard disk start os8088 |
| **Mount** | Mounts every FAT partition on the disk as a drive. It reads **Unmount** while the disk is mounted, and then takes all of them off the desktop |

The **C**, **H** and **S** boxes show the disk's geometry. If the driver
cannot learn it (the page says **Geometry unknown: type it**), set the
cylinders, heads and sectors with **-** and **+**. A typed geometry is
remembered.

The line at the bottom reports what happened, such as **Mounted 1 volume**,
**No FAT partition on it**, **No partition table yet** or **Partition is
over 2GB**.

## Good to know

- A disk you mounted is mounted again at the next start, without a click.
- Format and Delete erase what is in the slot, and both ask first.
- Format and Install need the system disk in drive A:, because the tool is
  read from it.
- Hibernate saves the machine to the hard disk, so it needs this driver.
- Without a hard disk, ticking Hard Drive leaves the box clear with **No
  hardware found** under it.
- Every os8088 system disk carries it as a hidden system file. The small
  system for 128KB machines loads no drivers and leaves it off.
