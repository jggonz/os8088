# Extended desktop

`EXTD.DRV` spreads the desktop across two monitors on a machine with two
display cards. The second screen becomes more desktop: you can drag a window
from one monitor to the other, or leave it standing across both.

![A VGA screen on the left and a Hercules screen to its right, as one desktop. The Disk window has been dragged onto the second monitor.](shot:extd-drv-desktop)

## What is inside it

The part of the system that runs a second monitor: starting the second card,
deciding where a window lands when you put it down on the other screen,
painting that screen's desktop, and handing the whole machine to one screen
while a full-screen program runs.

A machine with one monitor never needs any of this, so the file stays on the
disk. The system reads it when you turn the extended desktop on and frees it
when you go back to a single screen.

## Which machines can use it

Two display cards that can work side by side: a Hercules card together with
a CGA or a VGA. An EGA card is used on its own.

## Turning it on

1. Open the **Control Panel** and choose its **Display** page. The page only
   appears when the machine has more than one display card.
2. Under **Desktop Extension Mode**, click **Right** to put the second
   monitor to the right of the first, or **Below** to put it underneath.
   **Single** goes back to one screen.
3. Close the Control Panel to keep the choice. The next start comes up
   extended by itself.

The menu bar and the dock stay on the first monitor, the **primary**. To
have the second monitor on the left or above instead, select the other card
on the same page and press **Set Primary**: the arrangement turns round with
it.

## Good to know

- A new machine starts on one screen. The system can see a second card but
  cannot tell whether a monitor is plugged into it, so you say so here once.
- The system disk must be in drive A: to turn it on. If the file cannot be
  read, the panel says why and the desktop stays on one screen.
- A full-screen program, such as a game, uses one monitor while it runs. The
  desktop is spread out again when it ends.
- The small system disks for 128KB machines do not use a second card.
