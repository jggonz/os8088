# Copy and paste files

`FILECP.DRV` holds the code that copies and moves files between Disk
windows: the **Cut**, **Copy**, **Paste** and **Paste Into** items on a Disk
window's **Edit** menu. It is on the system disks for machines with 128 KB of
memory, where the system keeps it in a file to leave that memory free for
your programs. The system reads it off the system disk when you copy and puts
it away again when the copy or move is finished.

## Copying and moving files

1. In a Disk window, click the file or folder.
2. Choose **Edit > Copy** to copy it, or **Edit > Cut** to move it.
3. Open the Disk window, or folder, where it should go and choose
   **Edit > Paste**. **Paste Into** puts it inside the folder you have
   selected instead.

Dragging a file onto another Disk window or folder moves it the same way.

If a file of the same name is already there, the window asks before it
replaces it.

## The system disk must be in the drive

Because the code is a file, the system disk has to be readable when you copy
and when you paste. On a machine with one floppy drive holding another disk,
**Copy**, **Cut** and dragging do nothing, and **Paste** stops with `No
disk`. Put the system disk back in and try again.

A machine with two drives, or a hard disk, keeps the system disk in reach
and never notices.

## Good to know

- It is a hidden system file on the system disks for 128 KB machines.
  Leave it where it is; without it, Disk windows on that machine cannot
  copy or move files.
- The system disks for larger machines have no `FILECP.DRV`: there copying
  is part of the system itself.
- The Open and Save As chooser is the same kind of file, see
  [[FDLG.DRV]].
