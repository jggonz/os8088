# What can actually be tested, and where

**DEVELOP ON MARTYPC. QEMU IS A FALLBACK WITH A SHORT LIST.** If what you
are testing runs on an 8088 — which is the whole of this OS bar the 286/386
targets — `make marty` gives you a cycle-accurate 4.77MHz 8088 running a real
period BIOS, with a debugger attached: memory, registers, I/O ports,
breakpoints, single-step and cycle counts, none of it costing the guest a
cycle (docs/MARTYPC-DEBUG.md). It covers **all three** of SPEC.md §39's
adapters, scripted input, screenshots and sound. **And for anything with a
disk in its timing, the 5150 is where the number LANDS — though MartyPC's
floppy now turns (PERFORMANCE.md Sets 35/37) and agrees with the iron's raw
`int 13h` rows to the measurement quantum.**

**Here is the whole of QEMU's remaining list**, stated as a list so that "a
legitimate need" is something you can check rather than something you can
argue yourself into:

1. **286 and 386.** 86Box covers these too, and models the machine rather
   than just the CPU — prefer it where the question is about the machine.
2. **Rung 1 of the hard-disk driver** (SPEC.md §52.1) — the IDE task file
   read directly, which is gated on `CPU_286` because an 8088's `in ax, dx`
   is two 8-bit bus cycles at the same port and loses the drive's high byte.
   QEMU has an ATA disk at 1F0h and a CPU that clears that gate; MartyPC's
   8088 never can. **Rung 0 is MartyPC's now** and is the one the target
   machine actually uses (`os8088_xt_hdd` — an XT-IDE controller whose GPL
   option ROM ships with MartyPC, answering int 13h), so the two are
   complementary and neither replaces the other.
3. **SPEC.md §9.5's awkward mouse cases** — a mouse on COM2, the cross-wired
   IRQ4 card, and a modem chattering on the other port (`MOUSEPORT=com2`,
   `com2irq4`, a socket chardev). MartyPC can put its mouse on either port,
   but the cross-wired and modem cases are not built.

That is the list. **"It is quicker to type" is not on it, and neither is
"I already know the QMP commands."** If you find a fourth entry, add it here
rather than treating the rule as advisory.

That ordering is a **reversal**, and it is written down because the old one
cost a great deal. Everything in this tree was QEMU-first for years, and QEMU
is the emulator that is *furthest* from the target: it runs the guest at host
speed on a CPU that is not an 8088, through SeaBIOS rather than a period ROM,
with no CGA and no Hercules card in it at all. It is excellent at counting
work and it is not a machine. Nearly every entry in docs/FIELD-NOTES.md is
something QEMU showed as fine — and SPEC.md §18.91's `AL` bug is the sharper
shape: QEMU ran the buggy binary *correctly and quickly* and reported
nothing, while the real machine was moving 4.6x the sectors anyone asked for.
A tool that is wrong in the flattering direction does not announce itself.

| reach for | when | why |
|---|---|---|
| **MartyPC** | **the default** — any 8088 machine, any of the three adapters, and any question of the form *what is the machine doing* | cycle-accurate CPU, a real BIOS ROM, real CGA/Hercules/VGA cards, and a debugger that perturbs nothing |
| **QEMU** | the three-item fallback list above, and nothing else | on an 8088, MartyPC covers all three adapters, input, screenshots and sound. QMP and `tools/mouse.py` remain the harness for what is genuinely left |
| **86Box** | a machine that is **not an 8088** (the 286 and 386 targets), real sound cards on a period bus, a second opinion on the video probe | period-correct whole machines, and the widest hardware library |
| **the 5150** | anything with a **disk** in it, and the three defects no emulator shows | docs/FIELD-MACHINES.md |

## The one rule that outranks the table: a disk number lands on the 5150

**MartyPC's floppy used to be a fiction and now it is a model.** Upstream it
modelled no platter at all — a seek completed in the breath it was issued and
a sector arrived the instant it was asked for — which is where Set 11's 30x
came from. `tools/martypc/patches/04-floppy-disk-timing.patch` gives it
rotation, an MFM data rate, a per-cylinder seek and a configurable interleave
(PERFORMANCE.md Sets 35/37, docs/MARTYPC-DEBUG.md):

| `boot ticks`, 360KB `combo.img` | stock | Set 35 | **now** | real 5150 |
|---|---|---|---|---|
| `os8088_5150_cga_gla` (GLaBIOS) | 41 (2.25 s) | 130 (7.14 s) | **175** | — |
| `os8088_5150_herc` (IBM ROM) | — | 222 | **188** | **205** |

`tests/sysbench`'s whole raw `int 13h` block matches the field machine's own
report off that identical image to within one measurement quantum on nine of
thirteen rows, seven of them exactly (Set 37). So the rule is now two rules and
**both** have moved:

- **TIMING**: MartyPC is worth asking. It still is not where a figure LANDS:
  anything going into PERFORMANCE.md Part 9 comes off the 5150. PCem is no
  better and QEMU models none of it.
- **CORRECTNESS**: half of it moved. MartyPC runs the real IBM ROM, so what
  the ROM does is reproduced — §18.91's `AL` bug shows here as **893 boot
  ticks against 188** and 870 sectors in 183 reads against 183 in 24, the
  field's own signature. What a real 765 puts in ST1, or whether a real drive
  returns short, is still the emulator author's belief and still the 5150's
  question, as are interrupt stack depth (SPEC.md §8) and anything QEMU's
  SeaBIOS smooths over (docs/FIELD-NOTES.md 5).

Two caveats. **A disk TIMING comes off an IBM-ROM 5150 and no other class**
(Set 38): the drive is one model and six machines give bit-identical
controller traffic, but GLaBIOS turns an `int 13h` around **1.61x** faster
than the 1982 ROM — enough that nine one-sector reads cost the same as one
whole track there and ten revolutions here. Counts are fine on any machine; a
timing is not. docs/MARTYPC-DEBUG.md carries the per-machine table. And the
`ibm5150_82_v4` machines need an IBM ROM this tree cannot ship, so a container
without one in `tools/martypc/roms/` can run only the GLaBIOS twins — which
is the class you must not take that timing from.

---

This document exists because the opposite keeps getting concluded about
capability. It has happened for Hercules — `docs/HERCULES-TESTING.md` opens by
saying so, and that claim had sat in CLAUDE.md costing people time — and it
keeps happening for sound, for a duller reason: the AdLib and Sound Blaster
recipes are real, committed and mechanical, but they live in the middle of
`docs/SOUND-PLAN.md`, an 850-line *plan*, interleaved with phase history. A
plan document is not where anyone looks to answer "can I test this?", so the
answer people reach is "no".

Every QEMU recipe below was run end to end on a stock QEMU 8.2.2 and the
measured result is quoted with it. If one of them fails, that is a finding
about the tree, not about the emulator.

**This document answers *where a test can run*. [PERFORMANCE.md](../PERFORMANCE.md)
answers *what the target machine costs* — the calibration numbers, the
standing budget every redraw path has already been measured down to, and the
three visible defects the emulator cannot show. Read that one before changing
anything that draws or loops; "Modelling the old machine from a fast one",
below, is the short version of it.**

---

## The regression suite: three tiers and a budget

**`tools/os88test.py` is how you run the tests. `tests/suite.py` is the list
of them.** Before it existed this directory held ninety test scripts and no
way to enumerate them, so running "the tests" meant remembering which ones
existed - and the ones nobody remembered were reliably the ones that had
stopped working. That is `tools/checkdocs.py`'s own lesson one level up: *a
check nobody types has exactly that failure mode.*

```
python3 tools/os88test.py fast      # every build. ~3s, host-side only
python3 tools/os88test.py full      # BEFORE A MERGE. ~1 minute
python3 tools/os88test.py soak      # everything. No budget
python3 tools/os88test.py --list    # what is registered, and why
python3 tools/os88test.py soak -k 'disp*'   # just the ones about displays
```

`make` runs the `fast` tier itself, as a prerequisite of `all`, for the same
reason `checkdocs` is in there: a gate you have to remember is not a gate.
`make test-full` and `make test-soak` are the other two.

### What each tier is for

| tier | budget | what it does | when |
|---|---|---|---|
| `fast` | **30s** (uses ~3) | Host-side only. Reads what `make` just built and checks what breaks SILENTLY. | Every build |
| `full` | **10 min** (uses ~1) | `fast`, plus the eighteen knob kernels and `kern_small`, plus the C toolchain, plus the emulator smoke test. | **Before a merge** |
| `soak` | none | The other sixty-odd gates in `tests/`, one subject each. | When you touched that subsystem |

The tiers are **cumulative**: `full` runs everything `fast` does, `soak`
runs everything.

### The budget is enforced, and that is the feature

**The runner FAILS the tier when the wall clock overruns it**, green rows or
not. A suite with no ceiling grows until it is too slow to run, and a suite
too slow to run is not run - which is the state this repo was already in with
zero seconds on the clock. So adding a row that does not fit is a visible,
failing decision about what to take out or move down a tier, taken by the
author adding it.

Each row also declares its own expected seconds and is reported when it
overruns them. Without that the tier budget is spent by whichever row happens
to run last, and the one that actually got slower is invisible.

### Why `full` is CURATED and not "all of them"

This is the thing to understand before adding a row to it. Measured here, on
a cycle-accurate 5150 in a container:

  * a MartyPC boot to a settled desktop is **7.8 seconds**;
  * the emulator tests in `tests/` are **40-75 seconds each**, because each
    one boots its own machine and then drives a session through it;
  * and they **cannot run in parallel**. Every one drives the debug server on
    127.0.0.1:9001 - one port, one connection, and a second client does not
    error, it HANGS (docs/MARTYPC-DEBUG.md). The runner marks them `serial`
    and gives them one lane behind the host-side rows.

So ten minutes is about **eight** emulator tests, not fifty. That is not a
limitation to engineer away, it is what the machine costs. The honest response
is to say which eight and put the rest in `soak`, where they are still one
command away.

**What earns a `full` row is breadth per second.** `tests/bootsmoke.py` is the
model: eight seconds, and it exercises the boot sector, FAT12, the `int 13h`
splitter, adapter detection, the heap ladder, `drv_boot` and the first paint -
so it fails for almost any serious regression, wherever it was. A row that can
only fail for one narrowly-scoped reason belongs in `soak`, next to the change
that would break it.

### The host-side tests, and what each is defending

They are in `tests/unit/`, they need nothing but `python3`, and between them
they make about 4,300 assertions in two seconds. Each one exists because of a
failure this tree has actually had:

