REDLINE 1.0 - CPU + Graphics Performance Lab for os8088

Open REDLINE.O88. Summary shows a large CPU/RAM score, system snapshot and
graphical comparison bars. Detailed keeps every inventory field, raw timing,
method flag and comparison from the complete report. Compare expands the bars.
Use the bottom buttons or U/D/C to select views; Tab cycles them. Press R or
Run to benchmark. S/Save writes REDLINE.TXT in the package's current folder.
PgUp/PgDn page short screens. Detailed also supports arrows and Home/End.
F1 opens Detailed at the report's provenance. Q/Quit closes the package.
Close other apps and keep the pointer still when comparing runs.

Every test performs identical work on every CPU. CPU instruction mixes,
2KB RAM copy/fill and OS drawing/text/blits have separate timings.
1000 on the PC index is the recorded MartyPC 4.77 MHz IBM 5150 reference.
Graphics indices use the matching CGA, Hercules or VGA mode reference.
Other modes retain raw timings without borrowing a different mode baseline.
The Summary headline averages the six CPU/RAM indices, with equal weight.
Bars start at a 100x ceiling and double for faster results. VGA uses blue
CPU, green RAM and red graphics bars; monochrome adapters use dithering.
Bars animate after completion; their final numbers stay exact.

A benchmark averages 3 complete runs. Progress shows the run and workload.
The separate Graphics Lab contains nested window objects, buttons and a
3D viewport. Three graphics tiers range from basic primitives through larger
blits/line/text fields to projected wireframe and flat-shaded cubes, patterned
blits, scrolling, nested moving windows and actual container resize/repaint.
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

Host: make redline / make redlinedisk / make redline-profile
Research, measurement definitions and reference provenance:
docs/REDLINE.md and apps/redline/reference.json in the source tree.
