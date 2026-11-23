# Wide Control Panel data

`CPWIDE.DAT` holds the pictures and the words the wide Control Panel
([[CPWIDE.DRV]]) shows. It is data for the Control Panel, not a program.

## What is inside it

- **The icons**: a small icon for each page in the panel's list and each kind
  of driver, the OK, error and warning marks, and a large icon for each
  page's header.
- **The driver catalog**: for each driver the system knows, its kind
  (Display, Sound, Storage, Network, Input, Memory or System), its icon, the
  name the Drivers page shows, such as **Sound Cards** or **VESA Display**,
  and two lines saying what it does and what it needs. These are the words in
  the Drivers page's **DETAILS** box and the kinds its tabs filter by.

The system reads it from the system disk when the Control Panel opens wide
and frees it when the panel closes.

## Why it is on this disk

It sits in the root of the system disk beside `CPWIDE.DRV`, on the 720KB,
1.2MB and 1.44MB system disks, the emulator disk and the live media. The
360KB system disk and the small system disks for 128KB machines do not carry
it.

Without it, or if it is damaged, the Control Panel opens in its compact
layout, with no message. Every setting is still there: see [[CTRL.DRV]].
