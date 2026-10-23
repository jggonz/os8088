# OS88CZ

`OS88CZ.COM` is an MS-DOS program for moving files too big for one floppy
between a DOS PC and os8088. It splits a file into numbered parts,
`NAME.001`, `NAME.002` and so on, each sized to fill a fresh floppy, and it
joins such a set back into the original file. It also expands a
compressed file made by os8088's **Compress** command. It reads and writes
the same formats os8088 itself uses, so a set split on one machine is
joined on the other.

![OS88CZ run with no arguments, showing its three commands, in os8088's DOS window.](shot:os88cz-com-usage)

## Getting it

It is in the `SYSTEM/DOS` folder of the 720KB, 1.2MB and 1.44MB software
disks, the second disk of the everything set, the video demo drives and the
live USB image and CD. The 360KB software disk has no room for it. Copy it
to the DOS PC with `COPY`, like any file. It runs on MS-DOS 2 or later, on
an 8088 or anything newer, and needs about 192KB of free memory to split or
join.

## Splitting a file: S

`OS88CZ S FILE [SIZE] [DEST] [/S] [/P]`

- **SIZE** is the floppy the parts are for: `360`, `720`, `1200` or `1440`,
  or a number of bytes, at least 32,810. Each part fills a freshly
  formatted disk of that size. Without it, `720`.
- **DEST** is where the parts go. Without it, the current directory. To
  name DEST, give SIZE as well.
- **/P** stops before each part with **Ready for NAME.00n and press a key
  (Esc stops)**, so you can put a fresh floppy in DEST's drive. FILE must
  then be on another drive.
- **/S** stores every block as it is. Without it, OS88CZ compresses each
  32KB block that gets smaller, and stores the ones that do not.

OS88CZ reads the file twice: **Measuring** counts the parts, then it writes
them. The file must have a DOS 8.3 name, and a set holds at most 999 parts
and 128MB.

## Joining a set: J

`OS88CZ J PART [DEST]`

Name any part of the set; OS88CZ starts at `.001` and reads them in order.
The result gets the name the parts carry and goes into DEST, or the current
directory. When the next part is not where the last one was, it asks:
**Insert the disk with NAME.00n and press a key (Esc stops)**. So a set can
be joined straight off a pile of floppies onto a hard disk. It never asks
for a disk in the drive the result is being written to, and refuses to
overwrite a file that already exists.

## Expanding a compressed file: U

`OS88CZ U FILE OUT`

Expands FILE, a compressed file such as os8088's **Compress** writes, into
OUT. The whole file must fit in memory.

## On the os8088 side

In a Disk window, select a file and choose **File > Compress** to make a
compressed file, or select any part of a split set and choose **File >
Uncompress** to join it: the parts must all be in one folder, and the
result appears beside them. **File > Uncompress To...** asks where the
result should go and asks for each floppy as it needs it; the small system
for 128KB machines does not have it, and joining a set there says **Not
enough memory**.

## Good to know

- os8088 runs OS88CZ in its own DOS window too: double-click it, or type
  `OS88CZ` at the DOS prompt, as in the picture.
- A video file of several megabytes on a 720KB disk set is the case it was
  made for: split it on the DOS PC with `/P`, carry the disks over, and
  join them on the os8088 machine.
