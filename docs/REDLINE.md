# REDLINE: research, workloads and reference PC

REDLINE is a native os8088 CPU and graphics performance lab. Build with
`make redlinedisk`, open `REDLINE.O88` from drive B:, press **R**, then **S**
to save `REDLINE.TXT`. It also ships on the everything application set.
The package uses the existing OS menus, About handler, opaque text, report
pagination and shared benchmark timing machinery. It costs no resident kernel
memory. The dedicated floppies carry the same executable in all four sizes.

Summary is the default dashboard: framed performance, system snapshot, results
and hardware-details panels, large bitmap digits, colored VGA bars (blue CPU, green RAM, red graphics),
monochrome dithered bars and
bottom controls. Detailed retains the complete original report, including every
inventory field, both clock estimates, raw timing counts, method flags and
numeric indices. Compare gives the bars the window's full width. Saving from
any view always writes the complete report. The report window fits the viewport;
the temporary Graphics Lab owns resize and scene stress, and is destroyed after
each benchmark. Completion animates bar growth with the UI timer; the numeric
results are final throughout, and no animation runs inside a timing bracket.

Use the buttons or **U / D / C** to select Summary / Detailed / Compare; **Tab**
cycles the views. **R / S / Q** run, save and quit. **F1** opens Detailed at the
report's provenance. PgUp/PgDn page compact dashboard results or the Detailed
report; arrows and Home/End retain their report behavior in Detailed. The layout
uses the live content dimensions. CGA's short screen shows compact panels and
paged results; all facts remain available in Detailed.

![REDLINE Summary dashboard on VGA](redline-summary.png)

![REDLINE compact Summary on the 4.77 MHz CGA reference](redline-cga.png)

![REDLINE Detailed report on CGA, showing its last page](redline-scores.png)

![Live Graphics Lab rendering a projected flat-shaded cube](redline-shaded.png)

![Graphics Lab with nested windows, button and 3D viewport objects](redline-lab.png)

## What the period software looked like