| row | what it would have caught |
|---|---|
| `api-abi` | The API table decoded out of `build/kernel.bin` and compared with `apps/os88api.inc`. **The silent merge collision** CLAUDE.md asks to be checked by hand after every merge: two branches appending to the same tail merge CLEAN, and the result is two cells pointing at each other's addresses with nothing to say so until a package calls one and gets the other. |
| `mirror` | A constant written down in two files must agree in both - there is no linker here to notice. It found `SPL_RESIDENT` at 9 in `splash.inc` and `boot.asm` and **8 in `boot/boothd.asm`**, which made the hard-disk boot sector tick the splash into a module not yet loaded. |
| `image` | The seven shipped floppies walked by an **independent** FAT12 reader: `KERNEL.SYS` contiguous (the boot sector has no chain walker - a fragmented kernel builds, verifies and mounts cleanly and does not boot), a byte-exact standard BPB (SPEC.md 19.3 - DOS reads a floppy's geometry from its own table), SPEC.md 19.6's attributes. |
| `pkg` | Package, driver and module headers - and every file on every image proved byte-identical to the artifact in `build/` it came from. *"A stale image is indistinguishable from a change that did nothing."* |
| `diskverify` | The tree's own fsck, which `make` ran on the C, Word and RunCPM disks and on **none** of the seven the default build ships. |
| `asmrules` | Unreachable code after an unconditional jump - CLAUDE.md's own worked example is the tracker shipping *"two `jmp short`s in a row"* which assembles, boots, and puts a field bug back. And that a `cpu 8086` is reachable from every root; `boot/boot.asm` had none at all. |
| `registry` | Every test in `tests/` is registered in a tier or says why not. **This is the row that stops the suite going back to what it was.** |

### Adding a test

Add a row to `tests/suite.py`. `soak` is a real answer and costs nobody any
budget; the `registry` row will fail the build until you do, which is the
point. Say in `why=` what breaks if the check is not there - a check that
fails months from now is read by somebody who does not know what it was
defending.

## The matrix

The **MartyPC** column is "is this the right tool for it", not "does the
emulator have the hardware": a ✅ means reach for it first.

| Capability | MartyPC | QEMU | How (QEMU) | Verified result |
|---|---|---|---|---|
| VGA 640x480x16 (mode 12h) | ✅ | ✅ | `make test`, or the `os8088_xt_vga` machine | boots to Locator; loads packages. MartyPC has a register-level VGA and rasterises 12h — `vid_w=640 vid_h=480 vid_planes=4`, raster 800x524, and Minesweeper renders in 8 distinct palette colours |
| CGA 640x200 mono | ✅ | ✅ | `make test VIDEO=cga` | renders; dumps 640x400 (line-doubled) |
| Hercules 720x348 mono | ✅ | ✅ | `make test VIDEO=herc HERCSEG=0x7000` | renders; 55.8% lit at the desktop |
| The DOS end of the parallel link (SPEC.md §62) | ✅ | ➖ | `make dosstub`, then MartyPC `os8088_5150_cga_lpt` | **There is no DOS here and none this tree may ship**, so `OS88NET.COM` shipped to the field TWICE without one instruction of it executing - the second time it came back "returns to prompt instantly with nothing printed", which is a `.COM` whose entry is not at offset 0x100. `tests/dosstub` is a bootable floppy carrying an int 21h stub and the .COM inside its own image, on a real 4.77MHz 8088 with a real parallel port at 0x378: the banner, the default name, `/RO`, `/P:`, a filename, the cannot-open refusal, the sector arithmetic at all three of its outcomes and the latch probe are all verified there. **AND IT CAN BE GIVEN A PARTNER**, which this cell denied for two milestones: stock MartyPC stores what is written to the LPT status register, so `tests/lptlink/partner.py` in its MASTER role drives the real program's whole command loop - every file verb answered by `OS88NET.COM`'s own instructions (SPEC.md §62.10.3/§62.10.6). Two limits are real. The stub has **one file handle**, so `NF_COPY` - which opens a source and creates a destination at once - reads and writes the same row and lands a 0-byte file; the verb's *frame* is exercised and its *body* is not, and the byte-exact claim belongs to the os8088-side harness where `partner.py` holds real blobs. And the WIRE's verdict - timings, levels, a real cable - is still the 5150's question |
| Adapter switching (SPEC.md §39.11) | ⚠️ | ➖ | the `os8088_xt_vga` / `_5150_both` / `_5150_herc` / `_5150_cga` machines | MartyPC is the instrument, and one direction is out of its reach. Verified: the page lists **Vga + Cga** on `_xt_vga`, **Hercules + Cga** on `_5150_both`, and exactly **one** row on the two single-card machines; the live switch works **both ways on `_xt_vga`** and **CGA → Hercules on `_5150_both`**; the choice survives a reboot through `SYSTEM.CFG`'s `VM` key; and a disk asking for a card the machine lacks is **refused**, staying on the probe's answer. NOT verifiable here: **Hercules → CGA on a dual-card machine**, because MartyPC's MDA decodes the whole 64KB at B0000–BFFFF whatever 3BFh's page bit says — measured, B0000 and B8000 read back byte-identical — so the two cards contend for B8000 and the CGA's rendered output stays black although the card is correctly in `Mode6HiResGraphics`. A real HGC with that bit clear (which `vid_setmode` leaves clear) decodes B0000–B7FFF only. **That direction wants a run on the 5150.** The same aliasing is what `vid_cga_alias` (§39.11.1) exists to reject, and it is why the Hercules-only machine stopped reporting a CGA that is not there. **Hiding the page** (§31.10.1) verified on both single-card 5150s — the list is Scheduler/Buffer/Date-Time/Drivers/Sound and no Display row — with the static/driver boundary checked on `os8088_xt_hdd`, where the hard-disk driver's own page lands at row 5 (the slot the hidden page vacated) and dispatches to the driver, not to Display. **Blanking the outgoing card** (§39.11.4) verified on `_5150_both` in both directions: CGA → Hercules takes the CGA card from `Mode6HiResGraphics` to `Mode1TextCo40` (3D8h video bit clear), and Hercules → CGA trips an I/O breakpoint on 3B8h — a port `vid_setmode`'s CGA path never touches, so that write is `vid_blank` and nothing else |
| Two displays at once (SPEC.md §39.12–§39.19) | ✅ | ❌ | `tests/dualcheck.py` on the `os8088_5150_both` machine, or `make xt-multimon` | MartyPC is the instrument, because only it can read the guest's own answer back; 86Box is where the pair is two 6845s on a period bus. Verified on `xt-multimon` (an `ibmxt86`, 640KB, `gfxcard = cga` + `gfxcard_2 = hercules_plus`): the machine boots to the desktop on the **CGA**, the probe finds the mono card, and forcing `[vid_dmode]` to Extend grows the desktop onto "86Box Monitor #2" - §39.13's bring-up of a card the ROM never touched, on a second card that works. **`gfxcard_2 = hercules` WAS not found, and it was the KERNEL** — see §39.11.1.1. 86Box's plain Hercules answers only 4KB at B0000 while it is still in POST's text mode, so `vid_memchk`'s 0xAA at B000:1000 landed on its 0x55 at B000:0000 and the card was rejected as the text-only MDA that signature means. **This was filed as "the kernel is right and the model differs", and that was wrong**: a real Hercules GB101 in a real 5150 behaves identically (docs/FIELD-MACHINES.md, 5150 #2), because a Hercules whose configuration switch has never been written *is* an MDA. `vid_probe_avail` writes 3BFh and asks again now, and `[vid_hprobe]` in `sysbench`'s video block says which answer it took. The symptom to recognise is a Control Panel with **no Display page at all** (§31.10.1 hides it when `[vid_avail]` has one bit), which announces nothing about why. NOT verifiable here: **the extend as the user reaches it**. 86Box has no automation socket, the Desktop row is a mouse click (`tests/dispcp.py`'s coordinates, driven by hand), and macOS will not let a script synthesise one - so `[vid_dmode]` forced in a scratch build is how the machine was checked, and `dualcheck.py` is what says the geometry is right |
| PC speaker | ✅ | ✅ | `make test-snd`, or `MARTYPC_WAV=` | dominant 880.0 Hz (891.0 on MartyPC, inside tolerance) |
| AdLib / OPL2 | ✅ | ✅ | `make test-snd ADLIB=1`, or the `os8088_5150_sb` machine | dominant 880.0 Hz from a keyed 440; the Sound page's Test tone came out of MartyPC's OPL2 at 660 Hz |
| Sound Blaster (DMA streams) | ✅ | ✅ | `make test-snd SB16=1 TESTAPPS=build/sbtest.img`, or the `os8088_5150_sb` machine | 2.00 s at 1000.0 Hz on BOTH. MartyPC's is a DSP **2.01** by default — the classic `0x48`+`0x1C` auto-init path — where QEMU's is an SB16; `dsp_version` picks |
| Boot sound probe (SPEC.md §51.3.1) | ⚠️ | ✅ | the `os8088_5150_sb` / `_sbonly` / `_cga` machines, fresh image | MartyPC is the instrument here: its AdLib **answers the OPL2 timer-flag dance** on a cycle-accurate 8088, which is the whole of what the probe reads and what QEMU cannot show — QEMU's `-device sb16` has an OPL *stub* that does not answer, so on QEMU an SB16-only box reads as cardless. `_sb` → row 0 `WANT` 1, `SEG` 9E80, and the Sound page comes up on **Sound Blaster** with nothing ticked; `_cga` → `WANT` 0, nothing loaded, no `DRVE_HW`; `_sbonly` → `WANT` 0 by default and 1 under `make SNDSNIFF=sb`, against a real DSP 2.01 |
| Scripted mouse / keys | ✅ | ✅ | **`tools/os88mouse.py click X Y`** (absolute, closes the loop — see the MartyPC section; `os88marty.py mouse` is the relative primitive under it), `os88marty.py key`, or `tools/mouse.py` on QEMU | MartyPC drives the REAL devices: a Microsoft packet through the UART (`mou_seen` goes 0→1) and a keystroke through int 09h (SPEC.md §9.6's arrows moved `mouse_x` 320→350) |
| **Screenshots** (CGA/Herc) | ✅ | ✅ | `os88marty.py shot`, or `tools/shot.py` / `hercshot.py` | MartyPC reads VRAM directly — 60.0% lit, matching QEMU's CGA on the same desktop |
| Mouse on COM2 (SPEC.md §9.5) | ➖ | ✅ | `make test MOUSEPORT=com2` | both UARTs probe present, COM2 wins, COM1 retired |
| A **cross-wired IRQ** (SPEC.md §9.5.2) | ➖ | ✅ | `make test MOUSEPORT=com2irq4` | the Compaq Portable III: mouse at 2F8 driving IRQ4. Undetectable before the fix |
| A **modem** on the other port | ➖ | ✅ | a socket chardev at 3F8 — see below | eight result codes claim nothing, move nothing, click nothing |
| **A PS/2 mouse** (SPEC.md §9.9) | ➖ | ✅ | `make test MOUSEPORT=ps2` | the `pc` machine's 8042 has an auxiliary port whether or not anything is plugged into it, so both halves are testable here and neither is on MartyPC or a 5150 — an XT has no 8042 at all, and `[cpu_tier]` refuses the whole module there. See below |
| Performance benchmarks | ✅ | ✅ | `make bench` (from `tests/`, not in `all`) | numbers are always in flux — see below |
| **Flicker** — the double-draw flash | ✅ | ❌ | `os88marty.py flicker` (PERFORMANCE.md Part 3.1) | one sample per displayed frame. A Disk window repaint flashes 1,963 px for 166 ms; an idle desktop and a pointer move measure zero. CGA, Hercules and VGA — the MDA's rasterisation of Hercules graphics was measured working at the pinned build (docs/MARTYPC-DEBUG.md); this row used to exclude it |
| Fullscreen exclusive (SPEC.md §53) | ➖ | ✅ | `make test TESTAPPS=build/fsxtest.img` | every FSXM mode the adapter owns sets, draws and restores — the desktop screendump below the bar is byte-identical after a full sweep; Mode X dumps 640x480 (line-doubled 320x240) |
| ...**which MONITOR it lands on** (SPEC.md §53.7.1) | ✅ | ❌ | `python3 tests/dispfsx.py [--app paint] [--far] [--noxt]` on `os8088_xt_vga_herc` | needs two cards and the guest's own answer back, so it is MartyPC's alone. **Two assertions, and they are different questions**: while the bracket is UP, its own display must change a lot and the other must not change at all; AFTER the round trip, both cards must be pixel-identical to a forced full repaint (and so must their raster dimensions and the `vid_ctx` records — a card left in the wrong mode is a state a repaint cannot fix, so that comparison would read 0 either way). The second alone passes on a broken kernel: §53.6's exit `wm_paint_all` repaints the world, so a bracket that drew its whole face onto the *wrong* monitor for its entire session leaves no trace afterwards. A one-card machine (`--machine os8088_5150_cga_gla`) is the regression leg — `fsx_surf` answers `(0,0,w,h)` there, which is what both apps hard-coded before it existed |
| Does an INCREMENTAL redraw agree with a full repaint? | ✅ | ❌ | `python3 tests/dispcorner.py [--only a\|b] [--under hello] [--dest seam\|near\|far] [--mode right\|below] [--single]` on `os8088_xt_vga_herc` | do the thing, capture, force a repaint (poke `[cp_dirty]` with `WF_SAVEU` cleared, then read the flag back — see "Prefer a self-checking harness" below for why it is neither of the two more obvious ways), diff. **A** sweeps `hello` through launch / raise / drag / drag-back / close-the-Disk-window and watches `(W_X, W_Y+W_H)`, the drop shadow's bottom-left corner, which nothing writes (`wm_draw_shadow`'s L starts at x+1) and which therefore has to be desktop dither on both paths. **B** drags a window off two others and across the seam. **C is the one that found a real defect** (SPEC.md §11.96.13.1): it drags a Disk window ±40 and ±41 rows and labels each leg by the parity **the record actually took**, because `wm_dock_snap` and `ui_drag`'s clamp both move a dropped window — a requested +180 came out +175 in the run that found it. Its control is `make DRAGCACHE=0 && … --define NODRAGCACHE`. That defect's fix was **withdrawn** and the residue is accepted, so C no longer asks whether the two captures agree — it asks **which kind of difference** it is. `dither_split` excuses only pixels it can prove are a screen-phased dither replayed a row off (a filled rect, a 2-periodic checkerboard in both captures, in the same two colours, one the other shifted a row); anything else still fails, an **even** `dy` may produce neither class, and the control build may produce neither on any leg. **That is a classifier and not a tolerance, which is the only reason it is allowed to exist** — subtracting a count would pass a kernel that had lost the scroll-bar track altogether, and `--selftest` proves it does not, on synthetic captures, with no emulator and no build. A and C need no seam and run on a one-card machine, which is where the two **mono** adapters get checked — `CDGRAY` really is a checkerboard there. It prints WHERE and crops a PNG of both captures, because a count says a defect exists and only a picture says which of the things in that rect it is. Two rules it exists to enforce, both learned the expensive way: the subject is chosen **by name out of the guest**, never by a row number, and it must be **inert** — see "Prefer a self-checking harness" below |
| **What the guest WROTE to a floppy** | ✅ | ⚠️ | `tools/os88flush.py diff 0` (docs/MARTYPC-DEBUG.md); on QEMU the mounted `.img` is written in place, so `os88disk.py --verify` it after `quit` | the only route to os8088's write path that is not os8088's read path. A Control Panel close adds `SYSTEM.CFG` and moves sectors 1, 3, 5, 268 — both FATs, the root, the data cluster: SPEC.md §18.4's commit order, seen from outside. QEMU's ⚠️ is that its writeback is all-or-nothing at exit, so there is no *mid-session* snapshot and no per-drive control |
| Boot-sector relocation (SPEC.md §2.7) | ✅ | ✅ | `make test RAMKB=<n>` — see below | 105 boots, 104 prints `RAM` and never loads a byte |
| A machine that reports a **small** `int 12h` to the KERNEL | ✅ | ❌ | MartyPC `conventional.size`, or 86Box `mem_size` | `RAMKB=` moves the sector only; the heap still sees the real answer. MartyPC's real BIOS counts what the config says it has |
| Video **detection probe** | ✅ | ❌ | `make marty`, or `make xt-cga` / `xt-hercules` | MartyPC has a modelled CGA and MDA/Hercules, so the probe genuinely runs — this stopped being 86Box-only |
| 6845 programming | ✅ | ❌ | `make marty`, or `make xt-hercules` | modelled by both; MartyPC's `screen` reads the result back without a screenshot |
| Period-correct **CPU** timing | ✅ | ❌ | `make marty` (8088 only), or `make xt` / `286` / `386` | MartyPC is cycle-accurate and agrees with the 5150 on 45 of 47 gfxbench rows. **Not the disk** — see the rule above. 286/386 are 86Box only |

`VIDEO=` forces a code path; it does not exercise the probe that would have
chosen it. That distinction is the whole of QEMU's ❌ column for video: QEMU
emulates no CGA and no Hercules card, so what is untestable *there* is the
*choosing*, not the *drawing* — and the drawing is almost all of the code.
**MartyPC has both cards**, so the probe is no longer 86Box-only; what is
still 86Box's alone is a machine that is not an 8088.

---

## How much RAM the machine says it has

`boot/boot.asm` relocates itself to the top of conventional memory (SPEC.md
§2.7), which it finds with `int 12h`. **SeaBIOS answers 639 whatever `-m`
says** — conventional memory is capped there and the rest is above 1MB — so
neither the arithmetic nor the refusal below the floor can be reached here by
configuring QEMU. `RAMKB=<n>` assembles the sector to believe a number:

```sh
make test RAMKB=128         # where a 128KB machine (MIN_RAM_KB) puts it
make test RAMKB=104         # below the floor: must refuse
python3 tools/qmp.py build/qmp.sock 'xp /4xb 0x600'   # 00 00 00 00 = never loaded
```

Verifying it landed where it should is a memory dump, not a screenshot: the
sector's last two bytes are its `0xAA55` signature, so on a machine of *n* KB
they are at linear `n*1024 - 2`, and its first three are `EB 3C 90`.

```sh
python3 tools/qmp.py build/qmp.sock 'xp /4xb 0x9fbfc'   # 639KB: .. 55 aa
python3 tools/qmp.py build/qmp.sock 'xp /8xb 0x9fa00'   # eb 3c 90 'MSDOS'
```

Three things to know before trusting a run of this:

- **It shares the `VIDEO=`/`RTC=` stamp**, and needs to: the knob touches
  neither `boot.asm` nor `kernel.bin`, so without the stamp `make` rebuilds
  nothing and the machine boots the PREVIOUS relocation while you read the
  new one. That failure was seen once, and it reads as the address arithmetic
  being wrong.
- **It moves the sector and nothing else.** The kernel still asks the real
  `int 12h` for the top of its heap, so this is not a small-machine
  simulation — the rows in docs/KERNEL-MEMORY.md's RAM table below the boot
  floor are still simulated by clamping the heap.
- **The boundary is arithmetic, so test it at the boundary.** The sector
  refuses when its computed base is below where the kernel's read plus its
  own 2,048-byte stack would end, which for a 71,112-byte kernel is 105KB.
  Both sides of that were measured; the number moves whenever the kernel's
  size does.

---

## Video

CGA works because SeaVGABIOS's `int 10h AX=0006h` is a byte-exact CGA
framebuffer, so an ordinary `screendump` shows it. Note the dump comes back
**640x400** — QEMU line-doubles 640x200 — so a crop's Y and H are twice the
kernel's own. VGA is 1:1.

```sh
make test VIDEO=cga
python3 tools/shot.py build/qmp.sock /tmp/cga.png
python3 tools/mouse.py --screen 640x200 build/qmp.sock click X Y
```

Hercules needs its framebuffer relocated into spare RAM (B0000 is unmapped
under QEMU and silently swallows every write), and it is **never**
screendumpable — that framebuffer is guest RAM the VGA device has never heard
of, so `screendump` returns a black or stale VGA screen and does not error.
That silent non-failure is how "Hercules doesn't work" gets concluded from
one screenshot.

```sh
make test VIDEO=herc HERCSEG=0x7000
python3 tools/hercshot.py build/qmp.sock 0x70000 /tmp/herc.png   # LINEAR = HERCSEG*16
python3 tools/mouse.py --screen 720x348 build/qmp.sock click X Y
```

`HERCSEG` is a segment and `hercshot` takes the linear address; the missing
zero is the commonest mistake. Full recipe and the four ways to get it
silently wrong: `docs/HERCULES-TESTING.md`.

**`VIDEO=`/`RTC=` are tracked by a stamp file**, so a knob-built kernel is a
*different* kernel and changing the knob rebuilds it. Nothing in `build/` is
committed, so a forced kernel can no longer reach the repo — but it does stay
on your disk images until something rebuilds them, and a release must be built
knob-free:

```sh
rm -f build/os8088.img build/os8088-720.img build/os8088-360.img && make
```

**That stamp was BROKEN for every knob in the tree and is fixed — if you are
reading an A/B taken before this, re-take it.** The stamp's job is to delete
the artifacts a knob can change so they rebuild, and its list named
`build/kernel.bin` — correct while that *was* the nasm output. The on-demand
module split (SPEC.md §2.8) put **`build/kernel-full.bin`** in front of it,
which is the file `$(VIDDEF)` is actually passed to, and left `kernel.bin` a
DERIVED file that `os88mod.py` carves out of it. So switching a knob deleted
the derived file, re-ran the carve over a `kernel-full.bin` nothing had
rebuilt, and produced **the previous knob's kernel** with a fresh timestamp
and no complaint. `make VIDEO=cga` after a plain `make` re-emitted the VGA
kernel — the exact failure the paragraph above warns about, with the guard
against it defeated by an unrelated later change. The two **carved modules**
go on the list with it (`ctrl.drv`, `format.drv`): they are cut from the same
assembly and are exactly as stale, so a knob build was shipping a `KERNEL.SYS`
*and* two `.drv` files from the previous knob.

**It was found twice, independently, from two symptoms that neither of them
points at the stamp** — an incremental plain rebuild after `make SNAPAUDIT=1`
differing from a clean one by 39,504 bytes, and `make FDDABSENT=1` producing a
kernel with none of the knob's bytes in it while the knob's own A/B "passed"
by comparing a build against itself. If a knob has ever surprised you by
changing nothing, that was probably this.

Two things made it invisible, and both are worth knowing as shapes.
`kernsize.py` **re-assembles the kernel itself** with `$(VIDDEF)`, so it
faithfully reports the sizes of a binary the build did not produce — a 65-byte
knob was reported for a kernel that did not contain it. And a knob-only change
touches no source, so nothing else in the dependency graph moves. The check
that does not lie is to look for the knob's own bytes in the artifact:

```sh
rm -f build/.video-*        # the belt-and-braces version of the fix
make FDDABSENT=1 && python3 -c "print(open('build/kernel.bin','rb').read().find(b'\xc6\x44\x01\x21'))"
```

`os88sym.py` is the other guard that works, because it asserts byte-identity
between its own assembly and `build/kernel.bin` and **refuses** rather than
answering — pass the knob with `OS88_DEFINES=FDD_FORCE_ABSENT` (comma or space
separated) when driving a knob-built kernel.

---

## The mouse's port, and the modem on the other one (SPEC.md §9.5)

`make test MOUSEPORT=com2` gives QEMU a **live but silent** UART at 3F8 and
the mouse at 2F8. That shape is the point: leaving 3F8 unpopulated would make
the probe find one port and the kernel take the single-port path — testing
the easy half and none of the contest. Read the answer out of the kernel rather than off the glass; the
offsets move whenever an include before `mouse.inc` does, so re-derive them
from a listing (`nasm … -l`) and peek at `KERNEL_SEG*16 + offset`:

```
mou_bases  0x03f8 0x02f8     both probed present
mou_need   8                 two live ports, so a contest
mou_port   2   seen 1        the mouse is on COM2
mou_hpst   2                 mou_lockon has retired COM1
```

### `comscan` — when the mouse is not found on real hardware

`make comscan` builds the field diagnostic for exactly that (`tests/comscan`).
It is **not** an os8088 package, deliberately: the thing being diagnosed is the
mouse, so anything you have to click is unreachable on the one machine that
needs it. Two builds from one source —

- **`build/comscan.img`** (360KB) / **`build/comscan144.img`** (1.44MB), a
  *bootable* floppy. The shipped boot sector will load anything at
  `KERNEL_SEG:0000` that honours its three-point handoff (a `retf` at 0x0008,
  a spare word at 0x000C, entry at 0x0000), so this needs no DOS, no os8088
  and no mouse.
- **`build/comscan.com`**, a DOS program on the same disk. Its output goes
  through int 21h rather than the BIOS, so `COMSCAN > COMSCAN.TXT` captures
  the whole report to a file that can be carried off the machine.

It surveys 3F8/2F8/3E8/2E8 — including the two os8088 never probes, because a
live UART at 3E8 is the entire bug on its own and it says so in as many words.
Per port: the BIOS POST's own list, os8088's divisor-latch probe *and the same
probe with a long settle* (they disagree only if the kernel's is too quick for
that machine's bus), the scratch register, an internal loopback, the part type,
a raw register dump, then a 1200 7N1 DTR/RTS pulse and a polled capture with
the Microsoft packet machine run over it — bytes, the identify byte, packets,
violations, and the longest clean run against `MOU_LOCKN`.

**The measurement that matters, and the one nothing here could have made, is
which IRQ line the card actually drives.** os8088 derives it from the base
(3F8 → IRQ4, 2F8 → IRQ3); a card jumpered elsewhere gets probed, programmed
and hooked and then simply never interrupts. Two ways to ask it were built and
only the second works:

- **Reading the 8259's IRR with the lines masked** needs no handler and cannot
  storm, and it is wrong. A masked request is never acknowledged and an 8259
  clears IRR on acknowledgement, so a line asserted once before you arrived
  reads as 1 forever. Measured on QEMU, the base/hot/cold readings around a
  byte were `18h`, `18h`, `18h` — both serial lines stuck up, the card's own
  contribution invisible, every port reporting no interrupt at all.
- **Hooking int 0Bh/0Ch/0Dh/0Fh and arming RX on one port at a time**, which is
  what the kernel itself does. A runaway guard masks any line that will not go
  quiet after `IRQGUARD` interrupts, so a shared line cannot hang the machine.
  One flush pass runs first against *no* port, because unmasking delivers
  everything latched at boot in one go and otherwise whichever port is probed
  first collects it — COM1 reported IRQ3 *and* IRQ4 without a byte ever
  arriving on it.

Verified under QEMU both ways round: mouse on COM2 → `IRQ3`, COM1 silent;
mouse on COM1 → `IRQ4`, COM2 silent. **And it earned its keep on the first
real run**: the Compaq Portable III's mouse turned out to be at 0x2F8 driving
**IRQ4**, which is SPEC.md §9.5.2 and was invisible to every other test in
this tree.

**QEMU can reproduce a cross-wired card**, which turns a field-only bug into
a regression test — and all four combinations are `MOUSEPORT=` knobs:

| | mouse | and the other port |
|---|---|---|
| `make test` | 3F8, IRQ4 | nothing |
| `make test MOUSEPORT=com2` | 2F8, IRQ3 | live but silent at 3F8 |
| `make test MOUSEPORT=com2irq4` | **2F8, IRQ4 — the Compaq Portable III** | live but silent at 3F8 |
| `make test MOUSEPORT=com1irq3` | 3F8, IRQ3 | live but silent at 2F8 |

`-serial` cannot set an IRQ, so those go through `-device isa-serial`, which
takes `iobase=` and `irq=`. On a kernel without §9.5.2 the two cross-wired
rows never find the mouse at all — `[mou_seen]` stays 0 through forty
`mouse_move`s and the cursor never leaves its start.

One trap in writing that test, and it produced two false failures: a movement
pattern that **nets to zero** returns the cursor to where it started, so
"the cursor changed" reports a perfectly working mouse as broken. Drift in one
direction.

### The PS/2 mouse (SPEC.md §9.9), and why QEMU is the only instrument

`make test MOUSEPORT=ps2` gives the guest **no serial ports at all**
(`-serial none`, and no `msmouse` chardev), so both UART rows are rejected by
SPEC.md §9.5's probe, nothing can win the serial contest, and the only
pointing device on the machine is the PS/2 mouse the `pc` machine has anyway.
That is the positive test for the 8042 handshake, and there is nowhere else to
run it: MartyPC and every 5150 in docs/FIELD-MACHINES.md is an 8088 whose
keyboard is an 8255 PPI, so `[cpu_tier]` refuses the module before it reads a
port. **86Box's AT-class machines are where it should next be tried on
something closer to iron**, and it has not been.

What to assert, all of it readable with `tools/os88sym.py` and `xp`:

| | |
|---|---|
| `mou_bases` | `0000 0000` — the serial probe rejected both rows, so the contest is not merely lost, it cannot be entered |
| `mou_p2st` | **9**. This is the row to read first on any failure: it is how far the handshake got (SPEC.md §9.4.4), so `2` means "no auxiliary port" and `7` means "the enable timed out" and they are different bugs |
| `mou_p2` / `mou_p2id` | `1` and `00` — live, and a plain 3-byte mouse |
| after one `tools/mouse.py to X Y` | `mou_seen` 1, `mou_port` **04** (`MOU_P2ROW`), `mou_line` **FF** (`MOU_P2LINE`), and `mouse_x`/`mouse_y` **exactly** the requested X/Y |

That last row is the one that catches the defect a PS/2 driver actually has.
`tools/mouse.py` pins against the kernel's own edge clamp and then walks back
by exact deltas, so **landing on the requested pixel is a statement about the
sign handling and the Y inversion** — SPEC.md §9.9.3's "positive is up" —
which nothing else here would notice. A mouse with the Y sense inverted moves,
draws, clicks and drags perfectly and simply goes the wrong way.

Two more that are not about the mouse at all and are the reason the handshake
is written the way it is. **The keyboard must survive all of it**: the 8042
has one output buffer for two devices, and both `mou_p2_init`'s reads and the
ISR's are a chance to take a keystroke away from int 09h. Send a burst and
watch the BIOS keyboard buffer's head and tail at `0040:001A`:

```
python3 tools/qmp.py build/qmp.sock 'xp/2xh 0x41a'
python3 tools/qmp.py build/qmp.sock 'sendkey a' 'sendkey b' 'sendkey c'
python3 tools/qmp.py build/qmp.sock 'xp/2xh 0x41a'      # +2 bytes per key
```

Six keys advanced it `001E → 002A` with the PS/2 mouse live, and nine did
`002A → 003C` while `tools/mouse.py move` ran against it — no byte lost in
either direction. And the **serial** configurations must be unchanged: on the
default `make test`, `mou_port` still settles on `00` with `mou_line` `10`,
because QEMU keeps routing input to whichever handler was activated last and
`msmouse` is still that one. The PS/2 mouse is probed, found, and then retired
by `mou_lockon` — `mou_p2` goes back to `0` with `mou_p2st` left at `9`,
which is the pair that says "the vector is still ours to give back".

**What found the ordering bug.** The first version of this armed IRQ12 in the
8042's command byte before installing int 74h, and the machine's own BIOS
handler ate the `0xF4` acknowledgement: every earlier step passed and
`mou_p2st` sat at `7`. Nothing about that reads as a race — it reads as a
controller that stopped answering — and the only reason it was found in
minutes rather than on hardware is that `mou_p2st` records the step.

### Can we boot a real 5150 BIOS instead of SeaBIOS?

Asked, checked, and **no** — but the instinct behind it is sound and worth
recording so the next person does not re-derive it.

SeaBIOS genuinely does misrepresent a period machine, and it has cost this
project real bugs: the **int 1Eh diskette parameter table** it never reads,
which is the whole of FIELD-NOTES 5 (a real ROM ships EOT = 8 and the
multi-track bit, and QEMU cannot show any of it); a real BIOS's **short int
13h reads**, which SeaBIOS never returns; and **interrupt stack usage**,
which SeaBIOS keeps on an internal stack where a real BIOS lands it on
whichever task stack is current — the reason `tests/stackprobe`'s QEMU answer
is not the answer.

`-bios` will not fix any of that, because it maps a *file* and does not change
the *hardware underneath it*. QEMU has no XT-class machine — `-machine help`
lists i440fx variants, q35, microvm and `isapc`, all 486-era or later — and
the concrete blocker is the configuration read at the very start of the 5150
POST: an IBM PC reads its DIP switches through the **8255 PPI at 60h–63h**,
and every QEMU machine puts an **8042 keyboard controller** at 60h/64h with a
port-61h NMI/speaker latch instead. There is no 8255 anywhere in
`qemu-system-i386 -device help`. The POST fails its first configuration read,
before video, and beeps.

**86Box is the answer to that question and the repo already has it
configured** — `make xt`, `xt-640`, `xt-cga`, `xt-hercules`, `286`, `386sx`,
`386dx`, each with the real ROM set. That is what the "What 86Box is genuinely
for" section below is about.

Two footnotes. `-machine isapc` *does* boot os8088 (ISA-only, no PCI) and is
marginally more period-shaped than the default, but the BIOS is still SeaBIOS
so it buys nothing on the list above; it is not worth changing the default
for. And for the bug that prompted the question — §9.5.2's cross-wired IRQ —
**neither would have helped**: that is a property of a card's jumper, not of
the BIOS, and `-device isa-serial,irq=` models it exactly.

One trap that cost a whole debugging round, and it is the kernel's own idiom
misapplied: the per-port state is walked with a **word** index (0, 2, 4, 6), so
a *byte* array needs `NPORT*2` entries and not `NPORT`. Declared four bytes for
four ports, every array silently overflowed into the next — `p_live[2]` was
`p_phase[0]`, a dead COM3 read as live, and the survey was nonsense.
`kernel/mouse.inc`'s two-port arrays are four bytes and correct, which is
exactly how the wrong shape got copied across.

**The case that actually matters is a talkative device on the other port**,
because a Hayes result code is a well-formed Microsoft packet (§9.5.1). QEMU
can be that device: put a socket chardev at 3F8 and type at it.

```sh
qemu-system-i386 -drive file=build/os8088.img,format=raw,if=floppy -boot a \
  -chardev socket,id=modem,host=127.0.0.1,port=45881,server=on,wait=off \
  -serial chardev:modem \        # 0x3F8 - the "modem"
  -serial null \                 # 0x2F8 - a live UART, saying nothing
  -display none -qmp unix:build/qmp.sock,server,nowait -daemonize
# then: connect to 45881 and send OK/RING/NO CARRIER/CONNECT, CRLF-wrapped,
# pacing each burst by len*10/1200 seconds - it is a 1200-baud line
```

A third trap is in the survey rather than the harness, and real hardware is
what exposed it. **A test that leaves the baud rate wherever the last test
left it is a test whose result depends on the machine.** `t_latch` scribbles
0x55 into DLL and leaves DLM as the BIOS set it, so the loopback that ran
next was clocked at a rate that differed *per port* by however much their
as-found divisors differed — and its bounded wait then timed out on the
slower one and reported a healthy UART as broken. On the Compaq Portable III
that read as COM1 failing loopback while COM2 passed, on two ports that are
both ordinary 8250s. `t_loop` programs divisor 1 (115200 baud, ~87us a byte)
before it starts, and the survey prints the **as-found divisor** in a `DIV`
column, which is what would have made it diagnosable from the first report.

Two traps in building the harness, both of which produced a green run that
proved nothing:

- **`msmouse` speaks during boot.** With a real mouse attached to 2F8 the
  contest is over before the first byte of chatter is sent, so the modem is
  being tested against a port that has already lost. To test the *open*
  contest there must be no mouse anywhere — 3F8 the socket, 2F8 `-serial
  null` — and then `[mou_seen]` must simply stay 0 forever.
- **Assert on more than the port.** The first fix stopped the modem
  *claiming* a port while it was still moving the cursor and latching a right
  button (`mouse_btn` = 2) — a modem opening context menus is the same bug
  wearing a different hat. Check `mouse_x`/`mouse_y` and `mouse_btn` too;
  they must be exactly where the machine booted.

Then run it the other way round — the socket at 3F8 *and* `msmouse` at 2F8 —
and check the mouse still reaches its run, `mou_lockon` still retires COM1,
and chatter afterwards is ignored. The one thing to expect and not to file as
a bug: on a two-port machine the first ~8 packets of the session are counted
and discarded, so `tools/mouse.py`'s first absolute position is wrong. **In
practice it is wrong by the whole move, not by a few pixels** — `mouse.py`
pins against the top-left clamp with a burst of large negative deltas and then
walks to the target, the contest eats the front of the burst, and the pin
still lands because it over-drives; what gets lost is the walk. So the first
`to X Y` after boot leaves the cursor **at 0,0**, which reads exactly like a
mouse that is not working at all, and a screendump cropped to the target shows
nothing rather than something near it. Call `mouse.py` a second time — the
whole session is exact from there. Costed the same way twice, on VGA and on a
CGA field disk.

### The identify burst (SPEC.md §9.4.1)

**QEMU can test the half that must refuse, and cannot test the half that must
accept.** `msmouse` is not a UART-level device — it ignores MCR/DTR entirely
and emits packets during boot regardless — so out of a plain `make test` you
get `[mou_seen]` = 1, `[mou_hpst]` = 2 and `[mou_ident]` = 0: the *old* code
path, exactly, which is the right regression baseline and no test of the new
one.

**MartyPC models the UART and the mouse's reply to a DTR/RTS raise**, which
makes it the cheapest place to test the accepting half — the first
confirmation of it came off a MartyPC memory dump (SPEC.md §9.4.1 carries the
figures). It gives **two live ports**, `03F8` and `02F8`, so it is the
*contest* case rather than the easy one, and `[mou_need]` going `01 08` is
the assertion that matters there. Dump memory at the desktop **with the mouse
deliberately untouched** — that is the state that used to be broken, and
`mouse_x`/`mouse_y` still sitting on `[vid_w]/2, [vid_h]/2` is how you know
nothing has moved yet. It boots the **360KB** pair.

The refusal half uses §9.5's socket-chardev harness, with the sender timed to
land inside `mouse_init`'s drain window — which closes about **1.2 s after
QEMU launch** (sweep a single `M` at 0.2/0.6/1.0/1.4 s to re-find it; the
absolute position drifts a little per boot). Offsets move whenever an include
before `mouse.inc` does, so re-derive them from a listing:

```
idn   idb0   ident  idany  need        verdict
01    'M'    01     01     01 .. 08    a mouse: identified, need lowered
02    'M'    01     01     01 .. 08    'M3' likewise
05    'M'    01     01     08 .. 08    identified, but past MOU_IDSTRICT
1f    'O'    00     00     08 .. 08    Hayes codes - rule 2
2e    'M'    00     00     08 .. 08    'M' + a banner - rule 3
08    'M'    00     00     08 .. 08    a trickle that never stops - rule 4
```

The last row is the one worth building deliberately, because it is the only
way to isolate rule 4: send `'M'` every ~150 ms across the whole window, so
whenever the window closes a byte arrived inside `MOU_IDQUIET` of it, while
the count stays at `MOU_IDMAX` and rule 3 still passes.

**On a machine with no debugger, `sysbench` prints all of it** — SPEC.md
§9.4.2 publishes the block at `0060:0006` unconditionally, so a `make field`
disk carries it and no knob is involved. Its section is a state dump, not a
measurement: the bases and the first identify byte are hex, everything else
decimal, and a mouse that identified reads `first byte 4D`, `identified 1`,
`poller stamp 0`. Under `make test` it reads `44` ident bytes, first byte
`004D` and `identified 0` — msmouse's packet stream happens to start with a
header byte of 0x4D, and **rule 3 refuses it for being 44 bytes long**, which
is the refusal working on live data rather than on a scripted payload.

**Assert on `[mou_hpt]` as well as the identify state**, and that is the
actual regression this exists to prevent: read it, wait four seconds, read it
again. Unchanged means the recovery cycle stood down; advancing by **58**
means the mouse is still being power-cycled every 3.19 s. Two traps, both of
which produced a green run that proved nothing. A test whose sender never
lands in the window reads exactly like a rule correctly refusing — check
`[mou_idn]` is non-zero before believing a refusal. And `[mou_seen]` must stay
**0** in every one of these: an identify moves the prior and must never settle
the contest, so a run where it went to 1 is testing the packet path, not this
one.

---

## Sound

`make test-snd` is `make test` plus a wav capture at `build/snd.wav`,
finalized when QMP `quit` stops QEMU — so **run `tools/sndcheck.py` only
after `quit`**, or you measure a partial file. The capture is stream-on time,
not wall time: a silent boot yields an empty file, which is a pass for
`--expect-silence` rather than a broken harness.

Without `ADLIB=1`/`SB16=1` there is no card, the tone route falls to the PC
speaker, and that is what gets captured:

```sh
make test-snd TESTAPPS=build/fmtest.img
# launch FMTEST, then:
python3 tools/qmp.py build/qmp.sock 'sendkey b' 'sleep 2' 'quit'
python3 tools/sndcheck.py build/snd.wav 880          # -> dominant 880.0 Hz
```

The two gate packages are the mechanical checks. Neither ever ships on the
apps disks — each rides its own scratch image.

```sh
# AdLib: click once. The patch sets carrier MULT=2, so a keyed 440 must SOUND
# at 880 - that doubling is the assertion, and it only holds if the caller's
# patch bytes reached the operator registers.
make test-snd ADLIB=1 TESTAPPS=build/fmtest.img
python3 tools/sndcheck.py build/snd.wav 880          # -> dominant 880.0 Hz

# Sound Blaster: click once for a synthesised 1 kHz square, staged in 20
# chunks and played for 2 s.
make test-snd SB16=1 TESTAPPS=build/sbtest.img
python3 tools/sndcheck.py build/snd.wav 1000         # -> 2.00 s at 1000.0 Hz
```

The window says which half failed: FMTEST shows `K` (both verbs fine), `P`
(patch refused) or `N` (note-on refused), and a bare `N` means the frequency
never reached the driver. SBTEST shows `g:` grant and `o:` open.

`make test ADLIB=1` (without `-snd`) is the same card with no capture — the
right thing when you want to watch the driver attach rather than measure a
tone. **With no card the probe correctly finds nothing**, which is the right
answer and not the one you are trying to test; `sound.drv` ships on the boot
disk and `drv_boot` loads it before the first paint, so a driver that failed
to attach announces itself by opening the Control Panel on its Drivers page.

Depth, including the underrun and capture edge cases: `docs/SOUND-PLAN.md`
Phase 4.

---

## Hard disks

### Booting FROM one (SPEC.md §52.10)

The boot chain — MBR, volume boot record, kernel — is testable **without the
installer, and deliberately so**: if a disk the fixture builds boots and one
the installer builds does not, the fault is in the installer, and without a
fixture there is no way to make that distinction.

`tools/os88hdd.py` writes what SPEC.md §52.10.4 says the installer writes, in
the same order: `boot/mbr.asm`'s 446 bytes and one active partition entry, a
FAT16 volume, `boot/boothd.asm` with that volume's BPB over its first 62 bytes
and the kernel's sector count patched into the word at offset 508, and
`KERNEL.SYS` first and contiguous from cluster 2 — which the VBR *requires*,
because it reads the kernel as a flat run rather than walking a cluster chain.

MartyPC's `os8088_xt_hdd` is the machine, and it is **rung 0** (an option ROM
answering int 13h), which is the rung the field machine uses:

```sh
python3 tools/os88hdd.py \
    --template build/martypc/run/media/hdds/default_xtide.vhd \
    --out /tmp/boot.vhd --kernel build/kernel.bin \
    --vbr build/boothd.bin --mbr build/mbr.bin
cd build/martypc/run && MARTYPC_DEBUG_ADDR=127.0.0.1:9001 ./martypc_headless \
    --machine-config-name os8088_xt_hdd --mount hd:0:/tmp/boot.vhd &
python3 tools/os88marty.py 127.0.0.1:9001 run          # it starts PAUSED
python3 tools/os88marty.py 127.0.0.1:9001 verify       # the kernel is aboard
python3 tools/os88marty.py 127.0.0.1:9001 shot --rendered /tmp/hd.png
```

**NOTE THE `run`.** `martypc_headless` comes up with the CPU paused at the
reset vector, and every debug command answers perfectly well while it sits
there — so a `verify` that reports 88% differing and `boot_ticks: 0` is a
machine that was never started, not a boot that failed. That reads exactly
like a broken kernel and cost an hour once.

**NO FLOPPY IS MOUNTED**, which is the whole point: `--mount fd:0:` is absent,
so anything that reaches for A: has nothing to find. What the screenshot must
show is the desktop with a **Disk C** zone beside Disk A and Disk B. That zone
is the proof rather than a detail — `HDD.DRV` is not loaded (a fixture disk
carries no `SYSTEM.CFG`, so nothing asks for it), so the only thing that could
have created a third volume row is `dsk_boot_from` adopting the partition the
machine booted from (SPEC.md §52.10.3).

#### ...and what it can do ONCE THE VOLUME CARRIES FILES

`--file NAME=PATH` puts anything else in that volume's root, and it is what
takes the fixture from "does the boot chain work" to "does the OS work on a
machine installed to its disk". On an installed machine **every** file the
boot reaches is on the hard disk — `SYSTEM.CFG`, the drivers, and since
SPEC.md §2.8 the Control Panel itself — so a volume holding only `KERNEL.SYS`
boots to a desktop where the chip menu's Control Panel silently does not
open, and nothing on the machine can load a driver. That is not a bug and it
reads exactly like one.

The four that make the Hard Drive page reachable, which is the configuration
SPEC.md §52.10.3.1's duplicate-icon bug lives in:

```sh
python3 tools/os88hdd.py \
    --template build/martypc/run/media/hdds/default_xtide.vhd \
    --out /tmp/boot.vhd --kernel build/kernel.bin \
    --vbr build/boothd.bin --mbr build/mbr.bin \
    --file CTRL.DRV=build/ctrl.drv --file FORMAT.DRV=build/format.drv \
    --file HDD.DRV=build/hdd.drv --file HDDTOOL.DRV=build/hddtool.drv
```

Attributes follow SPEC.md §19.6 by extension — a `.DRV` lands read-only,
hidden and system, exactly as the installer writes it — because a fixture
that hides less than the installer does is a fixture for a volume nobody will
ever have.

**Then read `dsk_vtab`, do not count icons.** The volume table says which
volume is which transport, on which unit, at which base; a screenshot says
there are two hard-disk zones and leaves you guessing which one is the
duplicate. Tick the driver on the Drivers page, select **Hard Drive**, click
**Mount**, and the table must be unchanged — one hard-disk row, the
`DVK_BIOS` one the boot left, and the caption must read `Already mounted`.
Two zones for one partition is §52.10.3.1.

Two things this cannot tell you, both of which want the 5150: whether the
**timing** is tolerable (no emulator here is disk-accurate, and MartyPC's
error is 30× in the flattering direction — see the rule at the top of this
file), and whether a **real** option ROM's AH=08h reports the geometry its
AH=02h then translates with, which is the one assumption `boot/boothd.asm`
cannot make without hardware.

### The driver, its partitioner and its formatter

QEMU has an ATA disk at 1F0h and SeaBIOS gives it to int 13h as drive 80h, so
**both rungs of the driver's transport ladder (SPEC.md §52.1) are testable
here** — and so are the partitioner, the formatter, the mount and the desktop
zone, end to end:

```sh
make test HDD=40                 # a blank 40MB raw IDE disk, KEPT between runs
# System menu -> Control Panel -> Drivers -> tick Hard Drive
# -> the 'Hard Drive' page appears in the list; select it
# -> Format -> pick a slot -> Format   ... partitions AND formats it (SPEC.md
#                                          52.2); a slot with something in it
#                                          asks for the click again first
# -> Close -> Mount                    ... one icon per FAT partition: HDD C,
#                                          HDD D, ...
rm -f build/hdd.img              # start over from a blank disk
```

**Pair it with a host-side read**, exactly as the floppy write path is paired
with `os88disk.py --verify`: the in-kernel checks and a structural read catch
different bugs, and every bug found while building this was found by the host
half.

**`tools/os88disk.py --verify` is a FLOPPY fsck and will refuse a hard-disk
partition** - it checks `BPB_FATSz16 <= 10` and a real floppy geometry, both
of which a 31MB FAT16 volume legitimately breaks (SPEC.md 18.2 rule 10 drops
the cap for a driver-backed volume; 18.8 is why). Read the partition by hand
instead. The snippet below prints the BPB; the one after it is the test that
actually catches things - **compare every copied file against its source byte
for byte**, which is how a chunk-size bug that truncated a 116KB file to
64,512 bytes was found, with no error reported anywhere on screen.

```sh
python3 - <<'EOF'
d = open('build/hdd.img','rb').read()
e = d[446:462]                                   # partition entry 0
base = int.from_bytes(e[8:12],'little')
print('type', hex(e[4]), 'lba', base, 'secs', int.from_bytes(e[12:16],'little'))
b = d[base*512:base*512+512]                     # its boot sector
print('jmp', b[:3].hex(), 'spc', b[13], 'fatsz', int.from_bytes(b[22:24],'little'),
      'tot16', int.from_bytes(b[19:21],'little'), 'fstype', b[54:62])
EOF
```

And the one that earns its keep - a FAT16 reader that walks a partition and
compares every file it finds against the original on the host. `tools/` has
no hard-disk fsck, so this is it:

```sh
python3 - <<'EOF'
d = open('build/hdd.img','rb').read()
def vol(lba):
    b = d[lba*512:lba*512+512]
    return dict(spc=b[13], rsvd=int.from_bytes(b[14:16],'little'), nfat=b[16],
                root=int.from_bytes(b[17:19],'little'),
                fatsz=int.from_bytes(b[22:24],'little'), base=lba)
def rd(v,s,n=1): o=(v['base']+s)*512; return d[o:o+n*512]
def fat(v,c):
    off=c*2; b=rd(v, v['rsvd']+off//512); return int.from_bytes(b[off%512:off%512+2],'little')
def dirsec(v): return v['rsvd']+v['nfat']*v['fatsz']
def clus(v,c): return dirsec(v)+(v['root']*32+511)//512+(c-2)*v['spc']
def ents(v, first=None):
    raw = rd(v, dirsec(v), (v['root']*32+511)//512) if first is None else b''
    c = first
    while first is not None and 2 <= c < 0xFFF0:
        raw += rd(v, clus(v,c), v['spc']); c = fat(v,c)
    out=[]
    for i in range(0, len(raw), 32):
        e = raw[i:i+32]
        if not e[0]: break
        if e[0]==0xE5 or (e[11]&0x3F)==0x0F: continue
        out.append((e[:11].decode('latin1'), e[11],
                    int.from_bytes(e[26:28],'little'), int.from_bytes(e[28:32],'little')))
    return out
def content(v,c,size):
    o=b''
    while 2 <= c < 0xFFF0 and len(o) < size: o += rd(v, clus(v,c), v['spc']); c = fat(v,c)
    return o[:size]
v = vol(int.from_bytes(d[446+8:446+12],'little'))     # partition 0
for n,a,c,sz in ents(v):
    print(n, sz)
    if a & 0x10:
        for n2,_,c2,s2 in ents(v,c):
            if n2.startswith('.'): continue
            want = open('build/'+n2[:8].strip().lower()+'.o88','rb').read()
            print('   ', n2, s2, 'OK' if content(v,c2,s2)==want else '*** MISMATCH')
EOF
```

**Persistence: EVERYTHING needs the panel closed first.** Nothing on this page
writes `SYSTEM.CFG` from a click any more - not the geometry editor, and since
SPEC.md 51.9's verb 2 was retired not Mount and Unmount either. All of it is
staged into the kernel's settings struct on the spot and written at the panel's
teardown (SPEC.md 31.8), so a persistence run is: mount, type a geometry,
**click the close box on the LEFT of the title bar**, then quit QEMU, then
`make test HDD=40` again - and Disk A, Disk B and every hard-disk volume should
be on the desktop with no clicks at all. A run that quits with the panel still
open reboots with the probe's numbers back and nothing mounted, which reads
exactly like the blob not persisting and is the test being wrong. **Minimizing
is not closing**, and neither is a hard reset from outside; the System menu's Restart
is the other way that does flush.

Worth testing once as a pair, because it is the property the blob exists for:
untick the driver, close the panel, reboot (no driver, no icons), then tick it
again - the volumes come straight back, having round-tripped through a boot
where nothing could read them.

Both **change the image and the change persists** - `build/os8088.img` gains
`SYSTEM.CFG` - so `rm -f build/os8088.img build/os8088-720.img
build/os8088-360.img && make` when the next run's starting state matters,
exactly as for a floppy write test. Nothing under `build/` is committed, so
this costs a stale starting state and never a stale commit.

What QEMU cannot show: the **MFM** rung — rung 0 against a real XT controller's
ROM rather than SeaBIOS — and the 8-bit-bus behaviour that gates rung 1 off an
8088 in the first place. 86Box ships the XT ST-506 family (IBM/Xebec, DTC
5150X, WD1002A-WX1 and the Seagate ST-11M/R); confirm the exact `hdc =` key
with the launch-and-`kill -TERM`-and-read-back trick above, because 86Box
rewrites its config with what it actually accepted.

**And check the desktop on CGA**, always: `make test VIDEO=cga HDD=40`. A third
drive zone does not fit above the dock on a 200-line screen and wraps into a
second column to the left (SPEC.md §26.1) — which is invisible on VGA and
therefore exactly the kind of thing that ships broken.

## Host-side tools live in `tools/`, and a set of them goes in a FOLDER

`tests/` is guest code; `tools/` is the host side — the Python that drives an
emulator, builds an image or checks a document. Most of it is one file doing
one job and belongs at the top level, which is where `os88marty.py`,
`os88disk.py`, `mouse.py` and the rest are.

A few of them are **gates in their own right**, and are run the same way the
`tests/` packages are — `python3 tools/<x>.py [machine]` against a built tree:
`sucheck.py` (the raise cache, SPEC.md §11.96 — see "Prefer a self-checking
harness" below for the way it once passed without testing anything) and
`tools/notepad/pixcheck.py` (Note Pad's incremental redraw against a forced
full repaint). `tests/dualcheck.py` is the same species living on the other
side of the line, because what it drives is a machine rather than a program
(two adapters in one box, §39.11.1).

**`tests/wmartifact.py` is that same "is the glass what a repaint would draw"
question aimed at two OPEN defects**, and it is here rather than in a package's
folder because neither belongs to a package: one reproduces with `hello` and
the other by dragging a Disk window. docs/WM-ARTIFACTS.md is its report and
carries the measurements. Read that file's section 0 before concluding either
one does not reproduce — both are invisible in a screenshot (the glass is
stale, not corrupt), the first needs two windows clamped to the same shadow
row, and the second leaves its residue on the **secondary** display, so a run
that diffs the expected card alone comes back clean.

**A tool that grows into several files gets a directory, and the directory is
named for WHAT IT DRIVES**, not for what it does:

- **an application** → the app's name, as the user sees it in the dock and
  the Task Manager: `tools/notepad/`, and a Paint one would be `tools/paint/`.
- **a driver** → its Control Panel checkbox label (SPEC.md §31.9), because
  that is the name the machine itself puts on it and it is the one a bug
  report will use.
- **anything else** → the subsystem it belongs to, chosen the way a module
  prefix is: `tools/martypc/` is the emulator, and that is the pattern.

The point is that the folder answers "what is this for" before the reader
opens anything. A folder named for the technique — `tools/tracer/`,
`tools/bench/` — stops doing that the moment a second one exists.

Give the folder a `README.md` saying what the tool answers, what the OTHER
instrument for the same subject is and when to prefer it, and the ways it has
already lied. `tools/notepad/README.md` is the worked example: it exists
alongside `tests/npbench.inc`, and the two measure Note Pad from outside and
inside respectively, so the first thing its README does is say which question
each one answers.

### Name the FILE, never the row (`dispcp.open_named`)

**A scripted session must never write down which row a package is on**, and
this is a rule rather than a preference because it has now broken tests in
this tree twice, both times silently. The Disk window's listing is sorted by
name (SPEC.md §19.4), so the Makefile's build order never reaches the screen;
a subdirectory synthesizes a `..` at slot 0 and the root does not (§19.5), so
the two are offset by one; and adding **one** package to `GAMES/` renumbers
every entry after it. A stale row index does not error — it double-clicks
whatever sorted into that slot, and what the run then reports is that the app
it meant to open "did not launch", several steps and one screenshot away from
the cause.

`CYCLONE.O88` did exactly that: it landed alphabetically between `ARKANOID`
and `MINES` and quietly moved five tests' targets. `tests/dispmine.py`'s
`MINES_ROW` became CYCLONE, `tests/dispmcfs.py`'s and `tests/dispmodex.py`'s
`MISSILE_ROW` became MINES, and three of the `APPS/` ones were already wrong
from an earlier addition.

So there is one entry point and it takes a name:

```python
dispcp.open_named(m, mo, S, settle, wx, wy, "MISSILE.O88")
```

It asks the guest which directory entry that is (`row_of`, over the mount
snapshot the kernel just built), **scrolls it into view** and then clicks. The
scroll is the half that made this usable everywhere: `APPS/` is twelve entries
and a 640x200 Disk window shows seven, so a name-resolving helper without it
still could not reach `TRACKER.O88` — which is why `tests/dispfsx.py` and
`tests/paintgif.py` each build a one-file disk of their own. Those disks are a
speed convenience now rather than a requirement.

**It needs no `fit`**, and that is deliberate: how many rows a window shows
depends on its height, the adapter and the view mode, all of which a harness
would then have to track. Instead it asks the window to scroll to the entry
and reads `[FS_SCRL]` **back** — at the top it lands exactly there, near the
end it clamps, and `entry - FS_SCRL` is the visible row either way. The
arithmetic is the kernel's, which is the only thing that knows. It scrolls
with the arrow keys rather than the bar's cells (a key is a key; the bar is
five nested layouts deep) and waits by polling `[FS_SCRL]` rather than
settling on the picture, which is the difference between 30 seconds and 7.

**It reads the WINDOW'S OWN CACHE, not the global mount snapshot**, and that
is the second thing this cost. SPEC.md §22.1's rule is that paints read the
window's cache and only *actions* re-sync the globals — and SPEC.md §18.9's
quiet mount deliberately leaves `disk_nfiles` at 0 with `[dsk_lstale]` raised,
which is an ordinary state after anything that moved the volume without
navigating. Mounting a RAM disk from the Control Panel is exactly that, and
`tests/rdmove.py` met it: the globals answered *this folder is empty* about a
window with two rows on screen. The cache is a byte-for-byte copy of
`disk_dir` in the window's `FS_VSEG` claim, so it is the same decode — the
question is only which copy answers *what is the user looking at*.

`open_row` survives for the one caller that genuinely means a position on the
glass — `tests/wmartifact.py`, which asks for *the last row this window can
reach* — and it now **prints the entry it clicked** and takes an optional
`expect="NAME"` that refuses rather than clicking the wrong file. So the next
one of these is visible in the log instead of turning up as a launch failure.
`open_named` passes `expect` through, which closes the one hole the scroll
leaves: the arrow keys only reach the window while it is **frontmost**, so a
caller that forgot to raise it would otherwise scroll nothing and double-click
whatever was already on that row.

## Everything not shipped lives in `tests/`

`tests/` holds every package that is not shipping software, and it is **not**
`apps/`. Nothing under it is built by `all`, no artifact of it is tracked,
and none of it reaches a shipped floppy — so a normal build and every image
the project ships are exactly what they were before it existed.

Two kinds live there, and the difference is what they assert.

**Gates** answer pass/fail against a capability, and are the mechanical
checks referenced throughout this document:

| Package | Asserts | Run it with |
|---|---|---|
| `fmtest` | the AdLib FM surface (SPEC.md §34.2/§51.4) | `make test-snd ADLIB=1 TESTAPPS=build/fmtest.img` |
| `sbtest` | the Sound Blaster streams (§34.5/§34.6) | `make test-snd SB16=1 TESTAPPS=build/sbtest.img` |
| `filetest` | the write path (§18.4) | `make test TESTAPPS=build/filetest.img` |
| `fsxtest` | fullscreen exclusive (§53): keys 0–8 cycle every mode with an identifying pattern, `x` runs a same-mode bracket, `t` keys a duration-0 tone for the §53.3 legs; the window shows the `fsx_caps` mask (01EF/000F/0011 by adapter) and the last result (`K`/`R`/`F`/`S`) | `make test TESTAPPS=build/fsxtest.img` (also under `VIDEO=cga` / `VIDEO=herc`; `make test-snd` + two instances for the sound legs) |
| `socktest` | **a TCP connection over the parallel cable** (§62.11): `NETV_OPEN`/`STATUS`/`SEND`/`RECV`/`CLOSE` from a package's WORKER, non-blocking throughout, fetching a page and checking the exact bytes. The far end is `tests/lptlink/partner.py`'s `SocketBox` — **real host sockets**, not a model — so the page really did cross one, out of an HTTP server the harness starts on 127.0.0.1. It writes its reply in **two sends with a gap between**, which is what produces a zero-length `NETV_RECV` while the socket is still up — the one mistake that gives a plausible short page rather than an error. **That gap is WAITED FOR, not slept**: the first version used 1.5 s of wall clock and the condition never arose, because one wire exchange under a stepped partner costs seconds of host time, so the body was always there before the guest asked. The server holds the body until the far end has actually served an empty read on a live socket. Measured on a 5150: ten wire commands, 232 bytes in reads of 65/0/167/0, `HTTP/1.0 200 OK` at the front, every handle back. The wire's own verdict is still the 5150's (§62.10.3) | `make socktest && python3 tests/socktest.py` |
| `brfetch` | **the BROWSER fetching a page** (§71): a URL typed into the location bar through the 8255 and int 09h, an HTTP GET over the cable, and the reply through the same parser and painter a floppy page goes through. It checks the REQUEST the server saw (the exact request line and a `Host:` header — a malformed GET still gets an answer out of a forgiving server and fails against every real one), that ink appeared **below the bar**, and that the browser's own status line agrees | `make browsertest && python3 tests/brfetch.py` |
| `ethcfg` | **an address set BY HAND, and remembered** (§72.7). QEMU's for the same reason its neighbour is. It exists because every part of it fails QUIETLY: a window that draws but takes no keystroke, a field whose rect and whose hit test have drifted apart, an `Ok` that parses nothing, and a setting that is applied and never written. Five assertions in the order a user meets them - the default is Automatic with slirp's address; `Set Up` opens a 216x141 window seeded from the live addresses; typing through the 8255 and int 09h reaches `W_ONKEY` and `Ok` changes the driver's LIVE addresses (read out of its image, not off the screen), while a field that was NOT edited survives; the page then says `Manual` instead of `Bound` and greys Renew; and **it survives a REBOOT**, which is what the feature is for. It finds the Ethernet row at `[cp_nst]` rather than counting five - on a one-adapter machine the Display page is not drawn at all (§31.10.1), so a count clicks `Sound` on CGA and fails somewhere else entirely. It puts the machine back on Automatic afterwards, because a gate that dirties the tree for its neighbour will eventually be run in the wrong order | `make ethertest && python3 tests/ethcfg.py` |
| `ethernet` | **an NE2000, a TCP/IP stack, and the browser over it** (§72). **THE ONE GATE HERE THAT IS QEMU'S RATHER THAN MARTYPC'S, and not by preference**: MartyPC has no network card of any kind, so the emulator this tree develops on cannot host `ETHER.DRV` at all — it is on the short list beside the 286/386 targets and §52.1's IDE rung 1. Five assertions climbing the stack: a card was found and its PROM address is plausible (not zero, not all ones, not multicast); DHCP bound and the address, router and name server are the ones slirp hands out — which is already transmit, receive, broadcast, UDP and the option walker before a socket exists; the browser fetched a page over TCP and the exact request line reached a REAL host socket; `br_nstate` is `BN_DONE` and `br_nlines` non-zero; and the `.o88` under test is the one `make` builds for the shipped floppy. **Every assertion is about behaviour and none about speed** — QEMU is not an 8088 and there is no number here for PERFORMANCE.md to want off the 5150. `ETHDUMP=<file>` writes every frame either way to a pcap, which is the instrument for this driver: a stack that is silent and a stack talking nonsense look identical from inside the guest | `make ethertest && make browsertest && python3 tests/ethernet.py` |
| `drvscroll` | **the Drivers page's pressed look costs one control** (SPEC.md §31.1.2). Not a package — a host script, because what it asserts is what is on the GLASS around a mouse edge. The decisive check is the ROW BAND either side of an arrow press: 0 differing pixels *and* no flashing rect reaching into it, because a control redrawn to the same value is invisible to a pixel diff and unmistakable to `flicker`'s bbox. It also proves the arrow is drawn HELD, that sliding off puts the cell back to 0 differing pixels, that one click is one row, that a greyed arrow draws and scrolls nothing, and that the scrolled pane matches a forced full repaint. Reads the 1bpp framebuffer, so CGA or Hercules — and both were run | `python3 tests/drvscroll.py [machine] [image]` |
| `drvcall` | **a package reaching a driver** (§20.11): `OSAPI_DRV_CALL`, the `DSV_PKGCALL` fence, and — the half nothing else can check — that the driver was handed the *package's* segment in `ES`. Its counterpart is `RAMDISK.DRV`'s two package verbs, so it needs no card and no cable and runs on MartyPC. The assertion is in `tests/drvcall.py`, which reads the three report strings out of the package's own image and drives the Control Panel tick, so it sees the refusal **before** the driver is published as well as the answer after | `make drvcalltest && python3 tests/drvcall.py [--adapter herc]` |
| `stackprobe` | the 256-byte task-stack margin (§8) | `make test TESTAPPS=build/stkprobe.img` |
| `xmtest` | the extended-memory **teardown** (§41.5/§29.4): does a closed instance's blocks above 1MB get freed? Needs a machine with a store, so **QEMU on a 386** — the target machine can never have one. The assertion lives outside the package, in `tests/xmcheck.py`, which reads `xm_tab` over QMP around the close | `make test TESTAPPS=build/xmtest.img` then `python3 tests/xmcheck.py build/qmp.sock` |
| `trklog` | not a gate — a **recorder**. Tracker itself, built with `-DTRKLOG`, logging one record per system tick and writing it to `TRKLOG.TXT` (SPEC.md §45.14) | `make test SB16=1 TESTAPPS=build/trklog.img` |

`benchlib.inc` is the one shared source under `tests/` — the timing loop, the
48-bit arithmetic, the report arena and the file writer that `gfxbench` and
`sysbench` both use. It is shared rather than copied for the reason
PERFORMANCE.md Part 6 rule 7 gives: two harnesses that disagree is how three
of the four sizing bugs in this project were found, and two harnesses that
were copy-pasted cannot disagree.

`trklog` is the odd one out and worth reading the shape of before writing
another like it. It is not a separate program: it is `apps/tracker` assembled
a second time with `-DTRKLOG`, so the thing being measured is the shipped code
and not a copy of it that can drift from it. The hooks in `apps/tracker` are
every one of them inside `%ifdef TRKLOG`; the shipped `TRACKER.O88` carries no
records, no claims and no D/W keys. Recipe:

```
make test SB16=1 TESTAPPS=build/trklog.img   # builds the disk on demand
# double-click Disk B, launch TRKLOG.O88
# X    XT mode (5,500 Hz - what the 4.77MHz floor machine boots with)
# L    load BEVERLY.MOD (it is on the same disk), which starts playback
#      NOT 'P' - that is the PATTERN LOOP toggle (song vs loop one pattern).
#      The bench build lists its own keys on the windowed splash, because P
#      was reached for as the pace cycler for a whole round of testing.
#
#      CLICK.MOD is FOUR patterns and four orders (tests/mkclick.py), and it
#      had to become that: with one of each, `Pos` never left 00, P had
#      nothing to loop that the song did not already play, and every click
#      sounded identical - so the ear could not tie the display to the music
#      at all. Now the position is AUDIBLE (a pitch per pattern, C-2 E-2 G-2
#      C-3, climbing) and so is which of a bar's four clicks you are hearing
#      (row 00 is a fifth above the other three: TICK tick tick tick).
#      Verified in a MARTYPC_WAV capture: bursts 2.0 s apart at ~1555/1025/
#      992/959 Hz, then ~1753/1290/1257/1213, then ~2117/1555/1510/1455.
# D    arm the log       -> the status line counts 'LOG nnnn /0512'
# F    fullscreen; let it run through a few pattern boundaries (~9 s each)
# F    ...and back to windowed - the key that entered leaves (SPEC.md 45.17)
# Esc  also back to windowed  (W is refused in a bracket - the file API is
#                         UI-callback-only, SPEC.md 53.7; L likewise, which is
#                         why it is off the fullscreen legend)
# SPACE stop, ENTER RESUME where it stopped, HOME restart from the top
#      (SPEC.md 45.17 - Enter used to restart, which threw away the row
#       trk_play_stop had just parked)
# W    write TRKLOG.TXT to B:  -> 'Wrote TRKLOG.TXT'
#
# M    at ANY point, windowed or fullscreen: stamp FL bit 10h into the
#      current tick because you HEARD something. It is the only input to
#      the file that is not a measurement, and the first field capture is
#      why it exists - 62 seconds in which every counter was healthy and
#      nothing in the file knew whether any of it was audible.
#
# Y    the display back on the MIXER (SPEC.md 45.15 off) - and again to undo
# T    the frame clock back on the tick (SPEC.md 45.16 off) - likewise
#      Both are recorded in FL (20h, 40h) on EVERY record, so a file always
#      says which mode it was taken in. They exist so that "it is smooth
#      with Y, jerky without" can be answered in one sitting on the machine
#      that has the problem, instead of one rebuild per hypothesis.
#      T is WINDOWED-ONLY in practice: ttx_clkpick runs at bracket entry and
#      sets the clock back, so pressing it inside fullscreen changes nothing
#      until the next F. Press it, then F.
#
# E    GONE. The windowed pacing experiment (SPEC.md 45.16.3/45.16.4) - seven
#      cadences for a windowed ROW counter - is concluded, and its key, its
#      three surviving modes, both hold animations and the reveal have been
#      removed from tests/trklog.inc. SPEC.md 45.16.6 is the outcome: the
#      windowed readout is 'Pos xx/yy' and there is no row on it, so there
#      is no cadence left to pace. Judging it needed CLICK.MOD, because on a
#      real module the mixer eats the drawing (BEVERLY.MOD gets 6.0 frames/s
#      against 7.14 rows/s) - which is what the experiment found and why it
#      ended in not drawing the field rather than in a better cadence.
#
# K    XT mode's SAMPLE RATE - 4,000 / 5,500 / 11,000 Hz - WITHOUT leaving XT
#      mode (docs/FIELD-NOTES.md 16). Windowed-only, takes effect at the next
#      Play, and the text screen's header cell says which rate is running.
#      X is the wrong way to do this: it changes the SURFACE too, to the
#      graphics FT2 screen, which SPEC.md 45.9.1 measured at 2,567 glyph
#      cells a second - unusable on a real XT, and a starved frame clock
#      degenerates tui_playpos so the measurement is not the one you wanted.
```

**The buffer is a RING of the LAST 512 ticks (28 s).** It used to stop at the
first 1,024, which is the wrong half of a session to keep - the listener arms
it, plays, hears the thing, and only then reaches for W. `tlog_save` emits
oldest-first, so the file reads forwards in time either way.

The columns beyond §45.14's original set: **AR AP** are what the SCREEN
showed, which since SPEC.md §45.15 is the row the CARD is playing and not the
row the mixer is on - so `PS PT RW` and `AR AP` disagreeing by the ring lead
is the healthy case, and either of them frozen is not. **SD** is the number of
stamps between the two, i.e. the same lead counted in ROWS: it should sit near
`1.19 x BPM / speed` (22 at the default tempo) and pinning at 63 means the
stamp ring lapped. **FX DX** are the longest single drawing frame and feed
pass in that tick, in ticks, where 00 is the healthy answer for both.
**PLAY** is `CONS` interpolated between block IRQs (SPEC.md §45.15.1):
`PLAY - CONS` should stay between roughly zero and one block, and `AR`
advancing one row at a time rather than three is what it buys.

That set earned itself on its first run: `SD` pinned at 63 with `AR` frozen
for a hundred ticks and then jumping 44 rows, twice a capture, exactly when
`CONS` was in the upper half of its 16-bit range - which is how `mp_at_pos`'s
`jg` (a signed compare of the operands, so it honours the overflow flag) was
caught standing in for the kernel's wrap-safe `js` idiom. After the fix the
same capture holds `SD` at 22-25 and the screen row never stands still for
more than 7 ticks, which is the block-IRQ interval and not a stall.

Read it back off the image from the host with a FAT12 extractor, or mount
`build/trklog.img` — it is an ordinary floppy. **The disk must not be
write-protected**, for the same reason the bench disks must not be. The log
buffer is a heap claim taken at D and given back at D, so an unarmed log costs
no memory and cannot split the heap.

### Frotz: the story harness, which is `trklog`'s shape again

`apps/frotz` built a second time with `-DZHARNESS` (SPEC.md 61.13), for the
same reason `trklog` exists: the thing measured is the shipped code and not a
copy that can drift from it. Every hook is inside `%ifdef ZHARNESS` and the
shipped `FROTZ.O88` is unchanged to the byte — which is checkable, by building
it both ways and comparing sizes.

What it adds is a **teletype on COM4 (`0x3E8`)**: the story's output goes out a
byte at a time, its keystrokes come back in, and four markers say where it is.
So a story is playable from the host over a socket, and the answer to "does a
real story run" stops being a person reading a screendump.

```sh
make zh                                     # the harness interpreter
python3 tools/zharness.py ADVENT.Z3         # play its script, print the log
python3 tools/zharness.py ADVENT.Z3 --repl  # ...or type at it yourself
python3 tools/zharness.py --all --compare   # every story, diffed vs dfrotz
make zcheck                                 # the same, as a gate
```

It boots QEMU itself, builds the B: disk itself (the story arrives as
`STORY.DAT`, which is what lets one binary play all of them), and double-clicks
its way in — os8088 has no way to start a package from outside, so the launch
is a scripted GUI walk and the coordinates live in `tools/zharness.py`. **A
timeout waiting for `[[ZH:READY]]` is that walk, not the interpreter**;
`--shot` writes what the screen actually had on it. The walk is retried once
before it gives up: a double-click landing before the desktop has finished
drawing is decoded as two single clicks, and the giveaway is a screendump of a
bare desktop with the icon not even selected.

Three differences from a real session, each deliberate and each documented at
its `%ifdef`: no `[MORE]` paging (it waits on a key the script has no reason to
send, and the run deadlocks around the sixth room), no echo on the wire, and no
status line on the wire. The last two are what make the transcript comparable
to `dfrotz -p`, which prints neither in a form that survives normalisation.

`--compare` diffs the WORDS in order, not the lines: `dfrotz` wraps at its
`-w`, os8088 wraps at whatever the window is, and the harness sends the
pre-wrap stream. A wrong opcode changes the words; a different column count
does not. Both sides' commentary is dropped — `dfrotz`'s `Warning:` lines
(Balances calls `@get_child` on object 0 every turn and says so) and os8088's
own notices (`No room for undo; play continues.`) — and neither side's halt
ever is.

**`refused, with a reason` is a pass.** A story too big to be resident is
turned down on purpose (§61.4/§47), so `BRONZE.Z8` reports `No room for the
scrollback.` on a 640KB machine and the gate counts that as correct. A gate
that failed on it would be arguing with the design.

The reference's upper window is recognised **by its padding** and dropped:
`dfrotz` positions that text with spaces, and its own prose never carries a run
of three because it wraps at `-w` and separates words by one. That is what lets
a story whose upper window holds a quote box rather than a status line —
`BEAR.Z5`, `CURSES.Z5` — be compared at all. It fails safe: a story that
indents in the *lower* window loses those words from the reference and keeps
them here, so the error is a divergence to investigate, never a silent pass.
There is no way to opt a story out of the diff, on purpose: an opt-out is a
place for a real divergence to hide, and both of the ones this found are real.

**`make zgfx` is the other half, and it asks what the transcript cannot**
(SPEC.md 61.14). Every check above is about characters the story *printed*; a
story that draws a quote box into the upper window and then loses it prints
exactly the same characters as one that keeps it, so `zcheck` is structurally
blind to a whole class of defect. `zgfx` compares the interpreter's own model
of each row against the pixels under it, does it again on a freshly repainted
window, and holds each story's opening screen against the real curses Frotz's.

```sh
make zgfx                                     # the gate, stories + the fixture
python3 tools/zharness.py BEAR.Z5 --graphics  # one story
make zscreens                                 # re-take the golden screens
```

Three things about running it are worth knowing before a failure confuses you:

- **it is slower**, by a screendump per prompt. That is the only reason it is a
  separate target;
- **every complaint leaves a PNG** in `build/zh/`, named for the story, and the
  raw wire — markers, both windows' grids, every split and repaint — is in
  `build/zh/<story>.wire`. The `.log` beside it is the prose with all of that
  taken out, which is the wrong file to open when the question is graphical;
- **`make zscreens` is the only part that needs anything installed** (`brew
  install frotz`, `pip3 install pyte`). The goldens are committed, so the gate
  itself is still nasm + qemu + python3. Re-take them when the Frotz window's
  size changes — the geometry is in each file's header and the gate refuses,
  with the reason, rather than reporting the mismatch as a story failure;
- **each golden is two takes, and the gate wants the words common to both.**
  A story may roll for its opening — `CURSES.Z5` draws a different epigraph
  every time — so the file measures how much of the screen is settled instead
  of assuming all of it is. Fourteen of fifteen come out fully stable.

**Do not make it send keys.** An earlier version forced the repaint check by
sending `PgUp`/`PgDn` — which the interpreter takes before the story's input
ring, so they cannot be mistaken for the story's own input. It nonetheless left
`DREAMHLD.Z8` with no further prompt inside a seven-minute budget, every time,
where the same run without it finished cleanly. That is worth chasing on its
own (SPEC.md 61.14); it is not worth a gate that fails for two reasons at
once. The check now rides on the repaints stories provoke themselves.

**A `@random` story can diverge run to run in `zcheck`, and that is not a
regression.** Both interpreters seed from the clock (Standard 2.4,
`zx_seedclock`), so a word diff over a story that rolls dice is not
reproducible — `ZTUU.Z5` diverged on one lamp message in one run and matched on
the next. Re-run before believing a divergence that names a random event; the
graphics gate is unaffected, because its golden knows which words were rolled
for.

It found four defects on its first run over the library, and the two that
matter most to a reader were both about the upper window — a quote box erased
by the shrink that follows it, and a `Flags 1` bit that turned 905's clock into
a move counter (SPEC.md 61.5). Neither changed one character of any
transcript, which is the argument for the gate in one sentence.

`filetest` also has a fragmented-volume variant, `build/filetest-frag.img`,
and its results are worth pairing with the host-side fsck — the in-kernel
free-space check and `python3 tools/os88disk.py --verify <img>` catch
different bugs.

`stackprobe` is the one gate whose QEMU answer is NOT the answer: its worker
0xCC-fills its own stack slice, spins so every interrupt the machine takes
lands there, and reports the live high-water mark against 256 (the canary
line confirms it watched the word SPEC.md §8 protects). SeaBIOS services its
interrupt entries on an internal stack, so under QEMU only this kernel's own
tick and mouse handlers land on the slice (~90 bytes with the worker's own
frames); a real IBM BIOS runs int 09h on the current task's stack and STIs
early, so the tick and the mouse nest ON TOP of it. `make stackprobe` builds
`build/stkprobe360.img` for exactly that trip: boot `os8088-360.img` on the
real machine (or `make xt`), launch `STKPROBE.O88` off the probe floppy, hold
a key down, mash the mouse, play a Tracker module — then read High water.
Measured on a real 5150 (Hercules, 20MB MFM) under a floppy-to-hard-disk
copy plus typematic plus mouse: **112 of 256, canary intact, 217 samples** —
the ~20 bytes over QEMU's 92 being the BIOS nesting this gate exists to see.

**Benchmarks** answer *how fast*. `fontbench` prices the *primitive* (SPEC.md
§6.1.1): one ten-character run drawn four ways, as the hand-written
`gfx_fill` + `font_str` pair and as one `font_run`, each byte-aligned and
again at x+5. `typebench` prices the *keystroke* (§11.94): 40 characters typed
into a 40-cell line with the whole line redrawn after each, which is what
`np_redraw` does to its dirty band. `gfxbench` prices the *whole drawing
surface* on whichever adapter it booted on; `sysbench` prices the *machine*
underneath it. All four ride one disk.

```sh
make bench                                                 # build the two disks
make test                            TESTAPPS=build/bench.img
make test VIDEO=cga                  TESTAPPS=build/bench.img
make test VIDEO=herc HERCSEG=0x7000  TESTAPPS=build/bench.img
```

### A benchmark that is meant for the FIELD is ONE BOOTABLE DISK

Binding, and stated here because this is the file a person reaches for when
they are *writing* a harness — docs/FIELD-MACHINES.md has said it since
`make field` was built, under that target, and being filed under a target is
how it gets missed. `tests/npbench` was built as a second disk to be put in
B: and had to be rebuilt as a boot disk.

**The calibration machine has one floppy drive** (docs/FIELD-MACHINES.md). A
harness on a data floppy therefore means a swap mid-session, and on that
machine a swap is a walk to another room and back — so the operator cannot
boot the OS and mount the bench at the same time, and the numbers do not get
taken.

So a field harness rides the SYSTEM disk: `--boot`, `--kernel`, the drivers,
`TASKMGR.O88` in `SYSTEM/`, and the harness itself where a double-click
reaches it. `make field` and `make npbench` are both that shape; copy either
rule. Two consequences that are easy to miss:

- **It must not be write-protected.** The report is the deliverable and a
  protected disk answers int 13h status 03h, which the OS correctly reports as
  `Write protected`.
- **A harness that is a rebuild of a shipped app should carry the app's own
  file name**, not the harness's. SPEC.md §54's association resolves a
  document by the app's *stem*, so `npbench` ships as `APPS/NOTEPAD.O88` and
  the operator opens the reference note by double-clicking it. Named for
  itself, they would have to launch it and drive a file dialog by hand.

`make bench`'s four harnesses ride a scratch `TESTAPPS=` image because they
are driven under an emulator here, where there are two drives; the moment one
of them is wanted on the 5150 it needs the treatment above.

**The disk to send is `make combo`.** `build/combo.img` is one 360KB bootable
floppy carrying the system, every application, every game and all four
benchmarks, and it is the default for a field or bench request — not the
`herc.img`/`cga.img` pair, which is what this used to be. The pair existed
because both cards live in the 5150 permanently and the §39.1 probe can only
be asked one question at a time, so the adapter was a property of the
**build**; since §39.11 it is not. The Control Panel's **Display** page
switches the primary at run time, so one disk takes a set from both cards —
run `GFXBENCH.O88`, switch the display, run it again, and it names each report
after the adapter it *found*. `sysbench` runs once: none of its rows is a
question about the adapter. `make field` still builds the narrow disks
(docs/FIELD-MACHINES.md has the table: a 720KB geometry, two knob kernels, and
a pinned adapter for comparing against an older set).

### `gfxbench` and `sysbench` — the two that write a file

The first two benchmarks answer one question each and fit on a screen. These
two answer forty, and a CGA screen holds seventeen lines — so they page
(`Space`/`PgDn`/`PgUp`/`Up`/`Dn`/`Home`/`End`, or a click) and they **save
the whole report to a text file** with `S` or the Bench menu. `R` re-runs.
That file is the deliverable: it is meant to be carried off the machine and
pasted into [PERFORMANCE.md](../PERFORMANCE.md).

| what | where it lands |
|---|---|
| `gfxbench` on VGA / Hercules / CGA | `GFXVGA.TXT` / `GFXHERC.TXT` / `GFXCGA.TXT` |
| `sysbench` | `SYSBENCH.TXT` |

**Driving one from a SCRIPT is four steps and three of them have a trap in
them.** Written down because getting a benchmark to run unattended cost most
of a session, and none of the failures says what it is:

- **Reach the app through the Bench MENU, not a keystroke.** A scripted
  `m.key("KeyR")` did not reach `sysbench` in a run measured here — the
  splash was still up 150 s later — while `mo.menu(110, 8, 110, 26)` (its
  first item, `Run`) started it every time. The app is frontmost and its
  menu bar is drawn in both cases, so nothing on screen tells the two apart.
- **Do NOT `settle()` after starting the run.** The machine is deliberately
  FROZEN while a benchmark runs, so stillness means nothing, and a `settle`
  after it never returned inside a 600 s limit here. Sleep a fixed generous
  span instead — the app says ~40 s on a 4.77 MHz 8088 — and read the file.
- **Read the FILE, not the screen**: the report self-saves when it finishes,
  and the whole thing is forty-plus rows the screen pages through.
  `os88flush.Flush(marty=m).volume(0).read("SYSBENCH.TXT")` is it, sharing
  the one debug connection (a second `Marty` HANGS).
- **Keep the driver script OUT of the session scratchpad.** One vanished
  mid-session here and the run died with `can't open file` — which reads
  exactly like a path typo. `/tmp/<something>/` of your own is fine.

...and CLAUDE.md's `pgrep -f` warning applies to waiting for it, twice over:
`pgrep -f runsb.py` matches the very shell running the `until` loop, so the
loop never ends, and a `( ... ) &` nested inside a backgrounded tool call is
reaped before it finishes. Wait on the tool call itself.

**On an extended desktop the name is the card the SANDBOX is on**, not
`[vid_kind]` — `gfxbench` resolves its own window's origin against §57.4's
`VD` block, and the framebuffer segment, stride, bank count, status port and
the raw VRAM rows' addressing all follow it. So two cards give two reports
from one launch: run, drag the window onto the other monitor, run again.
Check the new **`sandbox straddles`** row before comparing two of them.

**The file goes to the CURRENT volume and directory** (SPEC.md §19.2), which
right after launching a package off the bench disk is that disk's root — so
the ordinary thing works. It means the bench floppy must **not** be
write-protected, and on 86Box that is the `wp://` prefix the config keeps
growing back — which `make xt` and friends now strip off both floppy keys at
launch.

`gfxbench` is ONE package for Hercules and CGA on purpose. Both are the same
1bpp software renderer over four different numbers (SPEC.md §39.3), which it
reads from `OSAPI_VIDEO` at run time; two sources would be two chances to
drift, and the whole value is that the Hercules column and the CGA column are
the same measurement. It runs on VGA too, for contrast.

What it measures, and why in that shape:

- **Raw bandwidth first**, because everything above it is explained by it.
  The same loop — 32 rows of 64 bytes — runs against plain RAM and against
  the framebuffer, so the ratio between those rows IS the bus penalty with
  the loop, the addressing and the string instruction identical on both
  sides. Word write, byte write, word read and byte read-modify-write are
  priced separately because the kernel's inner loops use all four and on an
  8-bit bus they are not proportional.
- **Primitives at TWO SIZES** wherever the cost has a per-call part and a
  per-pixel part (8×8 against 64×64, 8 px against 256 px). One size cannot
  separate them, and pricing a rect the harness never drew needs both terms.
  The derived block does that subtraction and prints its inputs beside it.
- **`gfx_blit4` twice** — a solid source and a four-pixel-run source. That
  pair is PERFORMANCE.md Part 3 item 4 made mechanical: a run coalescer that
  has quietly stopped coalescing shows as a ratio near 100, and one number
  could never show it.
- **The same ten characters as `fontbench`**, so the two harnesses check each
  other for free.
- **The gfx lock, measured backwards** — `GFX_UNLOCK+LOCK pair`. A package
  cannot call `OSAPI_GFX_LOCK`: it runs inside a callback that already holds
  it and the lock is not reentrant, so that is a deadlock rather than a slow
  row. Unlock-then-lock is the same two routines in the other order, and it
  is an idiom the kernel already uses inside a callback (`fm_drag`). What is
  in them is not the mutex but the **mouse cursor** (SPEC.md §7.1), and a
  field log put the pair at 21.8% of a Missile Command session with no pixel
  of the game in it. It is the one row here whose measured span cannot be
  interrupt-free — `gfx_lock` ends with `sti` by contract — so one IRQ per
  iteration can land inside it; the error is bounded and upward, and
  `[bl_max]` and the `!` flag are what to read it against.

`sysbench`'s headline is the one PERFORMANCE.md Part 2 has been quoting from
memory: **8086-nominal clocks against a real 8088**, per instruction class,
with the book figure and the ratio printed beside the measurement. The
interesting part is that the ratio is not one number — it is near 1.0 for
`mul`, which is execution-bound, and much worse for `nop`, which is starved
by a 4-byte prefetch queue behind an 8-bit bus. It also prices RAM
bandwidth, the clock ladder, the API's far-call floor, **what the kernel's
own interrupts cost per second of ordinary work** (the same workload timed
with interrupts off and then on), and the floppy — twice, because the first
read pays the motor spin-up and quoting either figure alone misleads.

**Two of its floppy blocks exist to pin the two numbers the MartyPC disk model
has no measurement for** (PERFORMANCE.md Set 35, `tools/martypc/patches/04-*`).
Both go through the kernel's `dsk_dbg_raw`, so both need a `DISKCNT=1` kernel
and are silent on any other — which is what every `make field` disk is.

- **The head step.** Every other raw row reads one track and never moves the
  head, so the model's step rate is the BIOS's own SPECIFY request and its
  settle is the DPT's, both on trust. `seek N cyl, pair` reads cylinder 0 and
  then cylinder N, so an op holds two seeks of that distance. **Read the rows
  as revolutions and expect whole ones**: a read ends at a fixed angular
  position, so the seek happens inside the wait for sector 1 to come round and
  is invisible until it is longer than that wait. What the block measures is
  therefore *the distance at which the cost steps up*, not a slope — and the
  `seek 0 cyl` row is the zero of that scale rather than something to trust.
- **Spin-up.** `1 sector, motor COLD` waits for the BIOS's own motor countdown
  at `0040:0040` to expire, checks `0040:003F` and prints it, then times one
  sector; `1 sector, motor warm` is the same read with the platter already
  turning. Both are N = 1 and must be — the event happens once. A `motor
  status 40:3F` of anything but `00` means the drive never stopped and the
  cold row is not cold, which is the one way this can lie and so is printed.

Three things about reading their output:

1. **A method-`t` row of 0 counts finished inside one 55 ms tick.** True on a
   fast host, and never true on the machine this is for.
2. **A `!` flag means one iteration came within a third of the PIT wrap.**
   The number is still probably right; it is no longer trustworthy.
3. **Under QEMU almost every row is noise**, and two are worse than noise:
   the retrace period (QEMU's status port toggles on every read so a poll
   always terminates) and the VRAM rows under `HERCSEG=` (B0000 is unmapped,
   so those rows measure plain RAM and the bus ratio reads 100). Both say so
   in the report's own header. `build/bench360.img` on real iron is the
   point of the exercise.

Every one of these images builds on demand — `TESTAPPS` is a prerequisite of
the test targets, so naming one is enough. `make bench` exists for building
the two benchmark disks *without* booting, e.g. to write `bench360.img` to a
real floppy.

The rest of this section is about the benchmarks, because a gate's answer is
a boolean and does not rot the way a number does.

**The `testing` branch still exists, and is now for developing these**, not
for holding them. A harness takes several rounds to get right — two of the
three corrections below were to the measuring apparatus, not the thing
measured — and that iteration does not belong in `experimental`'s history. A
finished harness lands here; the midway artifacts of writing one stay there.

**Treat every number as provisional and cite where it came from.** This is
not a caveat about tidiness — the figures have been wrong in ways only real
hardware exposed, twice in quick succession: the elapsed counter was 16-bit
and a real run overflowed it, and then the ratio overflowed because it came
from counts shifted right by 4 that real rows exceed. A third correction went
the other way: SPEC.md §6.1.1 predicted `font_run`'s true win sat near the
framebuffer-traffic figure, and a 4.77 MHz 8088 with a Hercules card measured
1.30x — the *instruction* figure to three digits. Per-cell overhead dominates
the byte-writes it guards. So a benchmark number quoted without a date and a
machine is worth very little.

**Under QEMU the numbers are not time at all.** QEMU runs the guest at host
speed, so add `-icount shift=3,sleep=off` and the PIT counts guest
*instructions* — reproducible and machine-independent (±1 count across runs),
but not microseconds, and it understates the mono win because what alignment
removes is disproportionately memory traffic. The Makefile has no knob for
it; override the whole command instead, which is the shortest correct form:

```sh
make bench
make test TESTAPPS=build/bench.img \
     QEMU="qemu-system-i386 -icount shift=3,sleep=off"
```

**Driving a harness under `-icount` is a different job from driving one
without it**, and both ways of getting it wrong look like a click that was
ignored. Guest time and host time come apart: a boot that takes 12 s of wall
clock without the knob takes 45–90 s with it, and a screendump taken on the
old schedule shows the desktop as it was before the click landed rather than
after. **Poll the dump for the state you want; never sleep a fixed interval.**
`shot.py` prints the non-white percentage on every call, which is enough of a
state machine for this: the desktop is one figure, a Disk window another, a
report page a third.

And **there is no double-click in `tools/mouse.py`** — two `mouse.py click`
runs are two processes and about a second apart, which the kernel reads as two
single clicks (§9's `DBLCLICK` window). A double-click has to be one process:
`goto`, then `mouse_button 1` / `mouse_button 0` twice with ~0.12 s between.
Importing `mouse.py` to get `goto` has a trap worth naming, because it fails
silently in the direction that looks like a lost click — `importlib`'s
`exec_module` runs the module with `__name__` set to the loader's name, so
`mouse.py`'s own `if __name__ == "__main__": main()` never fires and **the
pointer never moves**. The clicks then land wherever the pointer happened to
be, which is usually the last place a *previous* command left it, so the
script appears to work for exactly as long as the two positions agree.

`build/bench360.img` on a real 4.77 MHz 8088 (or 86Box) is where the PIT is a
wall clock and the microsecond column means microseconds. That is where these
numbers are worth taking. A VGA run measures the *fallback* path by design —
`font_run`'s fast path is mono-only.

Nothing under `build/` is committed — bench artifacts included, along with the
shipped images themselves — so there is no way for one of these disks to reach
the repo or a release. What keeps them off a normal build is `all`, which
builds nothing from `tests/`: that is the arrangement this folder exists for.

---

### `tests/assoctest` — the file type association gate (SPEC.md 54)

```
make test TESTAPPS=build/assoctest.img
```

...then **double-click `TEST.AST`, not `ASSTEST.O88`**. Every row is about
what happens when a *document* is opened, so launching the program by hand is
the control: rows 1-4 read `-` and that is correct, not a failure.

Six rows, all of which must read **PASS**:

1. the **header declaration** (§54.6) routed the document here - nothing was
   registered at runtime, so only a rule in this package's header can have.
   It ships **no icon**, so its block sits at offset 32 rather than 96, which
   is the layout arithmetic most likely to be got wrong
2. `OSAPI_ARG_FILE` handed over a name, and it is `TEST.AST`
3. the locator works - `FILE_GOTO` then `FILE_READ` returns bytes
4. `ARG_FILE` is **read-and-clear** - the second ask reports nothing, which is
   what stops a later instance inheriting a document
5. `OSAPI_ASSOC_SET` takes a registration
6. ...and **refuses honestly** when the table fills, rather than corrupting it

Before the double-click, the listing itself is half the test: `TEST.AST` must
already carry a **document page icon**, and a *bare* one, because the program
ships no glyph to inset.

### `tests/dispclose.py` — the close negotiation and the alert (SPEC.md 75, 27.15)

```
make && python3 tests/dispclose.py
python3 tests/dispclose.py --machine os8088_5150_herc_gla
python3 tests/dispclose.py --machine os8088_xt_vga
make small && python3 tests/dispclose.py --small
```

A MartyPC gate, one boot per run, driving Note Pad through every branch of
closing with unsaved work: a clean note closes silently; a dirty one refuses
and puts the alert up; Cancel, the alert's own close box (which is
`ASKA_CANCEL` through `uia_reap`, not a button) and Don't Save all behave;
Save on an **unnamed** note raises the Save As dialog and its commit is the
quit; Save on a **named** one writes and closes with no dialog at all. It
ends by reading `NOTES.TXT` **off the floppy with `os88flush`** rather than
asking os8088 whether it saved — docs/FIELD-NOTES.md 4's rule, the writer and
the reader being the same FAT12 code.

Four things in it are worth copying into the next gate of this shape.

**The alert is found in the WINDOW TABLE, never by looking at pixels.** It is
a package's own window (SPEC.md §75.3), so no kernel word names it — what it
IS, in kernel terms, is an UNOWNED window (`wm_owner` = 0xFF) that is not one
of the windows the test already knows about. **Exclude the file dialog**,
which is unowned too and which Note Pad raises from the alert's own Save
button: without that, the moment the feature works reads as a failure.

**A shared rect keeps the drawing and the hit test CONSISTENT; it cannot make
them RIGHT.** `os88ui_arect` is called by both, and two register bugs in it
(a `mul` clobbering the row origin, then a callee answering in the register
that banked the click's x) each produced buttons that drew perfectly and
could not be clicked — because the painter passes the index and never passes
the point. The gate is what caught both, and the tell for the second was a
click on **Cancel** opening a Save As dialog.

**A settle is not a launch.** `dispcp.open_named` ends in `settle`, and a
settle is two identical frames a second apart — which a package LOAD
satisfies, because the machine is frozen under the gfx lock for the whole of
it and the screen is perfectly still. `wait_launch` polls the window table
instead. The big build got away without it, which is the worse of the two
outcomes.

**A scratch disk is rebuilt, never cached on existence.** The first version
skipped `os88disk.py` when `build/npclose.img` was already there, so the gate
ran an earlier build's Note Pad and reported *its* behaviour — an hour spent
on a package whose source and whose memory disagreed.

**`--small` is the one gate here that drives `kern_small`, and it needs two
things nothing else does**: the symbol map has to be asked for explicitly
(`os88sym.syms(("KERN_SMALL",), check=False)` — `linear` proves its map
against `build/kernel.bin`, which is the big build), and **`WIN_SIZE` is 28
there, not 34**, because `W_ONDRAG`, `W_ONTIMER` and `W_TIMER` are inside
`%ifdef KERN_BIG`. Read with 34 the window table looks plausible for slot 0
and is nonsense from slot 1 on, so a visible window reads as "not used" —
which is exactly how that hour was spent the second time.

## Modelling the old machine from a fast one

Everything above is about *where* to run a test. This is about the systematic
error in running it anywhere but the target, and it has now cost four bugs, so
it is worth stating as a method rather than a warning.

**Most of this section is about QEMU, and MartyPC removes a good deal of
it** — a cycle-accurate 8088 does not have "a clock that tells you nothing",
and a constant sized while watching one is sized against roughly the right
machine. Read it anyway, for two reasons. It is the record of *how* these
mistakes are made, and the shape recurs whatever the emulator: an
optimisation that keeps its form and loses its substance still measures as a
success. And **the part that MartyPC does not fix is the disk**, where its
error is 30x and in the flattering direction — so every rule below applies to
disk work on MartyPC exactly as written.

**The container is roughly three orders of magnitude faster than a 4.77 MHz
8088.** Every constant you size while looking at QEMU is sized against the
wrong machine, and the failures are not proportional — they are structural,
because the constants encode *ranges*:

| what was sized against QEMU | what a real XT did |
|---|---|
| a 16-bit elapsed counter, one subtraction start-to-end | rows are 1.5M counts; it lapped silently into a small plausible number |
| `>= 32768 means the run overran` | most legitimate rows are 32768..65535; it discarded them |
| a ratio computed from `counts >> 4` | `>> 4` is still 90,000; it overflowed the word and printed 696 for 134 |
| `OSAPI_WM_GROW` on every keystroke | free in the emulator; a visible flicker in a 13×13 corner at 33 ms a keystroke |

The rule that falls out: **when a harness has to hold a range, size it from the
slowest machine it will ever run on, not the one in front of you.** A 32-bit
accumulator folded per iteration costs a few instructions and cannot lap; a
16-bit one sized "generously" against QEMU is wrong by 20x on hardware.

### Three calibration numbers, so an estimate needs no machine

All three are measured on the 4.77 MHz IBM 5150 this project targets
(`tests/gfxbench` and `tests/sysbench`, PERFORMANCE.md Part 9), not modelled:

- **About 756 µs of fixed cost per `gfx_*` drawing call**, before it draws a
  single pixel. `GFX_PIXEL` and an 8-pixel `GFX_HLINE` measured within 1 µs of
  each other, on Hercules *and* on CGA, whose framebuffers are 13% apart — so
  the floor is CPU-side (~3,600 clocks of far call, lock, clip test, dispatch
  and `gfx_rowbase`), not bus-side. **A redraw is priced by how many primitive
  calls it makes, not by how many pixels it covers.** That is the single most
  useful sentence in PERFORMANCE.md and it is the one this project spent years
  not believing.
- **About 1 ms per 8×8 glyph cell.** Four independent measurements agree:
  `fontbench` 10.09 ms per ten cells, `typebench` 33.3 ms per forty,
  `gfxbench` 901 µs for one `font_char` and 915 µs per cell across a whole
  78×34 page. A 40-cell line redraw is ~36 ms, so a keystroke that redraws its
  row costs about that.
- **Instructions are the better proxy, not framebuffer traffic.** SPEC.md
  §6.1.1 predicted the opposite and was corrected by measurement: per-call and
  per-cell overhead dominate the byte-writes they guard. The general form is
  the **8088 instruction floor** — 4.34 clocks per instruction *byte*, which
  is what the prefetch queue can deliver — so an 8086 cycle count under-reports
  an 8088 by anywhere from 1.01× to 4.34× depending purely on encoding length.

Two figures that used to sit here were wrong and are worth knowing were wrong,
because they are still quoted in old commit messages: a framebuffer
read-modify-write is **79.6 clocks, not ~30**, and only about 7 of those are
the bus; and the "add 20–40% for the 8088" rule of thumb was replaced by the
instruction floor above. The back buffer's ~24× flush-to-render ratio was
never measured on hardware and never could be — double buffering was VGA-only
and this machine has no VGA, which is a large part of why SPEC.md §32 removed
the feature outright.

The full table is [PERFORMANCE.md Part 2](../PERFORMANCE.md), together with the
standing budget every redraw path in the tree has already been measured down
to. Check a change against that table before concluding it is free.

### Count work, don't time it — QEMU is exact about the first and useless at the second

**On MartyPC you can now do both**, which is the shortest statement of why it
comes first: `step` returns real cycles, so "how much work" and "how long"
are one question there — for the CPU. For a disk it is still neither, and no
amount of cycle accuracy makes 0.27 s into 8.07 s.

Under QEMU the split is absolute. The container's clock tells you nothing
about a 4.77 MHz machine, but the *amount of work* the guest does is identical
on both, and QEMU will report it exactly. So when the question is "is this
slow because it does too much?", **instrument a counter and read it over QMP**
rather than reaching for 86Box:

```nasm
; kernel/font.inc, in .text so the offset is fixed
dbg_cells:  dw 0
...
font_run_cell:
    inc word [cs:dbg_cells]
```

```sh
nasm ... -l /tmp/k.lst   &&  grep dbg_cells /tmp/k.lst     # -> 0x1E78
python3 tools/qmp.py build/qmp.sock 'xp /2xh 0x2478'       # KERNEL_SEG*16 + off
```

`h` is a word; HMP's `w` is four bytes. Editing any include **before** the one
holding the counter moves the offset, so re-derive it after every rebuild.

A **package** can write the same counter — `mov ax, KERNEL_SEG / mov es, ax /
inc word [es:0x1E7E]` — which is how a walk inside an app is counted without
knowing the segment its region was claimed at.

This is what settled the Note Pad question (SPEC.md §27.4). A user reported
typing getting slower as a note grew and inferred that more than one character
was being redrawn. The cell counter said **2 cells per keystroke at every note
length and every window width** — the drawing was already right — and a
counter in the layout walk said 404 iterations, growing linearly. The cost was
in a place no screenshot could show and no wall clock here could measure.

Two rules that fall out of it:

- **Measure before redesigning.** The obvious hypothesis (the delta span is
  growing) was wrong, and the fix it would have produced was a fix to working
  code.
- **A counter is not a timer.** It tells you how many times something ran, not
  what it cost. Multiply by the calibration numbers above to get milliseconds,
  and say that you did — ~500 8086 cycles per walk iteration is a reading of
  the instruction stream, not a measurement.

### Reading a kernel word whose offset you cannot grep for

`ticks` is in `.bss`, and a `.bss` label's address in the listing is
**section-relative** — `mov ax, [ticks]` prints as `A1 [2A02]`, and 0x022A is
an offset into `.bss`, not into the segment. Grepping the listing for it and
adding `KERNEL_SEG*16` reads the wrong address, and the wrong address is
usually a plausible-looking number rather than an error. Take the operand out
of the **built binary** instead, which is by definition the linked one:

```sh
python3 - <<'EOF'
import struct
d = open('build/kernel.bin','rb').read()
off = struct.unpack('<H', d[0x606:0x608])[0]     # the imm16 of osapi_get_ticks
print("ticks at linear 0x%X" % (0x600 + off))    # KERNEL_SEG*16 + off
EOF
```

`0x605` is `osapi_get_ticks`'s `A1` opcode; the two bytes after it are the
address. Every other `.bss` word is that one plus the difference of their
listing offsets. It moves on every rebuild — re-derive, and note that the
answer can be **odd**, so a word dump on even addresses straddles it and shows
two neighbours changing instead of one.

That is how `FSXF_FASTTICK` (SPEC.md §53.2.1) is verified, and the check is
worth copying for anything that claims to leave a clock alone:

```sh
# at the desktop, inside an armed bracket, and after leaving it - all three
# must read 18.2/s, because the ISR divides and [ticks] does not change rate
python3 tools/qmp.py build/qmp.sock 'xp /1xh 0x<ticks>'   # ...twice, 10s apart
python3 tools/qmp.py build/qmp.sock 'xp /2xb 0x<sch_fast>' # N inside, 0 after
```

Both of that feature's bugs assembled cleanly, booted, and drew a correct
first frame: one halved the tick rate (the flag was stored from the register
the divisor's table index had been shifted into), and the other corrupted the
low byte of the app's entry offset. The rate reading caught the first and
nothing else would have — a display that scrolls smoothly at half speed looks
like a display that scrolls smoothly.

### Prefer a self-checking harness to a careful one

Three of the four bugs above were caught by **one number on screen
contradicting another**, not by inspection:

- `typebench`'s CHAR row does 1.33x `fontbench`'s PAIR work, so it cannot be
  the smaller number — yet PAIR reported the overrun sentinel and CHAR
  reported 15551. Only one reading is consistent, and it identified the lap
  and its size.
- The ratio was wrong while the counts and milliseconds beside it were right,
  which localises the fault to the one column computed differently.

So put **redundant quantities on the screen**: a raw count *and* a derived
time, two rows whose relative sizes are known in advance, a ratio you can
recompute by hand from the columns next to it. A harness that reports one
number per run is one you have to trust.

**A number can also contradict a DESIGN RULE, and that is the same bug with a
longer fuse.** `WF_SNAP` was mono-only for most of its life because "what
snapping buys is `font_run`'s single-store path, which is 1bpp-only" — and
`typebench`'s own VGA row said alignment was worth **5.8%** there against
**2.1%** on Hercules, printed in SPEC.md §11.94 directly above the paragraph
explaining that the flag was a no-op on VGA. Nobody read the two as
inconsistent, because the number was filed as a fact about `font_run` rather
than about *alignment*, which is what the row actually measures. Re-measured on
a cycle-accurate 8088 the gap is wider — 3.4% mono against **9.4%** VGA — and
`typebench`'s own header was printing `snap:off` on VGA the whole time, three
lines above the rows refuting it. **When a measurement and a design rule
disagree inside one file, the measurement is not the thing to explain away**;
and a harness that labels its own state (`snap:on`/`snap:off`) rather than
assuming it is what makes such a disagreement visible at all.

**And the sharper form of the same rule: a gate must not be able to pass by
doing nothing.** `tools/sucheck.py` — the raise cache's gate (SPEC.md §11.96)
— covered Solitaire by clicking a hard-coded (300, 40) on the Disk window's
title bar, and on the geometry it actually produces that point is **inside
Solitaire's own rect**. So the click went to the window that was already
frontmost, nothing was raised, nothing was covered, `wm_su_take` was never
entered — and the run reported *78 differing bytes of 128,000* and PASS,
because comparing the screen with itself is the best possible score. A healthy
run reports 124. **The vacuous figure was better than the real one**, which is
the failure mode to design against: a number in the right range is not
evidence that the thing under test ran. Two fixes, and it wants both — the
click point is now computed from the window rects read out of the guest and
asserts that an uncovered strip exists, and the claim map is an assertion
rather than a note, so "the cache was never taken" fails instead of
explaining itself away. It cost a session's worth of counters in `wm_su_take`
to find, and the counters were only reached for because the claim map was
empty; had the cache been small enough to miss, the gate would still be
green and still be testing nothing.

**A hard-coded ROW NUMBER is that same defect one step further out: it does
not select nothing, it selects a DIFFERENT PROGRAM.** `tests/dispcorner.py`
opened "row 1 of B:, then row 3", believing that was `APPS` and then
`HELLO.O88`. It is `GAMES` and then `MISSILE.O88` — a root has no synthesized
`..` and a subdirectory does (SPEC.md §19.5), so the two listings are offset
by one from each other, and the Makefile's build order is not the display
order either (§19.4 sorts by name). Nothing errored: both double-clicks landed
on real rows and a real program launched.

**What that cost is the METHOD, not the coordinate.** Missile Command has a
worker task drawing every tick, and *do the thing, capture, force a full
repaint, diff* requires a screen that **settles** — two captures seconds apart
of a running arcade game differ because the game moved, and that difference
reads as precisely what the run was looking for. It reported 219 differing
pixels near a dragged window's old rect, then **62 for the identical script**,
and an unstable count is the tell: a redraw defect is deterministic. Driven
against the window it meant all along, the same two operations answer **0**.

Two guards, and it wants both. The row is looked up **by name out of the
guest** (`dispcp.row_of`, over the live mount snapshot), and the launched
window's size is asserted against `hl_tpl`'s 240x90 — so another program in
that slot *fails* instead of being measured. The harness's own output had
carried the refutation the whole time, printing `hello at (39,47) 560x381`
about a window whose template is 240x90. **A subject for a pixel diff must be
chosen for being INERT**, which is a good part of what `hello` is in the tree
for.

**And then the CONTROL was not a control**, which is the same file's third
defect and the one that would have voided every figure it ever printed. Its
"force a full repaint" was *open and close the Control Panel*, on the stated
belief that closing a window ends in `wm_paint_all`. **It ends in
`wm_paint_dmg`** (SPEC.md §11.91) — the incremental path, over the panel's own
vacated rect — so both captures came off incremental draws, and for a region
the panel never covered the second capture was the first one again. Zero, on
any build. `[cp_dirty]` is what to poke instead: `ui_task`'s `.chk_cp` is the
only consumer and it is `gfx_lock` / `wm_paint_all` / `gfx_unlock` with nothing
between, so there is no other thing the flag could mean — **and the flag is
read back afterwards**, because the poke happening is not the repaint running.

**`wm_paint_all` is still not enough on its own inside a window.** It draws
every window through `wm_draw_win`, which puts a valid raise cache back
*instead of* running `W_PAINT` (SPEC.md §11.96) — so the "full repaint" of a
window's content can be a byte copy of the capture it is being compared
against. That is `tools/notepad/pixcheck.py`'s tautology exactly, and its fix
is the one to copy: clear `WF_SAVEU` across the forced repaint, which
`wm_su_ck` tests, so a cache *already banked* is invalidated too. The desktop
dither is drawn directly and is honest either way, which is why a defect in
desktop pixels can be believed from the weaker control and one in window
content cannot.

### What the emulator cannot show at all

Not "shows inaccurately" — cannot show. **Do not call all of these
"flicker"**: they are three defects with three causes and three fixes, and
lumping them together is how one gets fixed and the others ship
([PERFORMANCE.md Part 1](../PERFORMANCE.md) is the full vocabulary).

- **A visible redraw, which is not a flicker at all.** A window's whole
  content, or the whole screen, being painted again. On real hardware you
  *watch it happen* — the fill sweeps, then the text lands row by row — and
  on a heavy application (Paint, the Task Manager, a full Disk window) that
  is **seconds**, not a flash. Under QEMU it is microseconds and a
  screendump taken either side of it is identical. This is the single most
  expensive mistake available in this codebase, and the one an emulator is
  least able to warn about.
- **A double-draw flash.** Anything drawn twice — background, then content.
  The erase-and-letter pair is the canonical case: it leaves a line blank
  between the fill and the last glyph, tens of milliseconds on an XT,
  several display frames. The area is smaller than a full redraw so it reads
  as a flash rather than a wait, but it is still **very plainly visible**, on
  every keystroke. Note Pad's per-keystroke flash (SPEC.md §27.2) and the
  grow box's were both found by a person watching the real machine, and
  neither appears in any timing column, because the two methods take
  comparable *time* and differ in what is on screen during it.
- **Perceived latency and input overrun.** Whether a human can outpace the
  redraw — and start losing keystrokes to a full BIOS buffer — is a property
  of the real machine's speed against a real person's typing. A view that
  costs more than its frame budget reads as a *hung* display rather than a
  slow one, which is why the tracker stops animating its grid on a tier-0
  machine (SPEC.md §45.9.1).

And one the emulator reports as a **success**, which is worse:

- **An optimisation that kept its shape and lost its substance.**
  `gfx_blit4`'s first version emitted one call per run exactly as designed,
  and decoded every pixel individually inside the scan — 75–90 clocks a pixel
  against `repe scasb`'s seven and a half, both written down rather than
  measured. **Under QEMU it measured as exactly as fast**, because QEMU models
  no 8086 timing: every screendump was right and every test passed. That is
  why the cycle counts in `kernel/vga12.inc` are written down rather than
  measured, and why rewriting something whose *reason* is speed means
  verifying the reason survived, not the structure.

  This entry used to quantify it as "a 448×280 repaint went from about a
  quarter of a second to over two". Those two figures came from the same two
  written-down cycle counts and were never measured; the primitive has since
  been priced on the target machine, and it costs **`runs × 0.5 ms`** — so
  what a blit costs is decided by how *flat* the art is, not how big it is
  (SPEC.md §5.4, PERFORMANCE.md Part 9). The lesson above survives the
  correction intact. The numbers did not.

**And one an emulator here cannot be configured into at all: a mono-primary
two-card 5150.** The field machine boots on its Hercules because SW1-5/6 say
`11b` = 80×25 mono and §39.1's last rung is `int 11h` bits 5:4. MartyPC's
two-card machines report bits 5:4 = **`0x20`** (colour) and boot os8088 on the
CGA — and that is not fixable from the machine config: measured, listing the
MDA first changes nothing about the equipment word, because MartyPC derives
the 5150's display switches from whether a CGA is present at all rather than
from card order, and exposes no DIP override. (Listing it first only moves
*MartyPC's* primary to the MDA, which os8088 then does not drive, so the boot
gate watches a blank card and `launch` times out. `os8088_5150_herc_gla`
reads `0x30` only because it has no CGA in it.)

**That has since been fixed rather than lived with**, and the paragraph above
is kept because it is the reasoning: `tools/martypc/patches/03-video-dip-config.patch`
adds an optional `video_dip` to MartyPC's machine config, so SW1-5/6 can be
*set* instead of derived from the card list. **`os8088_5150_both_gla_mono`**
is the machine that uses it — both cards, switches mono, os8088 running
Hercules with `avail = 0x06` — and it is the only one that reaches SPEC.md
§39.11.1's `vid_cga_alias`, the routine whose bug survived because "the
direction the old emulator could reproduce was CGA-primary, where the routine
never runs". A dual-display change can be exercised here now; it is still
worth a field run, but it is no longer *only* answerable there.
docs/MARTYPC-DEBUG.md has the patch's reasoning and the one trap in the
machine (its MDA is listed first, or the boot gate watches the wrong card).

And one more that is not about time at all, and is the newest:

- **A status line from hardware that is NOT THERE.** SPEC.md §18.97's floppy
  probe decides whether drive B exists by reading TRK0 out of the uPD765 —
  ST3 bit 4, and ST0's Equipment Check after a recalibrate. **MartyPC
  answers both for a drive its own config does not have.** Measured: on
  `os8088_5150_cga_1fd`, a one-drive machine, a forced probe of unit 1 reads
  `ST3 = 0x79` — unit 1, ready, two-sided, **TRK0 set** — and a forced
  recalibrate reads `ST0 = 0x29`, IC = 00, normal termination, seek end, no
  EC. Both are the answers a *present* drive gives. The FDC synthesizes drive
  status rather than modelling an unpopulated select line floating to
  inactive, so **no emulator here can ever produce the absent verdict**, and
  the removal path is field-only.

  This is the safe direction and that is the only reason it is a note rather
  than a defect: the probe fails towards *keep*, so every emulator sees the
  pre-§18.97 behaviour exactly. What it means for testing is that **the two
  paths split** — everything except the EC branch is verifiable here (below),
  and the EC branch itself is verifiable only on the 5150 whose SW1 claims a
  drive it does not have (docs/FIELD-MACHINES.md).

  **QEMU is the same wall from the other side, and it is worth writing down
  so nobody re-derives it**: `fdctrl_handle_sense_drive_status` answers
  `0x28 | (track == 0 ? 0x10 : 0) | unit`, off a `track` that is 0 for a
  drive that is not there — measured, `ST3 = 0x39`, `probe stop 01`. Both
  emulators say *present* unconditionally, for different reasons.

  **`make FDDABSENT=1` is what splits the two halves of that** (SPEC.md
  §18.97.2). It forces the verdict to absent for unit 1 with no port touched,
  filling §57.5's block with the field 5150's own bytes (`ST3 = 21` twice,
  `ST0 = 71`, `probe stop 03`) — so the **decision** on top of the probe is
  testable on any machine here, while the **conversation** stays the 5150's
  question. That decision is what §18.97.2 changed and it needs both tiers:

  ```sh
  # tier 0 must RETIRE, tier 1+ must KEEP - one binary, two CPUs
  make FDDABSENT=1
  python3 - <<'PY'
  import sys, os; sys.path.insert(0, "tools")
  os.environ["OS88_DEFINES"] = "FDD_FORCE_ABSENT"
  import os88marty as M
  with M.launch("build/os8088-360.img", apps="build/apps360.img",
                machine="os8088_5150_cga_gla") as m:
      M.settle(m)
      print("tier", m.read(m.sym("cpu_tier", ("FDD_FORCE_ABSENT",)), 1)[0],
            "row1", m.read(m.sym("dsk_vtab", ("FDD_FORCE_ABSENT",)) + 16, 3))
  PY
  # -> tier 0, row1 (0xFF, 1, 0): retired, desktop shows A: alone
  make test FDDABSENT=1   # QEMU is tier 2
  # -> row1 (0x00, 1, 1): kept, desktop shows A: and B:
  ```

  Measured on both: `probe stop 03` either way, `verdict` 0 on tier 0 and 1
  on tier 1+, and the shipped (no-knob) kernel is **0 differing framebuffer
  bytes of 384,000** against the build before §18.97.2 — the new branch sits
  behind a `jnz` no emulator reaches.

**Testing §18.97, then, is three runs and a report:**

```
# 1. a two-drive machine must be UNTOUCHED - the byte-identity check
#    (0 differing pixels of 128,000 on CGA and 250,560 on Hercules)
make && cp build/os8088-360.img /tmp/a.img
make FDDPROBE=0 && cp build/os8088-360.img /tmp/b.img && make
#    ...boot each on os8088_5150_cga_gla / _herc_gla and diff m.vram()

# 2. a ONE-drive machine must not probe at all: os8088_5150_cga_1fd reads
#    eqp=01 ran=00, and dsk_vtab row 1 stays DVK_BIOS with its zone off -
#    which is the pre-existing behaviour for an unclaimed drive
python3 tools/os88marty.py 127.0.0.1:9001 ...   # read the 'FD' block

# 3. the mechanism, with the count gate forced in a SCRATCH kernel: the
#    recalibrate path must complete and decode, not hang the boot
```

Run 3 is the one worth spelling out, because it is the only way to reach
`fdd_wseek` and the ST0 decode here at all: temporarily make `desk_init`'s
`cmp ah, 2` a `cmp ah, 0` so the probe always runs, and `dsk_fdd_probe`'s
`test al, 0x10` a `test al, 0x00` so step 1 always falls through. Rebuild,
boot the one-drive machine, and the block must read `step=02` (`FDD_S_SEEKOK`)
with a plausible ST0 — **not** a hang, and not `step=05` (`FDD_S_NOSEEK`).
Revert both.

For all of these, the emulator's role is to prove *correctness* before you
burn a floppy. The judgement is made on hardware.

**Wall clock here is still a lower bound worth having.** Paint's figures
under `make run-640` — a full-canvas flood fill in ~4 s, a 448×280 4bpp BMP
open in ~8 s — are useful precisely because they are already slow *in the
emulator*. A real 8 MHz machine is several times slower and a 4.77 MHz 8088
slower again, so anything measured in seconds here is out of reach on the
target. That is how JPEG was ruled out (docs/PAINT-NOTES.md), and the
AT-class 86Box targets (`make 286`, `make 386sx`, `make 386`) are the honest
middle of that range.

## MartyPC — the first thing to reach for

> **Before you script a single click: use `tools/os88mouse.py`, not
> `os88marty.py mouse`.** The latter is *relative* — a real Microsoft packet
> through the real UART, which is what makes it worth having — so aiming at a
> control means dead reckoning from the kernel's edge clamp, and that drifts:
> a packet is a signed byte per axis, the UART is 1200 baud so anything under
> ~25 ms apart can be dropped, and the clamp eats overshoot without saying so.
> **The failure looks exactly like a broken feature**: the click lands three
> pixels off a 16px control, nothing happens, and the session reports a bug
> that is not there. `os88mouse.py` reads the live cursor out of the debug
> registry (SPEC.md §9.4.3), sends the exact remaining delta and re-reads
> until it agrees — pixel-exact in ~0.8 s, and it *fails loudly* when it
> cannot converge.
>
> ```sh
> python3 tools/os88mouse.py 127.0.0.1:9001 where
> python3 tools/os88mouse.py 127.0.0.1:9001 click 445 153
> python3 tools/os88mouse.py 127.0.0.1:9001 dblclick 150 90    # NOT two clicks
> python3 tools/os88mouse.py 127.0.0.1:9001 menu 12 8 40 45    # NOT click
> python3 tools/os88mouse.py 127.0.0.1:9001 drag 200 78 200 120
> ```
>
> `menu` is its own verb because a menu **cannot** be opened with a click —
> `menu_track` draws the pull-down and then polls a level, so press-and-release
> in place opens and closes it in one breath.
>
> **`dblclick` is its own verb for the same kind of reason, and it is the one
> that has cost the most time.** Two `click`s are not a double-click: `click`
> ends in a 1.5 s settle and every detector in the system (SPEC.md §22/§26/§38
> and `ui_tdbl`'s title bar) compares the two presses' BIRTH TICKS against a
> **9-tick** window. Take the sleep out and the opposite trap closes: packets
> sent faster than the 1200-baud UART can carry them are **dropped**, so the
> guest decodes one press instead of two. Either way the guest sees a single
> click — a file row *selects* instead of launching, a title bar *drags*
> instead of zooming — and nothing anywhere says so.
>
> So `dblclick` proves all four button edges against the published `mouse_btn`
> (the same discipline `to` applies to the position) and then measures the gap
> between the two presses in the guest's own 18.2 Hz ticks, read from the BIOS
> counter at `0040:006C` — the kernel's clock and the kernel's units, not a
> guess about host timing. It prints the span, and **raises** if an edge never
> arrived or the window was missed. A healthy double-click reads 2–4 ticks.
>
> **One connection at a time.** The debug server accepts a single client, so a
> script that wants both the mouse driver and the framebuffer must share one:
> `Mouse(marty=m)`, never a second `Marty`. Opening a second one does not
> error — it *hangs* until the read times out.

> **And start the emulator with `os88marty.launch`, wait with `settle`, and
> name a kernel flag with `m.sym`.** Every scripted session used to hand-roll
> the same twenty lines, and essentially all of this harness's lost time is in
> them.
>
> ```python
> with os88marty.launch("build/os8088-360.img", apps="build/apps360.img") as m:
>     mo = Mouse(marty=m)
>     mo.dblclick(608, 105)
>     os88marty.settle(m)              # ...instead of time.sleep(4)
> ```
>
> `launch` kills survivors **by PID out of /proc** and waits for the port —
> a survivor keeps 9001, the new emulator cannot bind and says so only in its
> log, and the client then drives the *stale* machine. `pkill -f
> martypc_headless` and `pgrep -f` both match the calling shell's own command
> line, so the first can kill the caller and the second makes `until ! pgrep`
> loop forever. It copies each floppy into the run directory (the guest WRITES
> to a mounted image), asserts `cycles == 0`, and owns the process so nothing
> leaks onto the next session.
>
> `settle(m)` is two identical rendered frames a second apart, which an os8088
> screen only is between events. The **boot** needs a gate on top, and the two
> obvious gates are both wrong: stillness alone returns during the BIOS POST,
> which sits perfectly still for seconds before the floppy is touched
> (measured — an 8.3 s "boot" showing a quarter of the desktop's lit pixels),
> and "has the card left text mode" hangs the full timeout on Hercules, whose
> MDA reports text mode in every mode. The gate is the **desktop** — the menu
> bar's white field, the 1px black rule under it and the dock strip, three
> facts from ONE read of the screen, because a gate and a stillness test that
> read it separately can answer about a state that never existed on an
> emulator running the guest faster than real time (docs/MARTYPC-DEBUG.md).
> Read through `vram` on the 1bpp cards and `fbuf` on VGA. CGA 4.6 s,
> Hercules 4.7 s, VGA 7.1 s, against the 26 s fixed sleep it replaces.
>
> `m.sym("fpg_on")` — or `python3 tools/os88sym.py --all` — is where a kernel
> symbol lives. **Never take one from `nasm -l`.** For anything in `.bss` the
> listing's address column *and* its bracketed operand bytes are
> section-relative and fixed up afterwards: `menu_bovr` reads there as `0x0879`
> and is at `0xCBA4`. That is a plausible small number pointing into `.text`,
> so reading a byte from it succeeds and means nothing — two sessions have lost
> time to it, one concluding a feature was broken from a flag that was never
> the flag. `os88sym` uses nasm's `[map]` on a temporary copy of `kernel.asm`
> and asserts byte-identity with `build/kernel.bin`, so a map describing a
> different kernel is an error rather than a wrong answer.

`make marty`, and the whole recipe is docs/MARTYPC-DEBUG.md. What it gives
that neither of the others does:

- **A cycle-accurate 8088 running a real 1982 IBM BIOS**, agreeing with the
  5150 on 45 of 47 `gfxbench` rows (PERFORMANCE.md Set 11). QEMU's guest runs
  at host speed through SeaBIOS; 86Box is period-*correct* rather than
  cycle-accurate.
- **A debugger that costs the guest nothing.** Memory, registers, ports,
  breakpoints, single-step, cycle counts, over a socket, with no driver and no
  UART in the guest. `verify` dumps `KERNEL_SEG` and diffs it against
  `build/kernel.bin` in one command, which is docs/FIELD-MACHINES.md's
  self-validating dump automated.
- **A modelled CGA and MDA/Hercules**, so the SPEC.md §39.1 detection probe
  actually runs — the thing this document called 86Box-only for years.

- **A PC speaker, an OPL2 and a Sound Blaster**, captured to one wav per
  source with `MARTYPC_WAV=`, in the format `tools/sndcheck.py` already
  parses. The SB is ours (`devices/sblaster.rs`, in our patch — upstream has
  the OPL2 and the 8237 but no DSP), and it is a **DSP 2.01**, so os8088's
  driver takes the classic `0x48`+`0x1C` auto-init path rather than QEMU's
  SB16 one. `tests/sbtest` gives the same 2.00 s at 1000.0 Hz on both.

- **The floppy the guest has been writing to, back on the host.**
  `tools/os88flush.py` (docs/MARTYPC-DEBUG.md): `diff` says what changed since
  the mount, `ls -R` and `get` read the volume with no kernel code involved,
  `verify` hands it to `os88disk.py`'s structural fsck. It is the only route
  to os8088's write path that is not also os8088's read path — and it shows
  the hidden and system files (SPEC.md §19.6) that are invisible from inside
  the OS by design. **Its `writes` counter is not a dirty flag**; `dirty()`
  compares content, and the reason is an upstream bug worth knowing about
  (docs/MARTYPC-DEBUG.md).

What it does **not** cover, and where to go instead: 286/386 (86Box), and
**anything with a disk in it** (the 5150, and nothing else) — noting that
"a disk in it" is about **timing**. What the guest *wrote* is checkable here,
per the bullet above; how long it took is not.

**Input is not on that list either, and no guest module was needed for it.**
`os88marty.py key` enters the emulator's keyboard buffer, so the guest sees a
keystroke through the 8255 and int 09h; `mouse` builds a real Microsoft
3-byte packet and clocks it into the serial controller, so `mou_isr` decodes
it. Both drive *more* of the real path than a memory poke would — a poke to
`[mouse_x]` skips the UART, the decoder and SPEC.md §9.5's port contest — and
more than QEMU's `msmouse`, which is not a UART-level device and ignores DTR.

**Screenshots are NOT on that list.** `os88marty.py shot out.png` reads the
framebuffer out of VRAM and decodes SPEC.md §39.3's banked layout — the same
arithmetic `tools/hercshot.py` uses, verified against QEMU's CGA at 60.0% lit
on both. Starting QEMU to look at a screen when MartyPC is already running
costs minutes for something one command answers. CGA and Hercules only: they
are 1bpp so the bytes are the pixels, where mode 12h is four planes behind
the Graphics Controller and not flat-readable at all. **`shot --rendered` is
the route that covers everything** — it asks the card what it rasterised
rather than asking memory what is in it, comes back in colour, and is taken
automatically on VGA. On a CGA desktop the two routes agree on 0 pixels of
128,000, which is a real cross-check and not a convenience.

An example worth copying, because it is the shape of question this is for.
"How many `int 13h` calls does one file load issue?" used to need
`make DISKCNT=1`, SPEC.md §18.94's published counter block, and a test package
on the floppy. It is now an `int` breakpoint on 13h against an **unmodified
shipped kernel** — but the *timing* of those calls still has to come off the
5150, because MartyPC will happily tell you a nine-sector read took 0.27 s.

---

## What 86Box is genuinely for

Narrower than it was, now that MartyPC covers the 8088 probe, the 6845 and
the sound cards: **a machine that is not an 8088** (the 286, 386, 486 and
Pentium targets), a **period bus** under a card rather than a modelled one,
and a second opinion on the video probe. `make xt`,
`xt-640`, `xt-cga`, `xt-hercules`, `xt-multimon`, `xt-sound`, `286`,
`286-sound`, `386sx`, `386`, `386-sound`, `486`, `pentium`, `xt-z`, `386-z`.

`xt-multimon` is the **two-card** XT — a CGA and a Hercules in one box, one
monitor window each, which is what SPEC.md §39.12–§39.19's extended desktop
is for. 86Box takes the second card as `gfxcard_2 = hercules_plus` alongside
`gfxcard = cga` and opens it as "86Box Monitor #2"; that key was established
the same way every other machine setting in this file was, by reading the
config back after an exit. **`hercules_plus` was needed and no longer is** —
a plain `hercules` comes up text-configured, which SPEC.md §39.11.1.1's retry
now gets past; the matrix row above has the history, and it is worth reading
because that difference was blamed on the model for a year and belonged to the
kernel. The failure mode either way is a Control Panel with no Display page
rather than an error. It is a **second instrument** for a machine
MartyPC already has (`os8088_5150_both`, and `tests/dualcheck.py` is the
gate), and the card order matches it, so the two emulators disagree about
nothing. What 86Box adds is a real 6845 pair on a period bus; what it lacks
is `dualcheck.py`'s reach into guest memory, so what it answers is *"does it
look right"* rather than *"is it right"*.

The last pair are the Frotz machines (SPEC.md 61.9) and are the only targets
that put something other than the apps disk in B:. They also cover a drive
geometry nothing else does — `xt-z` gives the XT a 720KB 3.5" DD drive as B:,
because a 360KB disk does not hold a story library and DOS 3.2 supported one
on an XT. 86Box takes it as `fdd_02_type = 35_2dd`, and that was established
the way every other machine setting in this file was: launch a throwaway copy
of the config, `kill -TERM`, read the file back and see what it kept.

The last two are the *fast* end rather than the period end: a 486DX2/66 and a
Pentium 133, both with an SB16. 8086 real-mode code runs on them verbatim, so
what they answer is whether the constants sized against a 4.77 MHz 8088 still
behave two orders of magnitude up — typematic deadlines, the tracker's ring
refill, Arkanoid's frame pacing. Neither MartyPC nor QEMU can answer that:
one is a cycle-accurate 4.77 MHz 8088 by construction, and the other does not
model a clock speed at all.

It is not installed in the web container and needs BIOS ROMs, so those
targets do not run there. Nothing above them does.

Three 86Box-specific traps worth knowing before blaming the OS: it silently
clamps `mem_size` to the machine's maximum; a `wp://` prefix on an
`fdd_0N_fn` path mounts that floppy write-protected — which the OS then
faithfully reports as "Write protected", and which means `SYSTEM.CFG`
settings do not survive a reboot. No shipped profile carries it on either
floppy now, and every `make` target that launches 86Box strips it from both
keys first, because 86Box rewrites its own config on exit and has twice put
it back. The price is the one QEMU already charges: a session that changes a
Control Panel setting changes `build/os8088.img` and the change persists, so
rebuild it before a run whose starting state matters.

And an unrecognised `cpu_family` is **silently replaced** rather than
rejected, at that family's *default* speed. `cpu_family = pentium` is not a
name 86Box knows: it boots a P54C at 75MHz while the config still says 133.
The cheap check for any candidate machine or CPU is the one in CLAUDE.md —
launch 86Box on a throwaway copy of the config, `kill -TERM` it, and read the
file back, because 86Box rewrites it on exit with whatever it actually
accepted.
