# System folder

`SYSTEM` holds the system's own furniture: the files and small programs the
machine uses rather than ones you open for work. It is best left as it is.

## What is in it

On the **system disk**:

- `FONTS`, the typefaces that Word, Scribe and Font Viewer use;
- `APPDATA`, where programs keep their settings and saved state;
- `TASKMGR.O88`, the Task Manager;
- `THEWIRE.O88`, The Wire, the online software library, opened from the
  Wire icon the desktop shows while a network link is up;
- `RLAPPLY.O88`, which applies REDLINE's tuning at start-up.

The drivers, the system itself (`KERNEL.SYS`) and the settings file are not
in this folder; they sit at the top level of the system disk.

On the **software disks** and most other disks, `SYSTEM` holds just
`APPDATA`, and on the software and network disks the `DOS` folder of tools
for a PC running DOS. The software disk carries a second copy of the Task
Manager here too.

## Putting things in it

Do not move or rename what is here. The system looks for these files in this
folder by name: a face moved out of `FONTS` is no longer offered, and The
Wire or the Task Manager moved elsewhere no longer opens from the desktop or
the System menu.
