# Expanded memory

`EMS.DRV` lets programs use the memory on a LIM EMS expanded memory board.
Such a board can hold megabytes, and unlike memory above 1MB it works in an
8088 or 8086 machine, so it is how an XT can keep far more than 640K of data
at hand.

## What is inside it

The driver finds the board, measures it, and shares it out. A board shows
its memory through a 64K window below 1MB, in four 16K pieces; the driver
lets each program take pages of the board and choose which of them show in
its part of the window. When a program closes, everything it held is given
back.

It knows boards whose page registers sit one after another: the Lo-tech
2MB EMS board, the PicoMEM's expanded memory, and boards compatible with
them. It looks for the window at E000h, D000h and C000h, and for the board's
registers at 260h, 264h, 268h, 26Ch and 288h. Boards that space their
registers apart, such as the Intel AboveBoard and AST RAMpage, are not
supported.

## What uses it

The **Video Player** keeps part of a long video on the board, ahead of the
disk, which helps a slow disk keep up.

## Why it is on the system disk

Finding a board means writing to its I/O ports, and on a machine without one
those ports may belong to another card. The Book8088's USB socket, for one,
answers at 260h. So the driver is never loaded unless you ask, and it uses
about 1K of memory while loaded.

## Turning it on

Open the **Control Panel**, choose **Drivers**, scroll the list down to
**EMS** and tick it. It loads at once and is kept for the next start when
you close the Control Panel. The row says **Loaded**, or **No hardware
found** when no board answered at any of the places above.

The board needs no settings: the driver tries each place itself. A board set
to an address outside that list is not found.

## Good to know

- DOS programs in the DOS window do not see the board through this driver.
- What is on the board does not survive a hibernate.
- The small system for 128KB machines loads no drivers and leaves it off.
