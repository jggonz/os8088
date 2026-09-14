SPEEDY BASIC
============

Speedy Basic is a native, windowed Turbo Basic interpreter for os8088.  It
includes a source editor and retained text and graphics output.  Programs use
the same parser and runner in a normal window and in full screen.
SPEEDYBA.OVL is required and must remain beside SPEEDYBA.O88.

Keys
----
F5       run the program
F8       execute one step
F6       switch between Editor and Output (Editor stops a running program)
Ctrl+F   enter or leave full screen

Use File > Open to load a .BAS program and File > Save As to write it.  The
Run menu can stop or reset a program.  While Output is selected, ordinary
keys are sent to INPUT and INKEY$ in the running program.

SCREEN 1, 7, 9 and 13 retain their native logical pixels.  POINT, LINE,
CIRCLE, PAINT and graphics GET/PUT share that surface.  Programs that POKE
B800 CGA/text memory or A000 mode-13 memory update the window too; VGA DAC
OUT instructions and PALETTE changes map to the nearest desktop colour.
Higher-resolution images are fitted to the window only when painted, so
POINT and saved graphics keep their original resolution.
