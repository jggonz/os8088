# Windows theme

`WIN95.DRV` gives os8088 the look of Windows 95: blue title bars with the
name on the left, minimize, maximize and close buttons on the right, a size
grip in the bottom corner and menus that stay open when you click them.

![The Windows theme on a VGA screen.](shot:win95-drv-desktop)

## What is inside it

The parts of the window look that a change of colours cannot give: the three
buttons and the X, the caption on the left, the size grip, and the menus
that open with a click. The system reads the file when you choose the
Windows theme and frees it when you choose another, so the other themes do
not carry it.

## Choosing it

Open the **Control Panel**, choose its **Theme** page and click **Windows**.
Close the Control Panel to keep it; the next start reads the file again by
itself. Picking **Bright**, **Dark** or **Color** puts the usual look back.

On a VGA or EGA screen the theme has a teal desktop, grey menus and blue
title bars. On a Hercules or CGA screen it is drawn in black and white on
the patterned desktop, with the same buttons.

## What changes

- **The buttons.** Every window has **minimize**, **maximize** and an **X**
  to close it, at the right of its title bar. They work on any window, even
  one behind the others. Maximize makes the window fill the desktop, and the
  same button puts it back. A window that cannot be resized shows maximize
  greyed.
- **The size grip** replaces the grow box in the bottom right corner. Drag
  it to resize the window.

![A menu opened with one click stays open.](shot:win95-drv-menu){left}

- **Menus open with a click.** Click a menu title and the menu stays open;
  click an item to choose it. Pressing, dragging to an item and letting go
  still works too. With a menu open, **Up** and **Down** move the highlight,
  **Enter** chooses, and **Esc** closes it. A click anywhere off the menu
  closes it without doing anything else.

The Color Theme window (**Customize...** on the Theme page) can also give
the Color theme this window style in any of its colour schemes: see **Color
theme presets**.

## Good to know

- The system disk must be in drive A: when you choose it. If the file cannot
  be read, the menu bar says **WIN95.DRV not loaded** and the nearest theme
  that needs no file is used instead: Color on a colour screen, Bright on a
  black-and-white one.
- While a menu is held open, the rest of the desktop waits: the clock and
  other windows do not redraw until it closes.
- The small system disks for 128KB machines have no themes.
