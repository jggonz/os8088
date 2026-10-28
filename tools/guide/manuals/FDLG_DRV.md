# File chooser

`FDLG.DRV` holds the code behind the window a program opens when you choose
**Open...** or **Save As...**: the Disk window that lets you pick a file, or
type a new name for one. It is on the system disks for machines with 128 KB
of memory, where the system keeps it in a file to leave that memory free for
your programs. The system reads it off the system disk when a program asks
for the chooser and puts it away again when the chooser closes.

## Using the chooser

The chooser is a Disk window with a name box at the foot. Open folders and
change drives in it as in any Disk window. Click a file to put its name in
the box, or type a name, then click **Open** or **Save** (or press `Enter`).
**Cancel** or `Esc` closes it and leaves the program as it was. Only one
chooser is open at a time.

## The system disk must be in the drive

Because the chooser is a file, the system disk has to be readable when a
program asks for it. On a machine with one floppy drive, if the drive holds
another disk at that moment, the menu item does nothing. Put the system disk
back in and choose it again. Once the chooser is open you can change disks
freely to pick where a file goes.

A machine with two drives, or a hard disk, keeps the system disk in reach
and never notices.

## Good to know

- It is a hidden system file on the system disks for 128 KB machines.
  Leave it where it is; without it, programs on that disk cannot open or
  save under a new name.
- The system disks for larger machines have no `FDLG.DRV`: there the
  chooser is part of the system itself.
- Copying files between Disk windows is the same kind of file, see
  [[FILECP.DRV]].
