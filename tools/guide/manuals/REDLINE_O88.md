# REDLINE

REDLINE measures how fast your PC is. It times 25 fixed workloads, from
plain processor arithmetic to drawing rotating 3D cubes, and compares each
result with the same workload on the original IBM PC: an 8088 at 4.77 MHz.
A score of **1.00x** means "as fast as that PC", **2.00x** means twice as
fast. It also lists what it found in the machine: the processor, the memory,
the video adapter and the BIOS.

Its second tab, **Inspect**, is a survey of the machine in eight pages:
the processor and its features, the memory and what uses it, the displays,
the drives, the ports, every interrupt line and what is installed. On a 286
or 386, its third tab, **Tune**, can identify the chipset and make the
expansion bus faster for 8-bit cards.

![REDLINE's Summary after a benchmark on a 4.77 MHz XT with VGA. Every score is close to 1.00x, because this machine is the reference PC.](shot:redline-summary){full}

## Starting REDLINE

REDLINE is **REDLINE.O88**, at the top of its own floppy, the REDLINE disk.
Open the disk and double-click it. The window fills most of the screen and
cannot be resized.

## Running the benchmark

Press **Run**, the `R` key, or choose **Bench > Run**. REDLINE works through
all 25 workloads three times and averages the three runs. While it works, a
second window, **REDLINE Graphics Lab**, appears and the drawing tests run
inside it. A progress bar shows how far the run has got.

![A benchmark in progress: the Graphics Lab draws one of its tests while the bar shows progress.](shot:redline-running){full}

For steady numbers, close other programs first and keep the mouse still
while the benchmark runs. Moving the pointer makes the machine do extra work
that ends up in the timings. A run takes a while on a slow machine; when it
ends, the Graphics Lab closes and the bars grow to their final lengths.

Press **Run** again at any time afterwards to take a fresh set of results.

## Reading the results

The workloads fall into three groups:

- **CPU**: four instruction mixes, called ALU, Rotate, MUL and DIV.
- **RAM**: copying and filling 2KB of memory.
- **Graphics**: nineteen drawing tests in three tiers. Tier 1 is the basic
  drawing calls (fill, line, frame, text, two kinds of picture). Tier 2 does
  the same at a larger size. Tier 3 is harder work: a wireframe cube and a
  shaded cube, each spun through four turns, a Mandelbrot picture, patterned
  pictures, scrolling, windows moving over each other, and the Graphics Lab
  window being shrunk and restored.

Each group has its own average, shown as **CPU average**, **RAM average**
and **Graphics average**. The big number at the top, **CPU + RAM
Performance**, is the average of the six CPU and RAM scores. REDLINE does
not add the graphics into one overall score.

Graphics scores are compared with the same screen mode on the reference PC:
CGA at 640x200, Hercules at 720x348, or VGA at 640x480 in 16 colours. On any
other mode the graphics timings are still measured and kept, but the
Graphics average says **n/a**.

On VGA the bars are blue for CPU, green for RAM and red for graphics; on a
monochrome screen they are patterned. The bar scale runs from 0 to the
highest score rounded up, plus 5x.

**System Snapshot** and **Details**, on the right, list what REDLINE found:
the processor and its estimated speed in MHz, the memory the BIOS reports
and the memory free in the system, the video adapter and screen size, the
BIOS date and model byte, and the serial and parallel port addresses. A
value REDLINE could not find out reliably is shown as `n/a` rather than
guessed.

## The three views

The buttons along the bottom choose a view, and so do the keys.

- **Summary** (`U`) is the dashboard shown above.
- **Compare** (`C`) gives the whole window to the workload bars.
- **Detailed** (`D`) is the full text report: every inventory line, every
  workload's raw timing and how it was taken, and every one of the three
  samples. It is several pages long.

![Compare: every workload's bar across the window.](shot:redline-compare){full}

`Tab` steps through the three views in turn. `F1` opens Detailed at its
first page, which explains how the numbers were measured.

![Detailed: the report, with the hardware inventory at the top and the raw timings below.](shot:redline-detailed){full}

## Saving the report

Press **Save...**, the `S` key, or choose **Bench > Save Report**. REDLINE
writes the Detailed report as **REDLINE.TXT** in the folder it was opened
from, ready to read in Note Pad ([[NOTEPAD.O88]]) or to copy to another computer.

## The Inspect tab

The **Inspect** tab (`I`, or **Bench > Inspect**) lists what is in the
machine and what os8088 made of it. It only reads: it changes nothing. The
first time it opens it reads the rest of itself from REDLINE.O88, so the
disk REDLINE came from must still be in its drive.

A row of buttons under the tabs picks a page, and so do the keys `1` to `8`,
`Left` and `Right`, and `Tab`:

| Page | What it shows |
|---|---|
| Summary | The machine, its BIOS, processor, coprocessor and clock speed, the memory, the display, how many drives and ports, which os8088 kernel is running, how long it took to start, the date and time, and how many programs are running |
| CPU | The processor's class, its bus width and prefetch queue on an 8088 or 8086, its vendor, name, family, model, stepping and features on a processor that can name itself, its speed and how it was measured, the memory features in use, and the coprocessor: an 8087, a 287, a 387 or later, or one built into the processor |
| Memory | Bars showing how the conventional memory is used: the system, its buffers, the memory programs can have, what is taken and what is free. Then extended and expanded memory, and a map of the area above 640KB: the video memory, every option ROM with its size and whether its checksum is right, and the system BIOS |
| Video | The adapter, screen size and colours, the BIOS screen modes, every display and the full-screen modes os8088 can use on each, and what an EGA or VGA BIOS reports about itself |
| Drives | How many floppy and fixed disks the BIOS found, every os8088 drive letter and how it is reached, which one the machine started from, the size and free space of the current drive, and which disk drivers are loaded |
| Ports | Each serial port with its chip, speed and settings, and the signals from whatever is plugged in; each parallel port with the printer's state; the game port; the keyboard |
| IRQ/DMA | Each interrupt line: what it is normally for, whether it is switched on, and whether the BIOS, an option ROM, os8088 or one of its drivers answers it. Then the DMA channels |
| Devices | Sound, the pointer, the network card, expanded memory, the clock, what the BIOS was told at power-on, every driver loaded and every program running |

The page scrolls like Tune's, with the arrow keys, `PgUp`, `PgDn`, `Home`
and `End`, or the scroll bar. Some things are deliberately not read, and the
page says why where they would go: the geometry of the disks, whether a
serial port has a 16550's buffer, and what a DMA transfer is doing.

The buttons along the bottom:

| Button | Key | What it does |
|---|---|---|
| Refresh | `R` | Reads everything again |
| Save report | `S` | Writes all eight pages to `INSPECT1.TXT`, then `INSPECT2.TXT` and so on, in the current folder |
| Quit | `Q` | Closes REDLINE |

## The Tune tab

The **Tune** tab (`H`, or **Bench > Tune**) is for AT-class machines: 286
and 386 PCs with an ISA bus. **Benchmark** (`B`) goes back.

![The Tune tab on an 8088. There is nothing to tune on this machine, so Tune and Apply on boot are greyed.](shot:redline-tune){full}

An AT chipset slows every 8-bit card access down to the speed of the first
IBM AT, and many BIOSes slow it further. Tune finds out how fast your
board's bus can really go. When you open the tab, REDLINE reads the BIOS,
looks for a chipset it knows and times an 8-bit read from the screen, from
a PicoMEM if there is one, and from each option ROM. Opening the tab writes
nothing.

Along the top:

- **Chipset** names the chipset REDLINE found. If it found none and you
  know what your board has, pick it here. Your pick wins.
- **Target** is what **Tune** sets the bus to. **Fastest tested**, the
  default, steps down one wait state at a time, reads back the screen, a slow
  ROM, a PicoMEM, the floppy controller and an I/O write after each step,
  and keeps the fastest step that read right. **Mfg spec** uses the
  chipset maker's published value, which is safe on any board with that
  chipset. **Bus clock x2** is an experiment for C&T NEAT boards only: it
  doubles the bus clock and may hang the machine. Put a disk in A: first.
- The framed field shows what the bus runs at now: **Stock: BIOS timing**,
  **Tuned: n waits** or **Mfg spec: n waits**.

The page below lists what REDLINE detected, what is set now, the timings it
measured and a short help text. Scroll it with the arrow keys, `PgUp`,
`PgDn`, `Home` and `End`, or the scroll bar.

The buttons along the bottom:

| Button | Key | What it does |
|---|---|---|
| Tune | `T` | Sets the bus to the Target |
| Apply on boot | `K` | Saves the setting and makes Redline Apply put it back at every start-up |
| Save report | `S` | Writes this page to `CHIPSET1.TXT`, then `CHIPSET2.TXT` and so on, in the current folder |
| Quit | `Q` | Closes REDLINE |

A chipset the probes did not find asks **Not detected. Write to it anyway?**
before the first write. A chipset nobody has tuned on a real board yet warns
once before the first sweep.

Nothing here is permanent. A reset puts the BIOS's own timing back, which
is why **Apply on boot** exists: it writes the setting to
`SYSTEM\APPDATA\REDLINE.CFG` on the boot disk and adds a start-up shortcut
for **Redline Apply** (`SYSTEM\RLAPPLY.O88`), which puts the setting back
after every boot and refuses on any other board or BIOS. See Redline Apply's
own manual, [[RLAPPLY.O88]], for its messages. On an 8088 or 8086 the Tune tab touches no
hardware at all: an XT's bus has nothing to tune.

## The Bench menu

![The Bench menu.](shot:redline-menu){full}

| Item | Key | What it does |
|---|---|---|
| Run | `R` | Runs the benchmark |
| Save Report | `S` | Writes REDLINE.TXT |
| Summary | `U` | The dashboard view |
| Detailed | `D` | The full report |
| Compare | `C` | The workload bars, large |
| Inspect | `I` | The Inspect tab |
| Tune | `H` | The Tune tab |

The **REDLINE** menu has **About REDLINE** and **Close**. The **Quit**
button, or `Q`, also closes REDLINE.

## Keys

| Key | What it does |
|---|---|
| `Tab` | Next view: Summary, Detailed, Compare |
| `F1` | Detailed, at the first page |
| `B` | The Benchmark tab |
| `I` | The Inspect tab |
| `H` | The Tune tab |
| `Up`, `Down`, `PgUp`, `PgDn`, `Home`, `End` | Scroll the results in Summary and Compare, or move through the report in Detailed |
| `Space` | The next page of results, in Summary and Compare |

## Files

| File | Written by | Where |
|---|---|---|
| REDLINE.TXT | Save | The folder REDLINE was opened from |
| INSPECT1.TXT, INSPECT2.TXT... | Save report, on the Inspect tab | The current folder |
| CHIPSET1.TXT, CHIPSET2.TXT... | Save report, on the Tune tab | The current folder |
| REDLINE.CFG | Apply on boot | `SYSTEM\APPDATA` on the boot disk |

## Good to know

- The scores compare workloads one at a time. There is no single number for
  the whole machine.
- Every workload does exactly the same work on every machine, so two
  reports from different PCs can be compared line by line.
- The reference figures were measured on an emulated IBM PC 5150 at
  4.77 MHz with 640KB, using each adapter's own screen mode.
- A test too quick to time on a fast machine shows `Timing below
  resolution` rather than a made-up number.
- Before you tune, keep backups. Fastest tested cannot check video writes,
  the keyboard, the clock, serial ports or an 8-bit disk card. If anything
  misbehaves after tuning, Tune to **Mfg spec**, or reset.
