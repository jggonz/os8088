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

## Choosing the drive

System Check first asks which drive to check, listing every drive the
machine has. Your hard disk is already selected, so most of the time you
just click **Scan** or press Enter. To check another drive, click it (or
use the arrow keys) first; double-clicking a drive checks it at once.

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
| Clean Up... | Removes the leftovers it is safe to remove, after asking |
| Quit | Closes System Check |

Keys: **S** scans or stops, **D** is Details, **R** saves the report,
**Esc** stops a scan, the up and down arrows choose a check, and Page Up
and Page Down scroll its findings.

## What Clean Up removes

Clean Up only removes files that are plainly left over: an install's
`KPAD.TMP`, a hibernation image nothing will resume, and programs this
release retired. It rebuilds the file associations when they point at
programs that are gone. Then it checks the disk again.

It leaves alone a program found in two folders (only you know which copy
you want) and a program built for another version (it may be your own,
waiting for an update). It cannot change the system's own hidden files.
For a wrong kernel or driver, the finding tells you to install again
from the release disks, keeping your files.

## Good to know

- System Check judges the disk against the release it came with. Use the
  copy in `SYSTEM` on the disk you are checking.
- On a 4.77 MHz PC it takes under a minute on a system floppy.
