# System Check

System Check looks over the disk your machine starts from and tells you
whether everything on it belongs to the same release of os8088. It is
meant for a machine that has been upgraded in place, where the new
release was installed over the old one and the old one's leftovers are
still there.

## Starting System Check

System Check lives in the `SYSTEM` folder of the system disk, and an
install copies it there on the hard disk too. Open `C:` (or the system
floppy), then `SYSTEM`, and double-click **SYSCHECK.O88**.

## Choosing the drive, and what to do

System Check first asks which drive to check, listing every drive the
machine has. Your hard disk is already selected, so most of the time you
just click **Scan** or press Enter. To check another drive, click it (or
use the arrow keys) first; double-clicking a drive checks it at once.

Beside the drives it asks what to do. Click a line, press Tab, or press
1 to 4:

| Task | What it does |
|---|---|
| Release files | Checks that everything on the disk belongs to this release (below) |
| Check the disk | Looks over the disk's file allocation table and every folder, like CHKDSK. Changes nothing |
| Check and repair the disk | The same, and repairs what it safely can |
| Defragment the disk | After a clean check, puts every file in one piece and moves files towards the start of the disk |

The three disk tasks need **FSCHECK.DRV**, which sits beside
`SYSCHECK.O88`. On the 360KB utilities disk both are at the root; on
the larger disks both are in `SYSTEM`.

The check then runs on its own, and the desktop stays usable while it
does. To check another drive afterwards, click **Scan** again.

## What it checks

Each check has a row and a lamp. On a colour screen the lamp is green
for OK, brown for a warning and red for something broken. On a black and
white screen it is a tick, a checkerboard or a solid square instead.

| Check | What it looks for |
|---|---|
| Kernel and boot files | `KERNEL.SYS` and the boot sector file are this release's, and the kernel is not empty |
| Drivers | Every driver and kernel module is one this kernel will load |
| Packages | Every program on the disk is built for this version of os8088 |
| Install leftovers | Files an interrupted install left, and the same program in two folders |
| Retired programs | Programs and drivers that an earlier release shipped and this one dropped |
| Release files | Files this release ships that are missing |
| Settings | The settings file does not ask for a driver that is not on the disk |
| File associations | Every program the associations name is where they say |
| Hibernation image | A saved session that will never be resumed |

Click a row to see what it found in the list below. **Details** gives
that list the whole window, which helps on a small screen.

## The buttons

| Button | What it does |
|---|---|
| Scan | Asks which drive, then checks it. While a check is running it reads Stop |
| Details | Gives the findings list more room, or puts the checks back |
| Save Report | Writes `SYSCHECK.TXT`, naming every finding, to the disk it checked |
| Clean Up... | Removes the leftovers it is safe to remove, and drivers this kernel refuses, after asking |
| Quit | Closes System Check |

Keys: **S** scans or stops, **D** is Details, **R** saves the report,
**Esc** stops a scan, the up and down arrows choose a check, and Page Up
and Page Down scroll its findings.

## What the disk tasks check

| Check | What it looks for |
|---|---|
| File allocation table | The disk keeps two copies of it, and they should match |
| Folders | Each folder's `.` and `..` entries, and entries that are garbage |
| File chains | A file's clusters that run off the disk, into free or bad space, or do not match the file's size |
| Cross-linked files | Two files claiming the same cluster |
| Lost clusters | Space marked in use that no file owns |
| Free space | How much is free, and any clusters marked bad |
| Fragmentation | How many files are in more than one piece |
| Defragment | What the defragmenter did |
| Repairs | How many repairs were made |

**Check and repair** fixes all of these except a cross-link: it ends a
broken chain at its last good cluster, cuts a file's size or its chain so
the two agree, rewrites a wrong `.` or `..`, copies the first FAT over the
second, and saves lost space as `FILE0000.CHK`, `FILE0001.CHK` and so on
in the root folder, so you can look at what was in it. A repaired finding
ends in *(fixed)*. For a cross-link, copy both files somewhere else,
delete them from this disk, and check it again; a file whose chain is too
long is left uncut too when another file shares its clusters.

A folder nested too deep, or one whose own chain is damaged, is not looked
into, so nothing can say what the files in it use. Then lost space is left
where it is and the disk is not defragmented, and the report says *not
done: a folder was not checked*. Move the folder higher up, or
repair what is wrong with it, and check again.

**Defragment the disk** only runs on a disk with nothing broken, so run
*Check and repair* first if it says so. It moves os8088's own files as
well as yours - the drivers and programs of an install that has grown in
pieces - and on a hard disk it moves `KERNEL.SYS` too, telling the disk's
boot sector where it went. It moves folders as well, but only when no other
program is open, because a program may remember where a folder was: close
your other programs first, or the report will say *folders not moved: close other programs*.
A program opened while it runs stops folders moving from then on. It
never moves disk images (`*.IMG`, except a hibernation image), or the
kernel on a floppy. If you save anything onto the disk while a disk task is running,
it stops and asks you to scan again rather than work from an out-of-date
picture.

## Running it from a link

A link to `SYSCHECK.O88` can say which drive and what to do, after the
program's name: for example `C: DEFRAG REPORT`. The words are a drive
letter with its colon, one of `RELEASE`, `CHECK`, `REPAIR` or `DEFRAG`,
and `REPORT` to save `SYSCHECK.TXT` when it finishes. Put such a link in
the `AUTOSTRT` folder and the disk is checked or defragmented every time
the machine starts.

## What Clean Up removes

Clean Up asks twice. First it offers the files that are plainly left
over: an install's `KPAD.TMP`, a hibernation image nothing will resume,
and the programs and drivers this release retired. It rebuilds the file
associations when they point at programs that are gone.

Then, if the check found any, it asks about the files this kernel refuses
to use: drivers built for another driver version, kernel modules from
another build, programs built for another version of os8088 or with a
damaged header, and a `COLDHMA.SYS` that is not this release's: *Also
remove 3 unusable files?* Such a file does nothing but take room. Removing it is safe, but an
install from the release disks is what brings a current copy back, so
afterwards System Check may list it as missing. A file that the disks you
installed from do not carry at all - `XMEM.DRV` and `COLDHMA.SYS` are not on
the 360KB system disk, and an old program may be on no apps disk any more -
is simply gone. Press Cancel to keep them. Then it checks
the disk again.

The drivers are hidden system files, so Clean Up asks **FSCHECK.DRV** to
remove them, and it removes only a file it has itself found retired or
refused. A driver this kernel loads, a module it uses and the kernel itself
are never touched.

It leaves alone a program found in two folders (only you know which copy
you want), a driver that loads but differs from this release's, and a
kernel from another release. For those, install again from the
release disks, keeping your files.

## Good to know

- System Check judges the disk against the release it came with. Use the
  copy in `SYSTEM` on the disk you are checking.
- On a 4.77 MHz PC it takes under a minute on a system floppy. Checking
  a 360KB floppy's file system takes about 5 seconds, and defragmenting
  one a few seconds per file moved.
