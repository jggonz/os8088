# os8088 system

`KERNEL.SYS` is os8088 itself: the window manager and desktop, the menus,
the Disk windows and the file system, the scheduler that runs several
programs at once, and the drivers for the screen, keyboard and mouse. When
the machine starts from this disk, the disk's first sector reads this file
into memory and hands the machine over to it. Everything you see after the
loading screen is running from it.

![The system disk in a Disk window: KERNEL.SYS and the other system files are hidden.](shot:kernel-sys-hidden){right}

## What is inside

One file holds the whole resident system, about 100KB once it is in memory.
It is stored packed, so it takes about 83KB on the disk and reads in
fewer turns of the disk; the loader unpacks it as it starts. The rarely used
parts, such as the Control Panel's pages, formatting and hibernating, live
in separate `.DRV` files beside it and are read only when you ask for them.

The About box (**About os8088...** in the System menu) shows the build
number of the system that is running.

## Why it is where it is

- **It is the first file on the disk.** The start-up sector is too small to
  look a file up, so it reads the kernel straight from the start of the
  disk's data area. Moving the file, or copying a different one over it with
  another tool, leaves a disk that will not start.
- **It goes with its own modules.** The Control Panel, the formatter and
  the other system modules on the same disk were made with this kernel and
  are refused by any other build. Copy a system
  disk whole (Clone Disk does this) rather than file by file.
- **It is hidden and read-only**, so neither os8088 nor DOS lists it and it
  cannot be deleted by accident.

Every system disk carries one, in each floppy size. The emulator system disk
carries a variant with the emulator pointer built in. When you install
os8088 on a hard disk, the installer writes this file there first.

## Good to know

Leave it alone. There is nothing in it to change, and the only way to
update it is to start from a newer system disk, or to install a newer one
onto the hard disk.
