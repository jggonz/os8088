# Control Panel

`CTRL.DRV` is the Control Panel: the window where you set how the machine
schedules programs, the date and time, which drivers are loaded, which
display it uses, the sound hardware, the look of the desktop, where the dock
stands and how the floppy drives are treated.

![The Control Panel open on its first page.](shot:ctrl-drv-scheduler)

## What is inside it

Every page of the Control Panel, its controls and its words. The system
reads the file from the system disk when you open the Control Panel and
frees it when you close the window. The rest of the time it takes no memory,
which leaves more room for your programs.

## Opening the Control Panel

Click the menu at the left end of the menu bar and choose **Control Panel**.
Clicking the clock at the right of the menu bar opens it straight on the
**Date/Time** page. Only one Control Panel can be open at a time.

The list on the left names the pages. Click a name to show its page on the
right. The panel is worked with the mouse.

## Changes are kept when you close it

A change takes effect as soon as you click it. The panel writes all your
settings to the system disk in one go when you **close** the window, and the
menu bar says **Settings Saved**. If the system disk is not in drive A: then,
it says **Not Saved: No Sys in A:** and the settings last only until you
restart. Minimizing the panel does not save.

## Scheduler

**Pre-emptive** shares the processor between running programs many times a
second, so a busy program cannot hold up the others. **Cooperative** lets a
program keep the processor until it pauses by itself. Pre-emptive is the
normal choice. The change takes effect immediately.

## Date/Time

![The Date/Time page.](shot:ctrl-drv-datetime){right}

The date is on the top row and the time under it. Click a field (the month,
the day, the year, the hour, the minute or the second) to select it, then
press **+** or **-** to step it.

**12-hour clock** shows the time with AM and PM, which adds an AM/PM field
to the time row. **Seconds in menu bar** adds seconds to the clock on the
menu bar.

The last line names the hardware clock the machine found, or says
**Hardware clock: none**. A machine without one starts at the same date
every time; with one, the date and time you set are written to it when you
close the panel.

## Drivers

The **Drivers** page lists the drivers on the system disk, with a box to
load or unload each one. It has its own page: see **Sound driver**.
While some drivers are loaded they add a page of their own to the list, such
as **Hard Drive** or **Ethernet**.

## Display

![The Display page on a machine with a VGA and a Hercules card.](shot:ctrl-drv-display){right}

This page appears only when the machine has more than one display adapter.
It lists the adapters it found. Click one and press **Set Primary** to move
the desktop onto it; the button is greyed while the dot is on the adapter
already in use.

With the **VESA Display** driver loaded (see **VESA display driver**) a
**Vesa** row lists the high-resolution screen the card offers, named by its
size and colour depth - `Vesa 1024x768x16`, or `Vesa auto` for the largest
the card has. Click that row again while its dot is on it to step through
the screens the card offers, then press **Set Primary**. The choice is
kept, and used at every start while the driver is loaded.

**Force Color** is for a VGA card that started the machine in shades of
grey. Tick it and the screen turns to colour at once, and every start after
that does the same. Clearing the tick takes effect at the next start. It is
greyed when there is no VGA.

**Desktop Extension Mode** spreads the desktop over two monitors: **Single**
uses one, **Right** puts the second monitor to the right of the first, and
**Below** puts it underneath. See **Extended desktop**. This row only appears
on a machine whose two cards can be used together.

## Sound

![The Sound page on a machine with only the PC speaker.](shot:ctrl-drv-sound){left}

Choose what plays sound: **PC Speaker**, **AdLib**, **Sound Blaster** or
**Covox**. A choice is greyed when the machine does not have that hardware,
or when the sound driver is not loaded. A Covox plugs into a printer port, so
it has three more boxes, **LPT1**, **LPT2** and **LPT3**, to say which.
**Test** plays a short tone through what is chosen.

## Theme

![The Theme page.](shot:ctrl-drv-theme){right}

The four themes change the colours of the desktop and its windows.
**Bright** is black on white with a patterned desktop. **Dark** is the
reverse. **Color** gives blue title bars and a teal desktop, and only works
on a VGA or EGA screen. **Windows** gives windows the buttons and menus of
Windows 95: see **Windows theme**.

**Customize...** opens the Color Theme window, with eight colour schemes and
a choice of title bar style: see **Color theme presets**. It is greyed where
Color is not offered.

**Window Animations** draws an outline that grows or shrinks when a window
opens, closes or is minimized. **Off** skips it and saves a moment each time.

**Screen Saver** opens the screen saver's own settings window: see
**Screen saver**.

## Dock

![The Dock page.](shot:ctrl-drv-dock){left}

Choose the edge of the screen the dock stands on: **Bottom**, **Left** or
**Right**. **Auto-hide** shrinks it to a thin line until you rest the pointer
on it. See **Dock options**.

## Floppy

![The Floppy page.](shot:ctrl-drv-floppy){right}

The system finds the floppy drives by itself. If it gets one wrong, set it
here. For each drive: **Auto** keeps what was found, **None** takes the
drive's icon off the desktop, and **5.25** or **3.5** gives it that picture.

**Reads** sets how much of a disk is read at one go. **Track** reads one
side at a time, which every machine can do. **Cylinder** reads both sides
together, which loads faster on machines that can. **Auto** lets the system
test the machine when it starts. If you choose Cylinder on a machine that
cannot, the menu bar says **No Cylinder Here: Auto** when you close the
panel.

These settings apply at the next restart, as the page says.

## Good to know

- The system disk must be in drive A: to open the Control Panel. Without it
  the menu bar says **Needs Sys Disk A:**.
- On the small system disks for 128KB machines the Control Panel has four
  pages: Scheduler, Date/Time, Display and Sound.
- When the list of pages is longer than the panel, it scrolls.
