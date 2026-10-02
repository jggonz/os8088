REDLINE 1.0 - CPU + Graphics Performance Lab for os8088

Open REDLINE.O88. Press R or click to run. Bench menu also offers Run,
Save Report and Top/About. Press S to save REDLINE.TXT in the package's
current folder. Arrow keys, Home/End and PgUp/PgDn browse the report.
Close other apps and keep the pointer still when comparing runs.

Every test performs identical work on every CPU. CPU instruction mixes,
2KB RAM copy/fill and OS drawing/text/blits have separate timings.
1000 on the PC index is the recorded MartyPC 4.77 MHz IBM 5150 reference.
Twenty # marks is baseline speed. Graphics indices require CGA, because
VGA and Hercules run different drivers and modes. Timings still run there.

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
