# DOS tools

`DOS` holds programs for a different machine: a PC running MS-DOS. Copy them
onto that PC with a floppy, then run them there to share files with your
os8088 machine.

## What is in it

- `OS88NET.COM` shares a folder on the DOS PC with os8088 over a LapLink
  parallel data-transfer cable. The folder appears as a drive on the os8088
  desktop. Turn on the **os88net** driver on the Control Panel's Drivers
  page at the os8088 end.
- `OS88CZ.COM` splits a large file across several floppies and joins the
  pieces back together, and expands files compressed with the Disk window's
  **Compress**. It is on the 720KB, 1.2MB and 1.44MB software disks.

## Where it is

Inside `SYSTEM` on the software disks, the network disk and the second disk
of the everything set.

They are meant for the other PC. Double-clicking one here runs it in the
DOS box, which is a handy way to read its help (type `/?` after its name at
the DOS box's prompt), but OS88NET does its work on the DOS machine at the
far end of the cable.
