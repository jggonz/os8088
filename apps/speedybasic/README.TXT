SPEEDY BASIC
============

Speedy Basic is a native, windowed Turbo Basic interpreter for os8088.  It
includes a source editor and retained text and graphics output.  Programs use
the same parser and runner in a normal window and in full screen.

Keys
----
F5       run the program
F8       execute one step
F6       switch between Editor and Output
Ctrl+F   enter or leave full screen

Use File > Open to load a .BAS program and File > Save As to write it.  The
Run menu can stop or reset a program.  While Output is selected, ordinary
keys are sent to INPUT and INKEY$ in the running program.

The graphics surface retains 320 by 200 colour pixels.  Higher-resolution
Turbo Basic screen coordinates are fitted to that surface so graphical demos
remain visible in a window on every display os8088 supports.

Runtime and compatibility
-------------------------
Keep SPEEDYBA.O88 and SPEEDYBA.OVL together in the SPEEDY folder. The build
includes both files on every Speedy BASIC disk, including the 360KB XT disk.

The native runtime implements fractional arithmetic without requiring an
8087, integer and numeric arrays, strings, structured loops and branches,
SUB calls, DEF FN, INPUT, graphics primitives, GET/PUT and speaker notes.
B800 text/CGA and A000 VGA memory accesses target private video buffers.
VGA DAC writes update those buffers' displayed palette, not the desktop's
hardware palette. VGA colours are mapped to the retained 16-colour surface;
high-resolution coordinates are downscaled, not displayed at full EGA size.

CALL ABSOLUTE runs a bounded virtual 8086 instruction subset supporting the
bundled span blitters. It is not unrestricted native machine-code execution.
Non-video PEEK/POKE addresses must belong to BASIC's allocated data claims.
PRINT USING currently supports a single integer # field and literal text.
This is a sample-oriented Turbo BASIC subset, not a complete compiler.
Pixel-heavy initialization, especially Boing and Mandelbrot, can take time
on an XT. The Run menu can stop a program while it is initializing.

Developer checks (from the repository root)
------------------------------------------
make speedybasicdisk
cc -O2 apps/speedybasic/hosttest/coretest.c -lm -o /tmp/speedybasic-coretest
/tmp/speedybasic-coretest
python3 tools/speedybasic_qemu.py --exercise --seconds 300
python3 tools/speedybasic_qemu.py apps/speedybasic/hosttest/REGRESS.BAS --software-fp

The QEMU runner launches the real disk package and feeds each unchanged
demo into its editor buffer through the debugger, then presses F5 through
the UI. It records runtime errors, completion/input/frame progress, controls,
clean exits, screenshots and results.json under build/speedybasic-qemu.
It does not emulate BASIC on the host or synthesize graphics screenshots.
The disk images are copied and run with snapshots; source disks are untouched.
