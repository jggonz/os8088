# File associations

`ASSOC.DAT` is a disk's list of which program opens which kind of document,
together with each program's icon. It is what lets you double-click a
document and have the right program open with it, and it lets a Disk window
show each document with its program's mark without reading every program
on the disk first.

![Documents in a Disk window, each showing the mark of the program that opens it.](shot:assoc-dat-icons){right}

## How associations work

A document's kind is the end of its name: `.TXT`, `.BMP`, `.MOD` and so on.
Double-click one and os8088 opens the program for that kind and hands it the
document. A handful of kinds are known from the start: `.BMP` and `.GIF`
open in Paint, `.TXT` in Note Pad, `.MOD` in Tracker, `.MD` in ArtfulType,
and `.COM`, `.EXE` and `.LNK` in DOS.

Every other program says for itself which kinds it opens: the list is part
of the program's own file. When os8088 sees a program in a folder, it learns
that program's kinds, so TeXPad's `.TEX` files, Sheet's spreadsheets and the
rest open with a double-click too.

If the program is on another disk, os8088 looks for it on the drives it has,
starting with the document's own disk. When it cannot find it anywhere, the
menu bar says what is missing, for example `Needs TRACKER.O88`. Put that
program's disk in a drive and try again.

## What the file does

Each disk that carries programs has its own `ASSOC.DAT` in its top folder.
It records the programs on that disk, the kinds they open and their icons.
With it, a disk is ready the moment it is put in: documents show their
marks and open at once, even before you have looked inside the folder the
program lives in. Without it, everything still works; os8088 just reads
each program the slow way and then remembers.

os8088 updates the file by itself when it learns something new, for example
after you copy a program onto the disk and open its folder. When you install os8088 on a hard
disk, the installer builds a fresh one for it from the programs it copied.

## Good to know

- It is hidden and belongs to the system. Do not edit or delete it.
- The system disk and every software disk carry their own, each describing
  its own programs, so the files differ from disk to disk.
