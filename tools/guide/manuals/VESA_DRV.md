# VESA display driver

`VESA.DRV` puts the desktop on a VGA card's high-resolution screen - up to
1024 by 768 pixels, in 256 or 65,536 colours - when the card has a VESA
BIOS. The desktop keeps its own sixteen colours at any size; a program
such as PiXEL can show a picture in the card's full colour.

Keep this hidden system file beside the other system files on the disk
you booted. Load it from the Control Panel's **Drivers** page by ticking
**VESA Display**; the desktop moves to the largest screen the card
offers at once, and does so at every start while the tick is set. A card
with no VESA BIOS refuses the driver and the desktop stays as it was.

Choose a screen on the Control Panel's **Display** page: the **Vesa** row
is named for the screen in use, and clicking it while its dot is on it
steps through the screens the card offers. **Set Primary** applies the
choice, which is kept.

The driver needs a computer with a 386 or better; it is not loaded on an
XT. Hibernate is not offered while the desktop is on the VESA screen, and
the extended desktop over two cards is not available with it. Full-screen
programs and games work as on the VGA and return to the VESA desktop.
