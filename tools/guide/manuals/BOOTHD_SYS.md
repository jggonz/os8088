# Hard disk boot sector

`BOOTHD.SYS` is the start-up code for a hard disk. It is one 512-byte
sector: the first thing the machine runs when it starts from a hard disk
partition that os8088 was installed on. Its only job is to read
`KERNEL.SYS` from that partition and start it.

## Why it is on the system disk

When you install os8088 on a hard disk (Control Panel, **Hard Drive** page,
**Install**), the installer takes this sector from the system disk you are
installing from and writes it to the start of the partition. The sector has
to match the kernel it starts, so it travels on the same disk as that
kernel. That way a newer system disk always installs its own start-up code
along with its own kernel, even when you install it from a machine that is
already running an older os8088 from its hard disk.

Nothing reads it when you start from a floppy. A floppy's own start-up
sector is part of the disk image and is not a file.

## Good to know

- It is a hidden system file on every system disk. You never open it.
- If it is missing, Install refuses with
  `Older disk: boot from it to install it`, and changes nothing on the hard
  disk.
- Leave it where it is.
