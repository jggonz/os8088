# Disk formatter

`FORMAT.DRV` holds the code that formats a floppy, so that os8088 can store
files on it. It is read off the system disk when you choose **Format
Disk...** and put away again when the format is finished or cancelled.

![The Disk menu of a Disk window, with Format Disk... at the top.](shot:format-drv-menu){right}

## Formatting a floppy

1. Put the disk in a drive and open its Disk window (double-click the
   drive's icon on the desktop).
2. Choose **Disk > Format Disk...**. The item is greyed when the window is
   not showing a floppy.
3. The window reads the disk and asks a question at the bottom, such as
   `Format A: as 360K?`. If the disk already holds files, the question
   starts with `ERASE`.
4. Press `Space` to switch between 720K and 360K where the drive offers
   both, `Enter` to format, or `Esc` (or any other key) to leave the disk
   alone.

![The question at the foot of the Disk window, ready for Enter or Esc.](shot:format-drv-prompt){left}

When the format worked, the menu bar says `Formatted A:` for a few seconds
and the window shows the new, empty disk. Every other window that was
looking at a folder on that disk goes back to its top level. If the disk
cannot be read at all, a `Disk error` message appears and nothing is
asked.

There is no undo. Formatting erases everything on the disk.

## Why it is a separate file

Formatting is something you do now and then, so its code does not need to
take up memory the rest of the time. Keeping it in a file leaves that memory
free for your programs.

The price is that the system disk must be readable when you ask. On a
machine with one floppy drive, where the drive holds the disk you want to
format, the Disk window first says `Insert the system disk in A:`. Put the
system disk in and press `Enter`; then it asks for the disk to format. A
second format in the same session asks nothing, and a machine with two
drives or a hard disk never asks.

## Good to know

- The same file also does a small piece of housekeeping: when os8088 learns
  where a program lives, it uses this file to update the disk's list of
  file types (`ASSOC.DAT`). You never see this happen.
- It is a hidden system file on every system disk. Leave it where it is; a
  system disk without it cannot format floppies.
- Formatting a hard disk is a different tool, on the Control Panel's **Hard
  Drive** page (see `HDDTOOL.DRV`).
