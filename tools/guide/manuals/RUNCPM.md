# RunCPM

The `RUNCPM` folder holds the CP/M emulator and its drive A:

- `RUNCPM.O88`, the emulator;
- `A`, a folder that is CP/M's drive A. Inside it, each user area is a
  folder of its own, so `A/0` is user area 0, which holds the CP/M master
  disk: the system's tools, editors, assemblers and MBASIC;
- `1STREAD.ME`, notes on RunCPM, and `LICENSE`, its licence.

## Where it is

This folder is on the everything set and on the live USB image and CD, whose
drive A also carries the library of CP/M games and applications. On
RunCPM's own disks the same files are at the top level of the disk.

## Using it

Double-click `RUNCPM.O88`. It finds drive A in the folder beside it, so keep
`RUNCPM.O88` and the `A` folder together. Files you save in CP/M land in the
folder of their drive and user area, and you can copy CP/M programs into
those folders from a Disk window.
