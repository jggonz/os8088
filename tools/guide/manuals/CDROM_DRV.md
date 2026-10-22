# CD-ROM drive

`CDROM.DRV` makes an IDE CD-ROM drive part of the desktop. A data disc in
it opens like any other drive - its folders, its documents and the programs
on it - and an audio CD plays through the drive itself, under
[[CDRACK.O88]].

## What is inside it

The driver finds the drive, reads the disc's table of contents, and answers
the desktop's questions about the files on it. CD-ROMs keep their files in
ISO 9660, not in the FAT of a floppy or a hard disk, so the driver reads
that format itself. Programs open off the disc, documents open in their
programs, and the Copy and Paste commands copy files from it to any other
drive. Nothing can be written to a CD: a save, a delete or a new folder
there is refused as it is on a write-protected floppy.

Names on a CD can be longer than the eight-and-three this system uses. A
long name is shortened the same way every time - the first six letters, a
`~` and one more character, then the first three letters of its extension -
so `Long File Name.text` is listed as `LONGFI~K.TEX`.

An audio CD's tracks are listed in the drive as `TRACK01.CDA`,
`TRACK02.CDA` and so on. Opening one starts [[CDRACK.O88]] on that track.
The sound comes out of the drive: its front headphone socket, and the
sound card's CD input when the drive's audio cable is connected to one. The
computer does no work while a track plays.

## Why it is on the system disk

The driver looks for the drive on the computer's IDE connectors, which
means writing to them, so it is never loaded unless you ask. It uses about
9K of memory while loaded.

## Turning it on

Open the **Control Panel**, choose **Drivers**, scroll the list down to
**CD-ROM** and tick it. It loads at once and is kept for the next start
when you close the Control Panel. The row says **Loaded** and a **CD-ROM**
drive appears on the desktop, drawn as a disc, or **No hardware found** when
no CD-ROM drive answered.

The drive may be on the first or second IDE connector, as the master or the
slave; the connectors many sound cards carry are tried too.

## Good to know

- It needs a 286 or better: an 8088 or 8086 machine's 8-bit bus cannot
  carry an IDE drive's 16-bit data.
- Only the first CD-ROM drive found is used.
- After changing the disc, open the drive again from the desktop: a window
  left open on the old disc stops at its next step.
- Taking the driver out does not stop a CD that is playing; CDRack's
  stop button does.
- The small system for 128KB machines loads no drivers and leaves it off.
