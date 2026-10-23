# High memory image

`COLDHMA.SYS` gives your programs more room on a machine with memory above
1MB. Part of os8088 normally sits in the machine's main 640KB. On a 286 or
later with extended memory, the system copies that part into the first 64KB
above 1MB (the "high memory area") and gives the main memory it used back to
your programs, around 38KB of it. Some drivers, such as the sound, hard disk
and network drivers, can also move up there.

## How it is used

Nothing has to be switched on. When os8088 starts on a machine with memory
above 1MB, it loads the extended memory driver, `XMEM.DRV`, by itself. That
driver reads `COLDHMA.SYS`, checks that it belongs to the kernel that is
running, and moves the system up. If anything does not match, it changes
nothing and the system simply runs in main memory as usual.

On an 8088 or 8086, such as an IBM PC or XT, there is no memory above 1MB
and the file is never read. It costs those machines a few kilobytes of
floppy and nothing else.

## Why it is on the system disk

The image is cut from the exact kernel on the same disk, so it must stay
with that `KERNEL.SYS`. Every system disk carries its own copy, and the
installer copies it to a hard disk with the rest of the system.

## Good to know

- It needs a 286 or better with memory above 1MB.
- It is a hidden system file. Leave it where it is.
