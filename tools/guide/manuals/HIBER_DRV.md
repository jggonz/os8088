# Hibernate

`HIBER.DRV` lets you stop the machine with everything still open and carry
on later exactly where you were. It saves all of the computer's memory,
open windows, running programs and the clipboard included, to a file on the
hard disk, then lets you switch off. The next start finds the file and
offers to put everything back.

![Hibernate... in the System menu, above Restart.](shot:hiber-drv-menu){right}

## Hibernating

Choose **Hibernate...** in the System menu (the menu at the far left of the
menu bar). A window asks:

![The Hibernate window, before anything is saved.](shot:hiber-drv-window){left}

Click **Hibernate** (or press `Enter`) to go ahead, or **Cancel** (`Esc`).
While the memory is saved the window says `Hibernating...`. Then the screen
goes to text and says `It is now safe to switch the machine off.` Pressing
a key instead restarts it, straight into the question below. If saving
fails, the menu bar says `Hibernate failed` and nothing is lost: the
machine carries on as it was.

## Resuming

Start the machine as usual. When os8088 finds a hibernation file on the hard
disk, the desktop shows a window: `A hibernation file is on drive C:`.

- **Resume** reads the memory back and you are where you left off.
- **Discard** deletes the file and starts normally.

## What it needs

- **A hard disk** that the machine's own BIOS can use. Without one the menu
  item reads `Hibernate (No HD)` and is greyed. A disk the BIOS does not
  know, such as an IDE drive reached directly, is refused with
  `Needs a BIOS hard disk`.
- **Room on that disk** for a file the size of the machine's memory, up to
  640KB. Otherwise the menu bar says `Not enough room on disk`.
- The file is `HIBERNAT.IMG`, with a small `HIBERNAT.PTR` beside it, in the
  top folder of the hard disk the machine started from (or the first hard
  disk).
- A program using extended memory greys the item as `Hibernate (XMS)`,
  because that memory is not saved.
- Resume only works with the same system that hibernated. After you install
  a newer os8088 the file is refused with `Hibernation: other build` and
  deleted. A file that cannot be read says `Hibernation: unreadable`.
- On a machine with memory above 1MB, resuming needs that memory set up the
  same way: `XMEM.DRV` turned on and `COLDHMA.SYS` on the system disk, as
  when you hibernated. Hibernating there also needs a little free memory
  for a moment; without it the menu bar says `Not enough memory`.

Sound cards and network cards are switched off before the memory is saved
and switched back on after a resume.

## Why it is a separate file

Hibernating is something you do when you are leaving the machine, and
resuming happens right after the system disk has started it, so the code
can wait on the disk until then and leave the memory to your programs the
rest of the time. It is a hidden system file on every system disk. Leave it
where it is.
