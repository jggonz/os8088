# USB mouse driver

`USBMOUSE.DRV` lets you use a USB mouse. It was made for the Book8088, an
8088 laptop with one USB socket, and it also works with the USB mouse of a
PicoMEM card.

![The bottom of the Drivers page on a machine with none of these devices: os88net, USB Mouse and EMS all say No hardware found.](shot:usbmouse-drv-rows)

## What is inside it

Two ways for a USB mouse to reach the machine:

- **A CH375 USB chip**, the one in the Book8088's USB socket, or an 8-bit
  card that carries the same chip at the same address. The driver talks to
  the chip itself, sets the mouse up and reads it, and notices when a mouse
  is plugged in or pulled out.
- **A PicoMEM card**, whose own USB socket takes a mouse. The card passes
  each movement on to the driver.

Any ordinary USB mouse works; the driver asks it for the simple report every
mouse supports. Movement is scaled so that the pointer crosses the screen at
a comfortable speed, and the left and right buttons work as usual.

## Why it is on the system disk

Almost no machine has a USB socket, and finding the chip means talking to
I/O ports that belong to something else on other machines. So the driver is
never loaded unless you ask, and uses about 2K of memory while loaded.

## Turning it on

Open the **Control Panel**, choose **Drivers**, scroll the list down to
**USB Mouse** and tick it. Close the Control Panel to load it at every
start.

Plug the mouse in before or after; the driver finds it either way. The
first movement of the USB mouse makes it the machine's mouse, and a serial
mouse that was in use stops being read until the next start.

## Without the hardware

The row says **No hardware found** and nothing changes. If the Book8088's
BIOS is using the USB socket for a flash drive, the driver refuses rather
than take the drive away from the BIOS, and the row says **Port or IRQ
in use**.

## Good to know

- Pulling the mouse out with a button held down lets go of the button, so a
  drag does not carry on by itself.
- A USB keyboard or flash drive plugged into the socket is left alone.
- Inside the DOS window the PicoMEM's USB mouse does not move the pointer.
- The small system for 128KB machines loads no drivers and leaves it off.
