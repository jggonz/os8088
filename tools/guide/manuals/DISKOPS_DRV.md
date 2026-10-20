# Disk operations

`DISKOPS.DRV` holds four of the Disk window's bigger jobs: copying a whole
floppy, turning a floppy into an image file and back, and compressing and
expanding files. It is read off the system disk when you pick one of them
and put away again when the job is done.

![The Disk menu, with Clone Disk... and Write Image... under Format Disk....](shot:diskops-drv-menu){right}

## Clone Disk...

**Disk > Clone Disk...** copies the floppy in the window's drive to another
disk, sector for sector. It works on any floppy, including a system disk
and a disk os8088 cannot otherwise read.

The question appears at the foot of the window:

![Clone A: to B:, erasing whatever is on B:.](shot:diskops-drv-clone){left}

- `Space` changes the target: the other drive, then **an image file**, then
  the same drive (for a machine with one drive, where you swap disks when
  asked).
- `Enter` starts the copy. `Esc` stops at any point.

With one drive, os8088 asks you to insert the SOURCE disk and then the
TARGET disk. If the target looks like the source, it warns
`Still the SOURCE disk`; press `Enter` to check again or `Y` to go ahead.
When the copy is done the menu bar says, for example, `Cloned A: to B:`.
The copy is exact except for its volume serial number: the new disk gets one
of its own, so os8088 and DOS can always tell the two disks apart.

![Cloning to an image file instead of a disk.](shot:diskops-drv-image){right}

Choosing **an image file** opens a Save box. The whole floppy is written
into one file, such as `DISK.IMG`, on any disk with room for it.

## Write Image...

**Disk > Write Image...** is the reverse. An Open box picks an image file,
then the window asks `ERASE A: and write DISK.IMG?`. `Enter` writes the
image onto the floppy in the window's drive. The file must be the size of a
whole floppy. The floppy gets a volume serial number of its own, as a cloned
disk does, and the image file itself is left as it was.

## Compress and Uncompress

Select a file and choose **File > Compress**. The file is replaced by a
smaller copy, and the menu bar says how big it is now, such as
`Compressed to 85%`. Programs and documents keep working as before: os8088
expands a compressed file by itself whenever it is read.

![README.TXT compressed to 85% of its size.](shot:diskops-drv-compress)

**File > Uncompress** turns a compressed file back into a plain one, so it
can be read by systems other than os8088. **File > Uncompress
To...** opens a Save box and writes the plain file wherever you choose. It
also joins a file that was split into parts across several floppies, asking
for each disk in turn.

All three are on the right-click menu of a file as well.

## Why it is a separate file

These jobs are needed rarely, so their code stays on the disk and leaves
the memory free for your programs. On a machine with one drive, a clone
first asks for the system disk to load the copier, then for the source
disk. If the system disk cannot be read, Compress and Uncompress say
`No disk`.

It is a hidden system file on every system disk. Leave it where it is.
