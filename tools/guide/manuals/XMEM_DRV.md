# Extended memory

`XMEM.DRV` lets os8088 use memory above 1MB, which a 286 or later machine
can have and an 8088 or 8086 cannot. With it, that memory becomes a pool
that programs and the system can store things in, and part of the system
itself moves up there, leaving more ordinary memory for your programs.

## What is inside it

- Switching on the memory above 1MB and measuring how much there is.
- The pool, handed out in blocks to whatever asks, and the copying in and
  out of it.
- Moving part of os8088 into the first 64K above 1MB, read from
  `COLDHMA.SYS` on the system disk. That alone gives programs about 38K more
  ordinary memory. Some drivers and their buffers can go up there too.

## What uses it

- The **RAM disk** can keep its drive in extended memory: choose **Xms** on
  its Control Panel page.
- The **Video Player** keeps part of a long video there, ahead of the disk.
- **PiXEL** and the **DOS** window use it, and DOS programs that look for an
  XMS driver are offered it.
- **Clone Disk...** in the Disk menu reads a whole floppy into it, so a
  copy made with one drive needs one disk swap instead of many.
- The **Task Manager** shows an **XMS** line with how much is in use.

## Why it is on the system disk

The machine this system is made for, an IBM PC or XT, has no memory above
1MB and never can. Keeping this code in the system itself would cost every
such machine memory for nothing, so it is a file that only the machines that
can use it read.

## Using it

There is nothing to switch on, and it has no row on the Drivers page. When
os8088 starts, it asks the BIOS whether the machine has memory above 1MB.
If it does, the system reads `XMEM.DRV` from the system disk and starts
using it; if not, the file is never read. A machine either has the memory or
it does not, so there is no choice to make.

On an 8088 or 8086, or a 286 with only 640K, everything works as it would
without the file: the **Xms** choice on the Ram Disk page is greyed, and
programs use ordinary memory.

## Good to know

- Only the ordinary system disks carry it. The small system for 128KB
  machines leaves it off.
- Expanded memory on an add-in board, which an 8088 can use, is a different
  thing and has its own driver, `EMS.DRV`.
