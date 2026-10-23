# Desktop widgets

`DESK.DRV` is what lets a Sticky App, such as Desk Calendar, live on the
desktop itself rather than in a window. With it, the App sits under every
window with no title bar and no dock tile, and stays put where you leave it.

![Desk Calendar on the desktop, drawn by the desk layer.](shot:desk-drv-desktop)

## What is inside it

The part of the system that handles desktop widgets: putting a widget onto
the desktop, showing it, moving it when you drag it, the handle that appears
under the pointer, and sending your typing to a widget you have clicked.

The system reads the file the first time a Sticky App starts and keeps it
until you restart the machine. Until then it takes no memory at all, so a
machine that never runs a Sticky App never pays for one.

## Using it

There is nothing to switch on. Start a Sticky App, from the `STIKAPPS`
folder on the system disk or from the **Sticky Apps** menu, and the system
loads this file by itself.

![The name tag turns into a handle when the pointer rests on it.](shot:desk-drv-handle){left}

A widget has a name tag at the bottom. Rest the pointer on it and the tag
turns into a handle, with a close box at its left. Drag the handle to move
the widget anywhere on the desktop. Click the widget to type into it; the
menu bar then shows the App's own menus.

Windows always cover a widget, never the other way round.

## If it is missing

When the file cannot be read, because the system disk is not in drive A: or
there is not enough memory, a Sticky App still works: it opens as an
ordinary window with a title bar instead of on the desktop.

## Good to know

- `DESK.DRV` is on every system disk except the small ones for 128KB
  machines. On those, Sticky Apps always open as ordinary windows.
- It stays loaded once a Sticky App has started, even after you close the
  App. A restart frees it.
