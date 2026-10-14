REDLINE 1.0 - CPU + Graphics Performance Lab for os8088

Open REDLINE.O88. Summary shows a large CPU/RAM score, system snapshot and
graphical comparison bars. Detailed keeps every inventory field, raw timing,
method flag and comparison from the complete report. Compare expands the bars.
Use the bottom buttons or U/D/C to select views; Tab cycles them. Press R or
Run to benchmark. S/Save writes REDLINE.TXT in the package's current folder.
Results group CPU, RAM and Graphics, each headed by its own mean. A panel
too short for its lines gets a scroll bar; PgUp/PgDn, arrows and Home/End
scroll Results. Detailed keeps its own paging.
F1 opens Detailed at the report's provenance. Q/Quit closes the package.
Close other apps and keep the pointer still when comparing runs.

Every test performs identical work on every CPU. CPU instruction mixes,
2KB RAM copy/fill and OS drawing/text/blits have separate timings.
1000 on the PC index is the recorded MartyPC 4.77 MHz IBM 5150 reference.
Graphics indices use the matching CGA, Hercules or VGA mode reference.
Other modes retain raw timings without borrowing a different mode baseline.
The Summary headline averages the six CPU/RAM indices, with equal weight.
All bars use the highest score rounded up plus 5x: 69x gives a 74x ceiling. VGA uses blue
CPU, green RAM and red graphics bars; monochrome adapters use dithering.
Bars animate after completion; their final numbers stay exact.

A benchmark averages 3 complete runs. Progress shows the run and workload.
The separate Graphics Lab contains nested window objects, buttons and a
3D viewport. The drawing area doubles to 256x128 on taller screens; CGA
keeps 256x64. Three graphics tiers range from basic primitives through larger
blits/line/text fields to projected wireframe and flat-shaded cubes, patterned
blits, scrolling, nested moving windows and actual container resize/repaint.
Each 3D row draws 48 rotating frames (four revolutions); a Mandelbrot row
computes 64x32 points up to 24 iterations and fills the drawing area.
The REDLINE report window fits the viewport and is not resizable.
Detailed and REDLINE.TXT retain individual samples, means and method flags.

MHz is labelled by source: CPUID nominal, measured TSC, or two independent
book-timing CPU estimates. TSC is not always the actual core clock. Unknown
clones have no trustworthy MHz formula; unavailable is better than a guess.
CPU generation cannot identify every vendor before CPUID. The legacy Cyrix
DIV fingerprint is shared by TI chips and is labelled accordingly.

RAM reports distinguish BIOS conventional/usable RAM from OS free memory.
An AH88 firmware report may cap at 15/64MB; E820 usable memory excludes
reserved ranges and is not a physical DIMM count. No destructive RAM probe.
BIOS ports/date/equipment and OS video/FPU information are read-only.

REDLINE has two tabs across its top: Benchmark (all of the above) and
Tune, for AT-class boards. Tune fingerprints the BIOS, probes for the
chipset read-only, names what it found, and times an 8-bit read from the
screen, a PicoMEM and each option ROM in ns a byte. The drop-down names the
chipset instead, and your pick wins. An 8088 or 8086 touches no port at
all: an XT's bus has nothing to tune. Beside Chipset, the Target drop-down
says what Tune sets the bus to:

  Fastest tested  tries fewer wait states one bus clock at a time, reading
                  the screen, a slow option ROM, a PicoMEM, the floppy
                  status and an I/O write back with interrupts off, stops
                  at the first wrong byte and SETS the fastest that read
                  right. The default.
  Mfg spec    the chipset data sheet's value (R4 10 on a WD FE3600,
              4 waits on a C&T NEAT).

The framed field after them says what the bus runs at now: Stock (the
BIOS's own timing), Tuned: n waits or Mfg spec: n waits. With a ~ it was
read off the bus rather than set this session: REDLINE times the CPU clock
and the 8-bit read, and the chipset's data book turns the two into a wait
count - so it is right after a reboot, or after RLAPPLY, too. The buttons:

  Tune          sets the Target.
  Save setting  writes SYSTEM\APPDATA\REDLINE.CFG on the BOOT disk.
  Save report   writes the page to CHIPSET1.TXT, CHIPSET2.TXT...
  Quit          closes REDLINE.

On a WD FE3600 board (the Packard Bell Victory 286) Tune sets R4, the 8-bit
wait states; on a C&T NEAT board (the Packard Bell PB286LP) the 8-bit wait
states in register 62h. What Fastest tested cannot read back - video
writes, the keyboard controller, the clock chip, serial ports, an 8-bit
disk card - is why Mfg spec is there: if anything misbehaves, Tune to it,
or reset, and keep backups before you Save a tuned setting. A chipset the
probes did not find asks once before the first write, and one nobody has
run Tune on yet warns once before the first sweep: the worst a sweep
itself does is hang the machine until a reset. Save report
writes everything the page measured, and the sweep step by step - send it
in, and that board is one report from being supported. Nothing is permanent: a
reset restores the BIOS's own timing - so after each boot, open
SYSTEM\RLAPPLY.O88 on the system disk. It has no window: it puts the saved
setting back, refuses on any other board or BIOS, and says what it did on
the menu bar.

Host: make redline / make redlinedisk / make redline-profile
Research, measurement definitions and reference provenance:
docs/REDLINE.md and apps/redline/reference.json in the source tree.
