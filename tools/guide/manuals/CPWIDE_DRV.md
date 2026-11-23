# Wide Control Panel

`CPWIDE.DRV` is the Control Panel's large layout for VGA and VESA screens.
It holds the same settings as the compact panel in a bigger window: a list of
pages in groups with icons, a header that says what each page is for, and its
own Drivers, Display and Theme pages with room to show more.

## What is inside it

The large window's frame, its page list, header and status line, and the
wide Drivers, Display and Theme pages. Every other page comes from the
Control Panel itself ([[CTRL.DRV]]). The system reads this file and
[[CPWIDE.DAT]] from the system disk when you open the Control Panel and frees
them when you close it.

## When the wide layout appears

The Control Panel opens wide when the desktop is VGA or VESA and the room left
for windows, the screen less the menu bar and the dock, is at least 632 pixels
wide and 408 tall. The window is 616x408 on a 640x480 screen and 760x520 on
an 800x600 or larger one.

Otherwise it opens in its compact layout: on EGA, Hercules or CGA, on a VESA
640x400 screen, and on a 640x480 screen with the dock on its left or right
edge. A hidden dock leaves room. The compact layout also opens, with no
message, when `CPWIDE.DRV` or `CPWIDE.DAT` is not on the system disk or there
is not enough free memory for them.

The layout is chosen when the panel opens. If the screen changes while it is
open and the window does not fit, the panel says **Close and reopen the
Control Panel.** Do that and it opens in the layout that fits.

## The window

The list on the left has three groups. **SYSTEM** holds Scheduler, Date/Time,
Keyboard and Floppy. **HARDWARE** holds Drivers, Display and Sound, and the
page of each loaded driver that has one, such as Hard Drive or Ethernet.
**APPEARANCE** holds Theme and Dock. Click a page to show it.

The header above the page shows its icon, its name and one line about it. The
status line at the bottom says how many K of memory are free, how many of the
drivers are loaded, and that settings are saved when the panel closes.

Settings are kept exactly as in the compact panel: a change takes effect when
you click it and is written to the system disk when you close the window.

## Drivers

The tabs across the top filter the list: **All**, then one tab for each kind
of driver this system has, from **Display**, **Sound**, **Storage**,
**Network**, **Input**, **Memory** and **System**.

The table has the columns **Name**, **Status**, **Memory** and **Start at
boot**. Status is **Loaded**, **Off**, **No hardware** or **Error**. Tick
**Start at boot** to load the driver at every start; the tick alone loads or
unloads nothing. Click a row, or use Up and Down, to select a driver.

**DETAILS** below describes the selected driver: its name and file, what it
does and what it needs, a sentence about its state, and the memory it takes.

| Button | What it does |
|---|---|
| Load | Loads the driver at once and ticks Start at boot |
| Unload | Unloads it and clears Start at boot |
| Settings... | Shows the driver's own page; greyed unless it is loaded and has one |
| Ports... | Sets the ports for the network card and the MIDI port, as in the Control Panel. **Back** returns |

When a load fails, the row and DETAILS say why.

## Display

**Adapter** lists each display adapter the machine has, such as `VGA 640x480`,
with `, running` beside the one in use. VESA is listed while the **VESA
Display** driver is loaded. Clicking a row only selects it. On a machine whose
two cards can share the desktop, a **Desktop** line offers **Single**,
**Right** and **Below**, which apply at once.

**Mode preview** draws a screen of the selected mode's shape, with its size and
colours.

**Modes** lists, for VESA, the modes the card offers. The tabs filter them by
colours: **All**, then **256**, **32,768** or **65,536** where the card has
them. The table gives Resolution, Colours, Memory and Status, where Status
marks the **current** mode, the **recommended** one and the **startup** one.
The first row, **Auto (best)**, is the recommended mode. For another adapter
the group names its one mode. Without the VESA driver it says **Load VESA
Display on the Drivers page for more.**

- **Use mode at startup** makes the selected mode the one VESA starts in.
  Clearing it goes back to Auto. It does not switch the screen, and it is
  greyed unless VESA is selected.
- **Force Color** works as on the Control Panel's Display page. It is greyed
  without a VGA and while the desktop is VESA.
- **Apply** switches to the selected adapter and mode, and keeps it.
- **Test...** switches for 15 seconds. The **Keep this mode?** box counts
  down, **Reverting in 15 seconds.** Press **Keep** to stay. **Revert**, Esc,
  the end of the count or closing the panel go back to the mode you had.
  While a test runs, only Keep and Revert respond.

Apply and Test... are greyed when the selection is what is running.
**Running mode** shows the mode in use and the card, such as `VBE 3.0, 16 MB
video memory`.

## Theme

Four cards show **Bright**, **Dark**, **Color** and **Windows** as small
desktops in their own colours. The theme in use has a blue ring. Click a card
to use that theme.

**Color Presets** shows eight colour schemes as swatches. Click one to use it:
Bright or Dark changes to Color, and Windows stays Windows in the preset's colours.
**Customize...** opens the Color Theme window.

**Effects** turns **Window animations** **On** or **Off**. **Screen Saver**
has **Settings...**, which opens the screen saver's settings window.

## The other pages

Scheduler, Date/Time, Keyboard, Floppy, Sound, Dock and the drivers' own pages
are the Control Panel's pages, shown in the wide window: see [[CTRL.DRV]].

## Good to know

- The two files are on the 720KB, 1.2MB and 1.44MB system disks, the
  emulator disk and the live media. A machine started from the 360KB system
  disk, or from a small system disk for 128KB machines, opens the compact
  layout.
- While it is open, the two files take about 20KB of memory.