DOS tools commonly put a hardware summary, numeric scores and comparison bars
on a single text screen. Landmark had CPU, FPU and video bars, with results
relative to a reference machine. Its clock-looking values could describe
*equivalent performance*. Kingston's own accelerator installation guide says
a 20 MHz 386SLC could score around 45 MHz and distinguishes the CPU CLOCK field
from the performance bar. The board maker recommends running directly in DOS
because multitasking changes the result. [Kingston SLC/Now! guide, Appendix A](https://www.ardent-tool.com/CPU/slcnowd1.pdf)

An actual PC-SPRINT board owner's DOS captures show Landmark, Norton SI,
CheckIt, MIPS and other tools on the same NEC V20 PC, comparing stock and turbo
modes. Landmark presents CPU/FPU/video comparisons; CheckIt combines hardware
inventory and diagnostic menus with CPU, math, video and disk comparisons.
The captures are useful visual references for the dense text, menu bars,
numeric tables and horizontal bars. Version numbers and reference machines
matter: similarly named scores need not share a scale.
[PC-SPRINT owner's screenshots and measurements](https://github.com/reeshub/pc-sprint#my-benchmarks)

Windows 3.1 tools added ordinary Windows title bars, menus, dialogs and reports.
WinTach's main dialog groups Word Processing, CAD/Draw, Spreadsheet and Paint
beside CPU/display/driver/color-depth facts, with a relative performance result
below. A contemporary graphics-card review reproduces the WinTach screen and
runs WinTach, WindSock and WinBench at different color depths, obtaining different
rankings. [1994 review and WinTach screen, Popular Electronics](https://www.worldradiohistory.com/Archive-Poptronics/90s/94/PE-1994-11.pdf)

WinBench 3.11 measured the Windows graphics stack, not just writes into video
RAM. A preserved *actual program report* enumerates arcs, lines, pixels, region
fills, monochrome/color BitBlt, alignment, raster operations, DIB conversion,
stretching and text/font combinations. It records the driver, resolution,
color depth and running tasks. It reports Graphics WinMark in millions of
pixels/second and Disk WinMark in thousands of bytes/second; the component
results remain visible separately. These units are specific to its test mix.
[WinBench 3.11 report comparing Windows 3.1/3.11](https://groups.google.com/g/comp.os.ms-windows.apps/c/s3hOxHkJOsc)

REDLINE borrows those hardware facts, workload tables and comparison bars.
It measures os8088's graphics API rather than pretending to produce a WinMark,
a Norton SI score or a Landmark equivalent MHz. Disk and FPU throughput are
outside this package's CPU/graphics scope; FPU *presence* is still reported.

## What REDLINE tests

Every machine runs identical work and iteration counts. The count column uses
PIT-count equivalents: net counts for method P, gross tick counts for method T.
`us/op` is per complete body, **not per instruction or pixel**.

| Workload | Work per body | Bodies per row |
|---|---|---:|
| ALU | 128 chained ADD/XOR pairs | 64 |
| Rotate | 128 register rotations by four bits | 64 |
| Multiply | 64 fixed 16-bit multiplies with operand reload | 64 |
| Divide | 64 fixed 32/16-bit divides, bounded quotient | 64 |
| RAM copy | 2,048 bytes with REP MOVSW | 32 |
| RAM fill | 2,048 bytes with REP STOSW | 32 |
| Rectangle fill | OS API, 64x32 pixels | 24 |
| Horizontal line | OS API, 256 pixels | 64 |
| Rectangle outline | OS API, 64x32 pixels | 24 |
| Opaque text | 31 cells, letters/digits/punctuation | 16 |
| Monochrome blit | 64x32, alternating bit pattern | 32 |
| Packed-color blit | 64x32, uniform color 1 | 16 |

A benchmark runs the full 24-workload suite **three times**. Scores divide the
reference by the arithmetic mean of each workload's three raw measurements,
rounded down in PIT-count units. Progress identifies the current run and row;
the native Graphics Lab makes the drawing visible. Detailed and saved reports
retain all three samples, means and sample timing flags, plus sample 3's complete
original per-body timing/method table.

The graphics tiers add these fixed bodies:

| Tier | Workload | Work per body | Bodies per run |
|---|---|---|---:|
| 2 | Fill | 256x64 pixels | 8 |
| 2 | Line field | 16 horizontal 256-pixel lines | 8 |
| 2 | Nested frames | Eight inset frames inside 256x64 | 8 |
| 2 | Text grid | Eight 31-cell opaque lines | 8 |
| 2 | Mono blit | 128x64, alternating bits | 8 |
| 2 | Packed blit | 128x32, uniform color 1 | 8 |
| 3 | Wireframe cube | Eight integer rotation/perspective projections, 12 Bresenham edges | 8 |
| 3 | Flat-shaded cube | Eight projected vertices, six scan-converted triangles; three face shades | 8 |
| 3 | Patterned blit | Generate, blit and restore a 128x32 packed pattern | 4 |
| 3 | Scroll | Byte-aligned 256x64 rectangle up/down four rows | 8 |
| 3 | Nested moving windows | Three scene renders with a child sliding; window/button/3D objects inherit parent coordinates | 4 |
| 3 | Lab resize/repaint | Shrink the native lab by eight pixels and restore it; method T | 4 |

Graphics include integer CPU work, renderer, API arrival and video bus. They
operate in the lab's clipped 256x64 canvas, using OS slots rather than raw VRAM.
The native window resize also measures the OS's damage repair and repaint.
The scene's child windows are app-rendered objects: os8088 provides top-level
native windows, and the lab owns their logical nesting. RAM buffers are package
owned. Register-heavy rows still include instruction fetch and setup.

`1000 = baseline`, larger is faster. CPU/RAM use the common 4.77 MHz PC
reference; graphics use the mode-matched CGA, Hercules or VGA reference. Other
modes keep their raw timings and withhold graphics indices. The Summary headline
averages only the six CPU/RAM indices with equal weight.

All bars share a ceiling of **at least 100x**, doubling until it exceeds the
largest available score (saturating at the index type's maximum). Axis labels
follow that ceiling. Positive subpixel bars get one pixel at completion. The
text report uses the same scale, capped at 50 character blocks. Numeric ratios
retain the full 32-bit index range, including values above 99.99x. Unresolved
timings have no invented score. Animation changes painted widths only.

`tests/benchlib.inc` is the single shared timer/report implementation, also used
by GFXBENCH/SYSBENCH. It latches PIT channel 0 without changing its programming,
subtracts the empty-body measurement, accumulates in 32 bits and services IRQs
between bodies. Method `T` samples (original-table flag `t`), including `w`
fallbacks when a body outgrew the PIT interval, use coarse ticks; such a run
should not be treated as a high-resolution score.
A mean can contain both net-PIT and gross-tick samples; every sample's flag
remains visible. Calibration checks the noise bound only for rows whose samples
all used PIT, and records the relative spread for every row. The screenshot/report
formatting and disk saving happen outside measured spans.

## CPU identity, MHz and RAM: what can be known

The existing OS tier gates all newer opcodes. On early CPUs, REDLINE checks
shift-count masking, the repeated-string restart defect, and the prefetch queue.
The NEC test requires an observed timer interrupt, so an uninterrupted operation
cannot prove NEC. Self-modified instruction bytes and FLAGS are restored on every
run. The historical techniques and their assumptions are described alongside
original source in [Cosmo's processor detection analysis](https://cosmodoc.org/topics/processor-detection/).

386+ probes test AC and ID before using CPUID. The report includes vendor,
family/model/stepping, signature, feature bits and the brand string where
available. Cyrix and both Transmeta vendor spellings are recognized. Early
Intel-compatible/AMD/IBM parts without unique public identification cannot
always be told apart. The DIV-flags fingerprint identifies a Cyrix/TI-compatible
behavior, not an exact model. REDLINE never enables disabled CPUID or writes
CPU configuration ports. [Linux's Cyrix detection and configuration code](https://github.com/torvalds/linux/blob/v4.4/arch/x86/kernel/cpu/cyrix.c)

Clock sources stay distinct: CPUID leaf 16 gives **nominal CPU MHz**; a timed
TSC gives **TSC MHz**. An invariant TSC, turbo or Transmeta translation can
make that differ from the core clock. Intel explicitly says leaf 16 is
specification information rather than a measurement. For pre-TSC 808x/286/386
classes, MUL and DIV yield separate book-timing estimates. Unsupported families
say clock unavailable. Raw timings and indices remain useful.
[Intel processor-frequency semantics](https://cdrdv2-public.intel.com/874239/252046-082-sdm-change-document.pdf)

Conventional RAM is INT 12h's firmware report. E820 sums firmware type-1 usable
ranges when available; it excludes reserved memory and is not a DIMM inventory.
Fallback INT 15h/AH=88h extended KB can cap at 15/64MB. No result is invented if
firmware declines. OS free RAM, largest conventional run and free extended KB
are separate queries. BIOS date/model, equipment word, COM/LPT bases, OS
FPU presence and the window's actual adapter/geometry round out the inventory.

## The 4.77 MHz reference

`os8088_redline_pc` is an IBM 5150 with a user-supplied IBM ROM.
`os8088_redline_pc_gla` uses bundled GLaBIOS and is runnable from a clean checkout.
The CGA profiles and `os8088_redline_herc_gla` / `os8088_redline_vga_gla`
share the machine type's Intel 8088 at `(315/22)/3 = 4.772727… MHz`, 640KB
conventional RAM, zero RAM wait states, two 360KB drives and a
serial mouse. The launcher configuration has **turbo=false**. No sound card,
CPU upgrade, RAM upgrade overlay or disk accelerator is present. The graphics
profiles differ by adapter: dynamic CGA 640x200x1, dynamic Hercules 720x348x1,
or VGA 640x480x4 with MartyPC's bundled video BIOS.

MartyPC's CPU models prefetch, bus activity and instruction timing and is
validated against physical 8088 instruction tests. That supports an accurate
reference, but **cycle perfect physical PC equivalence is not a guarantee**:
the upstream README publishes residual discrepancies, and peripheral/ROM
choices matter. NEC timing is explicitly not equally accurate.
[MartyPC accuracy and device support](https://github.com/dbalsom/martypc)

```
make marty redlinedisk build/os8088-360.img
python3 tools/redline_profile.py
# with your IBM BIOS dump installed:
python3 tools/redline_profile.py --machine os8088_redline_pc
# deliberately regenerate the shipped reference:
python3 tools/redline_profile.py --calibrate
make redlinedisk
python3 tools/redline_profile.py --machine os8088_redline_herc_gla --out build/redline-profile-herc --calibrate
make redlinedisk
python3 tools/redline_profile.py --machine os8088_redline_vga_gla --out build/redline-profile-vga --calibrate
make redlinedisk
```

The runner drives genuine UI commands, executes three trials of three complete runs, checks the
CPU/RAM and timing results, saves the guest file and extracts it through the
independent FAT reader. Each trial averages three runs; reference counts are each workload's median
across the three trial means. Retained
trials expose variation from refresh, beam phase and timer quantization.
`apps/redline/reference.json`, `reference-herc.json` and `reference-vga.json`
record measured package/kernel/emulator/BIOS hashes, machine/config, emulator
pin, individual samples, method flags and trial means. VGA also records its
video ROM hash. The corresponding `baseline*.inc` files contain the actual
counts; Hercules/VGA embed only their graphics rows, keeping a common CPU base. Calibration changes the package's embedded reference; the measured
image hash therefore describes the calibration input, not the rebuilt output.
Keep the two distinct when auditing provenance.

Host results and screenshots land in `build/redline-profile/`. The emulator
reference has not been validated on physical 8088/NEC/Cyrix/Transmeta machines.
The package should report what is reliably queryable on those machines rather
than claiming untested exact model identification.

## Validation

`tests/redline.py --host` independently walks all four FAT12 media, matches the
package and checks reference counts against the assembled workload hash.
`--machine os8088_redline_pc_gla` runs and saves the native report twice, checks
reference indices and executes large-denominator arithmetic on the 8088.
The Hercules and VGA machines exercise the same UI and check their matching
graphics indices, actual VGA bar colors, and unchanged report-window geometry. Native checks also verify averaging, scale/decimal boundaries, unsupported-mode
fencing, timer completion, lab cleanup, the six-row headline calculation, view
switching, held-button behavior, slide-off cancellation, Detailed End/F1
navigation, compact result pagination and the Quit button. `--modern` boots
the shipped probe code under QEMU BIOS for guarded
286-class/486 paths, Pentium/Pentium III, E820 RAM and TSC measurement. Cyrix
and Transmeta cases use CPUID vendor overrides, not those physical processors.

`--nec` executes the shipped early CPU probe twice on MartyPC's V20 with an
independent real PIT/IRQ0 harness. The native V20 row also boots the desktop,
runs and saves REDLINE, repeats the CPU probe and exercises every view and
button gesture. Main's MartyPC mode-flag protection patch makes that boot
possible. These checks validate NEC identity and restoration of self-modified
code; MartyPC's V20 timings are not a cycle-accurate NEC reference. The V20
profile is for validation, never calibration.

`--scene` captures the live VGA lab at real workload boundaries and verifies
wireframe versus shaded output, multiple face colors, nested parent-coordinate
inheritance and a child window's measured movement.
