# Sticky Apps menu

`STIKMENU.DRV` is the **Sticky Apps** menu on the desktop's menu bar. It
finds every Sticky App you have, shows which ones are running, and starts,
stops, hides, shows and arranges them. A Sticky App is a small program that
lives on the desktop itself, under your windows, such as Desk Calendar.

![The Sticky Apps menu with Desk Calendar running.](shot:stikmenu-drv-on)

## What is inside it

Everything the menu does: looking through the disks for Sticky Apps,
drawing the panel with each App's picture and description, and acting on
your choice. The system reads the file from the disk when you press the
**Sticky Apps** title and frees it again as soon as the menu closes, so the
menu takes no memory while you are not using it.

## Using it

The **Sticky Apps** title is on the menu bar whenever the desktop is in
front, beside **File**. With a Disk window in front it comes after **Disk**.
Press it to drop the panel.

![Before any Sticky App is running, each one offers Add.](shot:stikmenu-drv-add){left}

The panel lists every Sticky App it finds in the `STIKAPPS` folder of the
system disk and of any hard disk, one row each, with its picture, its name
and a line about what it does.

| On the panel | What it does |
|---|---|
| A row with **Add** | Starts that Sticky App |
| A row with a tick and **On** | Closes that Sticky App. It saves its own work first |
| **Arrange Sticky Apps...** | Lines the running Sticky Apps up in a column down the right of the desktop |
| **Hide All Sticky Apps** | Hides them all. Nothing is closed, and reminders still go off |
| **Show All Sticky Apps** | The same item while they are hidden: brings them all back |
| **More Sticky Apps...** | Opens The Wire, where you can find more |

The last line, **Drag a Sticky App to move it.**, is a hint and not a
choice. When there are more Sticky Apps than fit, the panel has a pager that
turns to the next page.

A Sticky App that is copied into a `STIKAPPS` folder shows up the next time
you open the menu. There is nothing to refresh.

## Good to know

- The system disk must be in drive A: when you press the title. Without it
  the menu bar says **Needs Sys Disk A:**.
- The file is on every system disk except the small ones made for 128KB
  machines, which have no Sticky Apps at all.
- Each time the menu opens it reads the `STIKAPPS` folders again, so on a
  floppy it takes a moment to appear.
