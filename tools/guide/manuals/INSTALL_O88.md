# Install

Install puts os8088 on a hard disk, so the machine starts without a floppy.
On a machine that already starts from its hard disk, it upgrades the copy
there and keeps your own files. It installs from your floppies, or, on a
machine with a network card, straight from the os8088 website.

It walks you through one page at a time: it asks where to install from,
switches the hard disk driver on, finds the disks, asks which partition to
use and whether to keep its files, and then copies the system and the
programs. The copying itself is done by [[HDDTOOL.DRV]].

![The first page, on a machine with no network card: the floppy disks are the source.](shot:install-welcome)

## Starting Install

Double-click **INSTALL.O88** in the root of the system disk, or click
**Install** on the Control Panel's Hard Drive page, which starts it from the
disk the machine started from. It is also in the root of a hard disk that
Install has set up, which is where you start an upgrade from.

Install needs a hard disk, and either the system disk and the apps disk or a
network connection: the Ethernet driver ([[ETHER.DRV]]) loaded with a working
card. It runs on any machine whose system disk carries it.

## The pages

Each page has three buttons along the bottom. **Next** goes on, **Back**
goes to the page before, and **Cancel** stops. Nothing is written to the hard
disk until you click **Install** on the last of the four steps.

| Page | What you do |
|---|---|
| Where from | Pick **Network** or **Floppy disks**. On a machine that started from its hard disk the heading says it is an upgrade |
| Step 1: the hard disk | Install switches the hard disk driver on if it is off, then lists the disks it found. Pick one |
| Step 2: where to put it | Pick one of the disk's four partitions |
| Step 3: keep or erase | Choose whether the files already on the partition stay |
| Step 4: ready to install | Check what will happen, then click **Install** |

![Step 2: the partitions of a 31 MB disk, and what is on each.](shot:install-slot)

`C: FAT16` is a volume you can see on the desktop as C:,
`Unpartitioned` is free space Install will make a partition of,
`Empty, No Room` is a free slot with no space left on the disk behind it, and
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

## Installing from the network

On a machine with its Ethernet driver loaded, the first page asks the os8088
website which release it has, and says **looking...** while it does. The
**Network** line then names that release and says whether it is newer than
the one you are running, the same, or older. A newer release is picked for
you. While **Network** is picked, up to four lines under it say what is new
in the website's release; picking **Floppy disks** takes them away.

The line is greyed when there is nothing to install from: **no network
card** when the Ethernet driver is not loaded, and **no answer** when the
website could not be reached.

Installing from the network copies the system and all the programs in one
go, so Install does not ask for the apps disk - and does not copy one that
happens to be in a drive either. The rest of the pages are the same as for
floppies. A web address you mounted on the Control Panel's Web
Drive page ([[HTTPPAGE.DRV]]) is put back when Install is done with it.

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

## Checking for updates at startup

The last page has two choices. **Check for updates at startup** makes the
installed machine ask the website each time it starts. When there is a newer
release, Install opens on its first page with the network picked. When there
is not, a short message in the menu bar says **os8088 is up to date**, or
why it could not ask, and nothing else happens. **Do not check for updates**
is the default, unless the hard disk already checks at startup: installing
again then keeps the check picked, and picking **Do not check for updates**
turns it off.

The check is a link called `OSCHECK.LNK` in the `AUTOSTRT` folder of the hard
disk. Deleting it turns the check off too, and holding Shift while the
machine starts skips it once.

## Keys

| Key | What it does |
|---|---|
| Enter | The same as the default button, **Next** |
| Esc | **Cancel** |
| Up, Down | Move the choice on the pages that have one |

## Good to know

- If the copy stops, the last page names what went wrong, in the words the
  Control Panel uses. **Back** returns to the partition list.
- An upgrade replaces os8088's own files and leaves everything else. Your
  `SYSTEM.CFG` and the settings programs keep in `SYSTEM\APPDATA` stay.
- A disk whose size Install cannot work out is greyed on Step 1. Set its size
  on the Control Panel's Hard Drive page first.
- Install is not on the system disks for 128KB machines, which do not load
  drivers.
