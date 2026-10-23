# Notes

`NOTES` is where Sticky Note ([[STIKNOTE.O88]]) keeps its notes. Each note
is one plain text file, so the folder is an ordinary folder of `.TXT` files
that Note Pad, a Disk window or a DOS PC can read.

## What is inside it

On a new system disk the folder holds one note, [[WELCOME.TXT]], which the
pad shows the first time it opens. Every note you make with `+` on the pad
is added here as `NOTE0001.TXT`, `NOTE0002.TXT` and so on, using the lowest
number that is free.

The pad shows every `.TXT` file in the folder as a note, in the order they
are in the folder, and leaves any other kind of file alone.

## Where it is

`NOTES` is inside `SYSTEM\APPDATA` on the system disk in every size, on the
emulator system disk and on the live USB image and CD. Sticky Note always
uses the folder on the disk the machine started from. The small 128KB system
disks have no Sticky Note, so they have no `NOTES` folder either.

## Using it

You do not need to open this folder to use Sticky Note: the pad makes,
opens and deletes notes for you. Open it in a Disk window when you want to
work with the files themselves.

- Copy a `.TXT` file into the folder and it appears on the pad as a note
  the next time the pad turns a page. The first line is its title.
- Copy notes out of the folder to keep them, or to read them on another
  machine. They are plain text, with `[ ]` and `[x]` in front of the
  checklist lines.
- Deleting a file here deletes that note.
- Keep the system disk writable, or new notes and ticks are not saved.

The folder holds up to 30 notes. When it is full, the pad says **Notes
folder is full**. If the folder is deleted, Sticky Note makes a new, empty
one the first time it needs it; a folder made that way may hold fewer
notes, 14 on most disks.
