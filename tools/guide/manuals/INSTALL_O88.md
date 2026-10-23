# Install

Install puts os8088 on a hard disk, so the machine starts without a floppy.
On a machine that already starts from its hard disk, it upgrades the copy
there to the release on your floppies and keeps your own files.

It walks you through one page at a time: it switches the hard disk driver
on, finds the disks, asks which partition to use and whether to keep its
files, and then copies the system disk and the apps disk. It does the same
work as the **Install** window on the Control Panel's Hard Drive page
([[HDDTOOL.DRV]]), and it leaves the disk the same way.

![The first page, on a machine that started from a floppy.](shot:install-welcome)

## Starting Install

Double-click **INSTALL.O88** in the root of the system disk. It is also in
the root of a hard disk that Install has set up, which is where you start
an upgrade from.

Install needs a hard disk, the system disk and the apps disk. It runs on any
machine whose system disk carries it.

## The pages

Each page has three buttons along the bottom. **Next** goes on, **Back**
goes to the page before, and **Cancel** stops. Nothing is written to the hard
disk until you click **Install** on the last of the four steps.

| Page | What you do |
|---|---|
| Welcome | Read what is about to happen. On a machine that started from its hard disk this page says it is an upgrade |
| Step 1: the hard disk | Install switches the hard disk driver on if it is off, then lists the disks it found. Pick one |
| Step 2: where to put it | Pick one of the disk's four partitions |
| Step 3: keep or erase | Choose whether the files already on the partition stay |
| Step 4: ready to install | Check what will happen, then click **Install** |

![Step 2: the partitions of a 31 MB disk, and what is on each.](shot:install-slot)

The partition list uses the same words as the Control Panel's Install
window. `C: FAT16` is a volume you can see on the desktop as C:,
`Unpartitioned` is free space Install will make a partition of, and
`Booted From` is the volume the machine is running from. A partition Install
cannot use is greyed, and its line says why. The line under the list tells
you what will happen to the one you picked, and says when os8088 is already
on it.

On Step 3, **Keep them, and upgrade os8088** replaces the system and the
programs and leaves your own files and settings alone. **Erase it and install
fresh** deletes everything on the partition first. If there is nothing on
the partition, only Erase is offered. If the machine started from that
partition, only Keep is offered: start from the floppy if you want to erase
it.

![Step 3, for a partition that holds files.](shot:install-mode){right}

## Copying

When you click **Install**, the machine is busy for several minutes while it
copies. The bar in the menu bar shows how far it has got. Do not switch the
machine off.

The **Installing** page lists every step as it happens, in a box that scrolls:
the copy buffer it took, the disk it is copying from, the format (or that the
files are kept), every folder it makes and every file it copies with its
size, the files it keeps or leaves alone, the boot sector and the partition
table, the apps disk it found, and at the end how many files and kilobytes it
copied. When the copy ends, the heading says **click Next** and the list stays
on the screen until you do, so you can read how it finished. If the copy
stops, the last line of the list says why.

If the apps disk is in another floppy drive, Install finds it and copies it
straight after the system disk. Otherwise the page asks for it: take the
system disk out of drive A:, put the apps disk in, and click **Continue**.
**Skip** leaves the programs off; the hard disk starts without them.

When everything is copied, take the floppy out and click **Restart**. The
machine starts from the hard disk.

## Keys

| Key | What it does |
|---|---|
| Enter | The same as the default button, **Next** |
| Esc | **Cancel** |
| Up, Down | Move the choice on the disk, partition and keep-or-erase pages |

## Good to know

- If the copy stops, the last page names what went wrong, in the words the
  Control Panel uses. **Back** returns to the partition list.
- An upgrade replaces os8088's own files and leaves everything else. Your
  `SYSTEM.CFG` and the settings programs keep in `SYSTEM\APPDATA` stay.
- A disk whose size Install cannot work out is greyed on Step 1. Set its size
  on the Control Panel's Hard Drive page first.
- Install is not on the system disks for 128KB machines, which do not load
  drivers.
