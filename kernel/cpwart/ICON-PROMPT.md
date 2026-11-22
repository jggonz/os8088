You are producing the icon artwork for the redesigned os8088 Control Panel shown in the three attached mockups (cp-drivers-vga-640x480.png, cp-display-vesa-1024x768.png, cp-theme-vga-640x480.png). os8088 is an 8086 GUI OS; these icons will be converted pixel for pixel into 4-bit-per-pixel bitmaps, so they must be exact pixel art.

Rules for every icon:
- ONLY the 16-colour EGA/VGA palette: black #000000, blue #0000AA, green #00AA00, cyan #00AAAA, red #AA0000, magenta #AA00AA, brown #AA5500, light grey #AAAAAA, dark grey #555555, light blue #5555FF, light green #55FF55, light cyan #55FFFF, light red #FF5555, light magenta #FF55FF, yellow #FFFF55, white #FFFFFF. No other colour, no anti-aliasing, no blur, no alpha.
- Background of every cell is MAGENTA #FF00FF (a key colour that is NOT in the palette); the converter turns it into "transparent". Do not use #FF00FF inside an icon.
- A 1-pixel black outline on each icon, in the style of the mockups' sidebar icons. Consistent light source, late-1980s look.

Produce TWO PNG files in the current directory, nothing else:

1. `cp-icons-16.png` - exactly 256x32 pixels: a grid of 16x16 cells, 16 columns by 2 rows, no gaps, no labels. Row 0, columns left to right:
   0 Scheduler (a list/clock-in-a-window), 1 Date/Time (calendar page "20"), 2 Keyboard, 3 Floppy (3.5" disk), 4 Drivers (a green circuit chip), 5 Display (monitor), 6 Sound (speaker with waves), 7 Theme (painter's palette), 8 Dock (a dock strip with three tiles), 9 Hard disk (a driver-owned page), 10 Network (two linked computers), 11 Debug monitor (a bug), 12 Generic driver page (a gear), 13 Mouse, 14 Memory (a RAM stick), 15 Ports (a DB-9 connector).
   Row 1 - DRIVER CATEGORY icons for the drivers table:
   0 Display, 1 Sound, 2 Storage (hard disk), 3 CD-ROM disc, 4 RAM disk (chip + disk), 5 Network card, 6 Input/mouse, 7 Memory (EMS/XMS), 8 System (gear), 9 Screen saver (night sky), 10 USB plug, 11 Virtual machine (box in a monitor), 12 Unknown driver (a page with "?"), 13 Status OK (green check in a circle), 14 Status error (red X in a circle), 15 Status warning (yellow triangle with "!").

2. `cp-icons-32.png` - exactly 384x32 pixels: twelve 32x32 cells in one row, no gaps, no labels - the large page-header icons, same subjects and style as row 0 of the 16x16 sheet: 0 Scheduler, 1 Date/Time, 2 Keyboard, 3 Floppy, 4 Drivers, 5 Display, 6 Sound, 7 Theme, 8 Dock, 9 Hard disk, 10 Network, 11 Generic driver (gear).

After saving, verify the dimensions and that every pixel is one of the 16 palette colours or #FF00FF, and fix any that are not.
