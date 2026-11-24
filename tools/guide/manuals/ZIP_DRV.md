# Zip drive

`ZIP.DRV` reads and writes Iomega Zip disks in a Zip drive plugged into the
computer's printer port. The disk becomes a drive on the desktop: open it,
copy files to and from it, save documents on it and take it to another
computer, as you would a floppy that holds as much as seventy of them.

## Which drives

- The original parallel **Zip 100**, the grey-blue drive with a printer
  socket on the back.
- The **Zip Plus** and the parallel **Zip 250**, which speak a newer version
  of the same printer-port language. The driver tells them apart by itself.

A Zip 100 disk works in all three. A Zip 250 disk needs a Zip 250 drive.

Disks formatted on a PC work, in both of the ways PCs format them: the usual
one, where the disk is divided like a small hard disk, and the one that uses
the whole disk as a single very large floppy. Disks formatted on a Macintosh
cannot be read.

## Any computer with a printer port

The driver works through every kind of printer port, the original IBM PC's
and XT's included. On those the computer reads the disk four bits at a time
through the port's status lines, about 29KB a second on a 4.77MHz PC. A
later port that can also read eight bits at a time is found and used by
itself, and is faster.

## Turning it on

Plug the drive into the printer port and switch it on. Then open the
**Control Panel**, choose **Drivers**, scroll the list down to **Zip Drive**
and tick it. It loads at once and is kept for the next start when you close
the Control Panel. A **Zip** drive appears on the desktop, under the
floppies, with the next free letter from **D:** on (C: is kept for a hard
disk). The row says
**No hardware found** when no Zip drive answered on any printer port.

It is never loaded unless you ask, because looking for the drive sends a
few signals down the printer port. A printer on that port may print a stray
character or two when you tick the row.

## The Zip Drive page

Once the driver is loaded the Control Panel's list has a **Zip Drive** page.
It shows which printer port the drive is on, how the driver talks to it
(**VPI0** for a Zip 100, **VPI2** for a Zip Plus or 250, then **nibble** or
**PS/2** for the port's kind) and the disk in it: its size, **No disk**, or
that it is write-protected.

**Eject** finishes writing anything still on its way to the disk, closes any
window open on it and pushes the disk out. The drive stays on the desktop,
empty, like a floppy drive with no floppy in it. Put another disk in and open
the drive again.

## Good to know

- A write-protected disk opens and reads as usual; saving to it says
  **Write protected**.
- If you push the drive's own eject button instead, open the drive again
  once the next disk is in: the driver notices the change and reads the new
  disk.
- Only one Zip drive is used, on the first printer port where one answers.
- A Zip disk cannot start the computer, and this system does not format Zip
  disks.
- It uses about 5K of memory while loaded.
- It is on every system disk, the 360KB one for an IBM PC or XT included.
  The build for emulators and the small system for 128KB machines leave it
  off.
