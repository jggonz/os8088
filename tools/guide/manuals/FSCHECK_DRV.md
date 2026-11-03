# Disk checker

`FSCHECK.DRV` holds the code behind System Check's three disk tasks:
**Check the disk**, **Check and repair the disk** and **Defragment the
disk** - and the part of **Clean Up** that removes retired drivers and
drivers this kernel refuses, which are hidden system files a program cannot
delete. System Check reads it the first time it needs it, and puts it away
again when its window closes. Nothing else uses it, and
the rest of the time it takes no memory at all.

## Where it lives

It sits beside [[SYSCHECK.O88]] and must stay there, because System Check
looks for it in the folder it was started from:

- on the 1.44MB, 1.2MB and 720KB system disks, in `SYSTEM`, as a hidden
  system file;
- on the 360KB utilities disk, at the root, next to `SYSCHECK.O88`;
- on the live USB stick and CD, in `SYSTEM`;
- on a hard disk installed from one of the larger system disks, in
  `SYSTEM`, where the install copies both. An install from the 360KB
  disks does not copy them.

The 360KB system disk has no room for it, so it does not carry System
Check either. Use the utilities disk on a machine that starts from one.

## What it does

The disk tasks are described on System Check's own page. In short, the
checker reads the disk's file allocation table, every folder and every
file's chain of clusters, and reports what is wrong, like CHKDSK. Asked to,
it repairs what it safely can and puts files back in one piece. It works on
any FAT floppy, hard disk partition or RAM disk the machine can see.

## When it cannot run

System Check names the reason on its File allocation table row, and the
task does not start:

| Message | What to do |
|---|---|
| `FSCHECK.DRV is not beside SYSCHECK.O88` | Copy it back from the release disk into the same folder as System Check |
| `FSCHECK.DRV is from another build` | It belongs to a different release. Copy both files from one release's disks |
| `this kernel has no disk checker` | The system made for 128KB machines has no disk checker |
| `not enough memory` | Close a program and try again |

## Good to know

- It is built for one release of os8088 at a time, the same as the
  kernel's own modules. A copy from another release is refused rather than
  run.
- Leave it where it is. Without it, System Check still checks a disk's
  release files, but the three disk tasks cannot start, and Clean Up
  leaves old drivers where they are.
- It decides itself which system files Clean Up may remove: a driver on
  its own list of drivers earlier releases shipped, one this kernel would
  refuse to load, or a `COLDHMA.SYS` the kernel is not using. It never
  removes the kernel or a driver or module the kernel uses.
