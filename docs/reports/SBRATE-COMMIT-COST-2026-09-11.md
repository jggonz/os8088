# What ONE scroll-bar drag commit costs, on the machine this OS is for

**Taken 2026-09-11**, on `claude/scroll-live-286` at the commit that added
SPEC.md 13.10.5.4.1, in an Ubuntu container. **Instrument:** MartyPC
(`make marty`), machine `os8088_5150_cga_gla` — a cycle-accurate 4.77 MHz
8088 with a CGA — through `os88marty.flicker`, which samples the glass once
per displayed frame and is what PERFORMANCE.md Part 5's rows were taken with.
Cycles are the emulator's own count between the first and last frame that
changed a pixel; the millisecond column is those cycles at 4.772727 MHz.

**This file is a MEASUREMENT and is true of that tree and no other.** It is
not maintained against a later one; a second reading is a new file.

## Why it was taken

SPEC.md 13.10.5.4 sized every bar's throttle from PERFORMANCE.md Part 5 rows
for *adjacent* operations — a Disk window's full repaint, a Browser line, a
Word track click. 13.10.5.4.1 needed the cost of **the commit a dragged thumb
actually makes**, per bar, because that is the quantity the rate has to clear.

## The readings

| bar | what was dragged / paged | frames | cycles | ms |
|---|---|---:|---:|---:|
| **Disk window** | 31-file listing, `fit` 5, default window | 8 | 557,845 | **116.9** |
| **Note Pad** | 200-line document, thumb top → bottom | 22 | 1,831,603 | **383.8** |
| **TeXPad** | `GUIDE.TEX`, PageDown, first page | 22 | 1,991,429 | **417.3** |
| **TeXPad** | ...and deep in the document | 12 | 955,083 | 200.1 |
| **Word** | `WELCOME.DOC`, PageDown | 9 | 1,433,923 | **300.4** |
| **Word** | ...and deep | 9 | 1,513,145 | 317.0 |

Every row settled inside the capture window, so no count was taken against a
moving target.

## Three things in it worth keeping

1. **The Disk window is the CHEAPEST bar, not the dearest.** 13.10.5.4's own
   argument is written about it and the arithmetic before this reading had it
   at the top of the table. It is a third of TeXPad's. The reason is `fit`:
   the default Disk window shows **5** rows, and a commit's cost is
   overwhelmingly *rows × cells per row*.

2. **Which means the number is a property of the WINDOW, not of the
   program.** A grown Disk window, or the same listing on a 640x480 VGA,
   multiplies its rows and its cost with them. Every figure here is the
   DEFAULT window on a 640x200 CGA, which is the cheap end of each bar's
   range — so a rate cut from them is cut from the cheap end too.

3. **A PageDown is the same commit and needs no bar geometry.** Past one
   windowful the delta is ≥ `fit`, so no incremental blit applies and the
   view repaints whole, which is what a dragged thumb always does. That is
   how TeXPad and Word were priced without knowing where their thumbs are;
   Note Pad was priced on a real drag and the two methods agree in scale.

## What it does NOT say

**Nothing here is a 286 reading.** MartyPC is an 8088 for ever
(docs/TESTING.md's closed list, entry 1), so the step from these numbers to a
286 budget is a tier factor nothing in this tree can measure. 13.10.5.4.1
records what the factor would have to be for each candidate rate and which
field reading exists.
