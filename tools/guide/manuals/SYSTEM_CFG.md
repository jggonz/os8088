# Settings

`SYSTEM.CFG` holds os8088's saved settings: everything you choose in the
Control Panel. That includes which drivers are switched on, the scheduler,
the clock's 12 or 24 hours and seconds, the sound route, the theme and
colours, the dock and the floppy drive settings, and each driver's own
settings, such as the RAM disk's size and boot image. The system reads it at start-up, so the machine comes back the way
you left it.

![The menu bar after the Control Panel is closed with changes.](shot:system-cfg-saved)

## How it is saved

Most changes in the Control Panel take effect as you make them. When you close
the Control Panel, any changes are written to `SYSTEM.CFG` and the menu bar
says `Settings Saved`. If the disk cannot be written, it says
`Not saved: disk error`; the settings still apply until the machine is
switched off.

The file lives in the top folder of the disk the machine started from, so
keep the system disk writable. Most system disks ship without one: the
first time you save settings, os8088 creates it. Until then, every setting
has its default.

## On the emulator system disk

The emulator system disk ships with a `SYSTEM.CFG` already on it. It
switches on the **Absolute mouse** driver, so in an emulator that supports
it (such as the os8088.com demo or QEMU) the pointer follows your own mouse
straight away, with no need to capture it.

## Good to know

- It is a small hidden file. Use the Control Panel to change it; there is
  nothing in it to edit by hand.
- Deleting it puts every setting back to its default at the next start.
- A setting written by a newer os8088 that this one does not know is
  ignored.
- When you install os8088 on a hard disk over an existing installation and
  keep its files, its `SYSTEM.CFG` is kept too.
