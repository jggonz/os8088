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

Run > Build Package writes the program as a standalone .O88 application.
Choose its name in the Save As dialog, then open the new package from the
Speedy folder.  SPEEDYCC.RT is the compiler's runtime template and must remain
beside SPEEDYBA.O88.  The compiler emits 8086 instructions directly; the
result runs without Speedy Basic.

The in-system compiler currently accepts blank lines, comments, CLS, END,
STOP, quoted or blank PRINT, and constant SCREEN, COLOR, LOCATE, PSET and LINE
statements.  Decimal, &H hexadecimal and &O octal integer constants are
supported; compiled source is limited to 128 statements and 79 characters per
line.  The editor's interpreter remains the compatibility path for the
complete language and all included demos.  Unsupported compiler input reports
the source line and does not leave a partial package.

SCREEN 1, 7, 9 and 13 retain their native logical pixels.  POINT, LINE,
CIRCLE, PAINT and graphics GET/PUT share that surface.  Programs that POKE
B800 CGA/text memory or A000 mode-13 memory update the window too; VGA DAC
OUT instructions and PALETTE changes map to the nearest desktop colour.
Higher-resolution images are fitted to the window only when painted, so
POINT and saved graphics keep their original resolution.
