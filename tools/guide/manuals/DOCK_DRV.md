# Dock options

`DOCK.DRV` lets the dock stand on the left or right edge of the screen
instead of along the bottom, and lets it hide itself until you want it.

![The dock standing on the left edge of the screen.](shot:dock-drv-left)

## What is inside it

Everything the dock needs beyond its plain place at the bottom: laying the
tiles out down a side of the screen, drawing and answering clicks there, and
the hiding strip that opens under the pointer.

The plain dock along the bottom needs none of this, so the file stays on the
disk until you ask for something else. The system reads it when you choose
**Left**, **Right** or **Auto-hide**, keeps it while that setting is in use,
and frees it when you go back to **Bottom** with Auto-hide off.

## Using it

Open the **Control Panel** and choose its **Dock** page.

| Setting | What it does |
|---|---|
| **Bottom** | The dock runs along the bottom of the screen, as normal |
| **Left** | The dock stands on the left edge |
| **Right** | The dock stands on the right edge |
| **Auto-hide** | The dock shrinks to a one-pixel line on its edge. Rest the pointer on the line for a quarter of a second and the dock opens; it closes again about three quarters of a second after the pointer leaves it |

When the dock changes edge, windows move back inside the space it leaves
them.

A side of the screen is shorter than its bottom, so a dock on the left or
right holds fewer tiles when many programs are open.

Close the Control Panel to keep the setting. The next time the machine
starts, it reads `DOCK.DRV` from the system disk by itself.

## Good to know

- The system disk must be in drive A: when you choose one of these settings.
  If the file cannot be read, the menu bar says **Dock Module Unavailable**
  and the dock stays where it was.
- The small system disks for 128KB machines have no Dock page: there the dock
  is always at the bottom.
