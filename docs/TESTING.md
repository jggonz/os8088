# What can actually be tested, and where

> **Writing the test itself is docs/WRITING-TESTS.md** — the registry row, a
> `secs` you measured, `wants=` instead of `builds=True`, `os88ui` instead of
> a remembered coordinate, and the guest's clock instead of `time.sleep`.
> **Driving MartyPC is docs/MARTYPC-DEBUG.md** — `launch`, `settle`, `sym`,
> `os88ui`, the two mouse drivers, several instances at once, the machine
> list. This file answers *can this be tested here, and with what*.

**DEVELOP ON MARTYPC. QEMU IS A FALLBACK WITH A SHORT LIST.** If what you
are testing runs on an 8088 — the whole of this OS bar the 286/386 targets —
`make marty` gives a cycle-accurate 4.77MHz 8088 running a period BIOS, with
a debugger that costs the guest nothing. It covers all three of SPEC.md §39's
adapters, scripted input, screenshots and sound, and its floppy now turns
(PERFORMANCE.md Sets 35/37).

**Here is the whole of QEMU's list**, stated as a list so that "a legitimate
need" is something you check rather than argue yourself into:

1. **286 and 386** — including anything that needs memory above 1MB
   (`XMEM.DRV`, `tests/xmcheck.py`, `tests/msegxms.py`, `tests/heapmap.py`)
   or a surface only a faster machine draws (`tests/trkscrl.py`, SPEC.md
   §45.9.1). 86Box covers these too and models the machine rather than the
   CPU — prefer it where the question is about the machine and a person is
   watching.
2. **Rung 1 of the hard-disk driver** (SPEC.md §52.1) — the IDE task file
   read directly, gated on `CPU_286` because an 8088's `in ax, dx` is two
   8-bit bus cycles at one port. QEMU has an ATA disk at 1F0h and a CPU that
   clears the gate. Rung 0 (an option ROM answering `int 13h`) is MartyPC's:
   `os8088_xt_hdd` carries an XT-IDE ROM, and it is the rung the field
   machine uses.
3. **SPEC.md §9.5's awkward mouse cases** — a mouse on COM2, the cross-wired
   IRQ4 card, and a modem chattering on the other port (`MOUSEPORT=com2`,
   `com2irq4`, a socket chardev). `tools/zharness.py`'s COM4 teletype is the
   same socket chardev, which is why the Frotz harness is QEMU's.
4. **The PS/2 mouse** (SPEC.md §9.9). An IBM PC/XT has an 8255 PPI, not an
   8042 with an auxiliary port, so `mou_p2_init` on MartyPC reads
   `[cpu_tier]`, finds `CPU_8086` and returns — correct, and a test of
   nothing. `tests/ps2mouse.py` is the row; `make test MOUSEPORT=ps2` the
   recipe.
5. **The Ethernet card and the TCP/IP stack** (SPEC.md §72). MartyPC has no
   NIC of any kind, so `ETHER.DRV` cannot be hosted on it: QEMU's `ne2k_isa`
   on `-netdev user` is the only harness (`make ethertest`,
   `tests/ethernet.py`). Every assertion over it is about behaviour, none
   about speed.
6. **The RTC ladder's WRITE half** (SPEC.md §37.90, §37.94). An IBM PC has no
   real-time clock — the MC146818 arrived with the AT — and MartyPC models no
   XT clock card, so `clk_probe` rejects every rung there and `[clk_tier]` is
   0. QEMU has an MC146818 and nothing else, so rung 1 wins. The assertion is
   a **reboot** (recipe below). Rungs 2 and 3 are in no emulator here.
7. **The VMware absolute pointer** (SPEC.md §9.11, §9.11.7). Nothing on
   MartyPC decodes port `0x5658`; QEMU's `pc` machine carries `vmport` and
   `vmmouse` by default. `make vmmousetest && python3 tests/vmmouse.py`, on
   `kern_emu` out of `build/emuk/` — the shipped kernel has no resident half
   to turn on.

That is the list. **"It is quicker to type" is not on it, and neither is
"I already know the QMP commands."** An eighth entry goes here, not into a
row's docstring. Entries 4–7 share the shape that gets on the list easily:
MartyPC does not have the hardware at all. Entries 1–3 are the ones to argue
with.

Two rules bind every QEMU row. **It kills what it launched** — `make test`
daemonises QEMU and the process outlives the script, so `tests/os88qemu.py`
is the teardown every launcher owes and `tests/unit/t_qemuown.py` checks it is
used. And **`tests/dispcp.py` drives QEMU rows too**, without `m.sym` or a
cycle counter, so its waits there are host-clock loops (docs/plans/HANDOFF-SOAK-FINDINGS.md B5).

| reach for | when | why |
|---|---|---|
| **MartyPC** | **the default** — any 8088 machine, any of the three adapters, any question of the form *what is the machine doing* | cycle-accurate CPU, a real BIOS ROM, modelled CGA/Hercules/VGA, and a debugger that perturbs nothing |
| **QEMU** | the seven-item list above, and nothing else | it counts work exactly and cannot time it; SeaBIOS, no CGA, no Hercules |
| **86Box** | a machine that is **not an 8088**, real sound cards on a period bus, a second opinion on the video probe — **and only where a person is watching**: no debugger and no automation socket | period-correct whole machines, the widest hardware library |
| **the 5150** | anything with a **disk TIMING** in it, and the three defects no emulator shows | docs/FIELD-MACHINES.md |

QEMU is the emulator furthest from the target — host speed, a CPU that is not
an 8088, SeaBIOS, no CGA or Hercules card — and it is wrong in the flattering
direction: SPEC.md §18.91's `AL` bug ran correctly and quickly there while the
real machine moved 4.6x the sectors asked for.

## The one rule that outranks the table: a disk number lands on the 5150

MartyPC's floppy has rotation, an MFM data rate, a per-cylinder seek and an
interleave (`tools/martypc/patches/04-floppy-disk-timing.patch`), and
`tests/sysbench`'s raw `int 13h` block matches the field machine's report
off the same image to one measurement quantum on nine of thirteen rows (Set
37). So MartyPC is worth **asking**; it is still not where a figure **lands**
— anything going into PERFORMANCE.md Part 9 comes off the 5150.

Correctness moved further than timing. MartyPC runs the real IBM ROM when
the ROM is present, so what the ROM does is reproduced (§18.91 shows there as
893 boot ticks against 188). What a real 765 puts in ST1, whether a real
drive returns short, and interrupt stack depth (SPEC.md §8) are still the
5150's questions.

**A disk TIMING comes off an IBM-ROM machine and no other class** (Set 38):
GLaBIOS turns an `int 13h` around 1.61x faster than the 1982 ROM. Counts are
fine on any machine; a timing is not. docs/MARTYPC-DEBUG.md's *Which of them
a DISK number may come off* is the per-machine table.

### Which ROM did it actually load? Fingerprint it, never infer it

The IBM 5150 ROM cannot be in this tree (CONTRIBUTING.md 6). Eleven machines
in `tools/martypc/configs/os8088_machines.toml` ask for
`rom_set = "ibm5150_82_v4"`, and without
`tools/martypc/roms/BIOS_IBM5150_27OCT82_1501476_U33.BIN` none of them runs:
`martypc_headless` exits **rc=1** before the guest starts, and `launch`
raises naming the log. The failure is loud; what was silent was the suite
choosing GLaBIOS machines and calling them IBM ones.

So the policy is the twin. `os88marty.machine("os8088_5150_cga")` resolves to
`os8088_5150_cga_gla` **always**, not only when the ROM is missing — a row
must behave the same on every box. Four twins exist (`_cga`, `_herc`, `_both`,
`_sb`), each differing from its original in `rom_set` alone;
`tests/unit/t_machines.py` fails a row that names an IBM machine directly and
a twin that has drifted. A row that genuinely needs the period ROM says so in
`why_ibm=` (a sentence, in the source) and raises when the ROM is absent
rather than running on the twin. `os88ui.boot()` takes the same argument.

Read the ROM regardless of what the config says:

```python
with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                      machine=os88marty.machine("os8088_5150_cga")) as m:
    print(os88marty.rom_banner(m))       # 'GLaBIOS [.] .Reb' or '501476 COPR. IBM'
    print(bytes(m.read(0xFFFF5, 8)))     # the reset-vector date: b'07/17/25' / b'10/27/82'
```

`os88marty.assert_rom(m, ibm)` is the same read as an assertion.
`tests/int0sweep.py` is the row that wants the IBM ROM: there INT 0 is the
BIOS stub that masks the whole 8259, so one divide overflow is a dead machine
where GLaBIOS gives a wrong clip index and carries on.

---

## The regression suite: three tiers and a budget

**`tools/os88test.py` runs the tests; `tests/suite.py` is the list of them.**

```
python3 tools/os88test.py fast      # a commit you keep. 48 rows, ~13s, host-side
python3 tools/os88test.py full      # major work reaching the integration branch. ~4 min
python3 tools/os88test.py soak      # everything. No budget - and rarely what you want
python3 tools/os88test.py --list    # what is registered, and why
python3 tools/os88test.py soak -k 'disp*'   # just the ones about displays
```

`make` runs the `fast` tier itself, as a prerequisite of `all`; `make
test-full` and `make test-soak` are the other two. **When each of them is
worth running is §"When to run which tier" below** — the two that cost real
time are not per-commit gates, and running them as though they were is where
this suite's time has actually gone.

**RUNNING THE WHOLE SOAK IS `tools/os88soak.py`, NOT `make test-soak`** —
that target runs the same rows serially:

```
python3 tools/os88soak.py check     # can this box answer? what would SKIP?
python3 tools/os88soak.py start     # preflight, then run detached
python3 tools/os88soak.py status    # reads a file - SAFE to poll
python3 tools/os88soak.py stop
```

`check` is worth typing on its own: a capability the box has not got makes a
row skip, **and a skip is the box declining to answer, not a pass**. It names
every gap, the rows it would silence, and the command that fixes each. The
width is CORES-1 so a `status` poll has a core to run on; `start --resume`
after a reclaimed container re-runs only what did not finish. Before blaming
a failure on contention read docs/plans/SOAK-PARALLEL.md §1: load does not
make a row slow, it makes it less thorough at the same wall time.

### What each tier is for

| tier | budget | what it does | when |
|---|---|---|---|
| `fast` | **30s** (uses ~9) | Host-side only, 25 rows. Reads what `make` just built and checks what breaks SILENTLY — and only what somebody who did NOT touch the subject can break. | A commit you are going to keep |
| `full` | **3 min** (uses ~1¼) | One question: *did you obviously break the OS?* Boots to a desktop on both 1bpp adapters and on VGA, builds and boots `kern_small` on its 128KB floor machine, checks the mouse and keyboard, and builds a C package. 5 rows. | A major round of work reaching the integration branch |
| `soak` | none | The other 301 gates in `tests/`, one subject each — every per-package and kernel-internal row, the 99-knob build matrix, and everything about the tree or the suite rather than the product. | The end of extensive kernel surgery — or when asked |

**Both gates are deliberately narrow, and docs/WRITING-TESTS.md §2.1 and §2.2
are the rules.** `fast` is the one tier nobody opts into, so a row about ONE
package or about a kernel internal no package can reach is charged to every
contributor who is not working on it. `full` asks only whether the OS is
obviously broken, so a row about one package, about the build matrix, or about
the suite's own instruments does not belong there either. Both kinds live in
`soak`, one `-k` away, run by the person whose change would break them —
including the 99-knob matrix, which means knob `%ifdef` arms now assemble at
soak cadence rather than at every integration merge.

The `when` column is the whole of §"When to run which tier" below, compressed
to fit in a table. **Read that section before running a tier on a schedule of
your own** — running all three at every step is not caution, it is spending
two hours to be told what thirteen seconds already said.

The tiers are cumulative. **The runner FAILS the tier when the wall clock
overruns its budget**, green rows or not: a suite with no ceiling grows until
it is too slow to run. Each row also declares its own `secs` and is reported
when it overruns them, so the row that got slower is named.

### When to run which tier

**This section is the authority on when a tier is run. CLAUDE.md carries the
short form and points here; nothing else in the tree may say otherwise.**

A tier is not a ritual performed at every step — each one answers a different
question, and the question is only asked at one point in the cycle. `full` is
four minutes and the whole soak is nearly two hours; run them where an answer
changes and nowhere else. The general principle underneath all three rows: a
change is covered by the ROW that is about the thing it touched, not by the
tier that contains it. `python3 tools/os88test.py soak -k 'disp*'` after a
redraw change is minutes and is the right answer far more often than any
tier is.

**`fast` — at a commit you intend to keep.**

Run it when you are about to commit to your own branch and the commit could
change a byte under `build/`. For most work that means nothing extra to type:
`all` depends on `test-fast`, so the `make` that produced the artifacts you
are committing has already run it.

It is **not** required for:

* **a build you are not going to commit.** An experiment, an A/B, a knob
  build taken to answer one question — none of them are going anywhere, and
  the tier's rows read the SHIPPED artifacts. `make` already declines to run
  it on a knob build for exactly that reason.
* **a documentation-only commit.** `python3 tools/checkdocs.py` is that
  commit's gate, and it is the one to run — a stale § citation is what such a
  commit can actually break.
* **a commit whose only change is the build number.** That is three bytes of
  `.text` moving because the commit count moved (SPEC.md §14.2). No invariant
  in the tier can see it and none can break on it.

**`full` — when major work first reaches the integration branch.**

Run it:

* the **first time** a piece of major work merges from your feature branch
  onto the integration branch; and
* **again** when you come back to that branch later and land another large
  round of work on it.

That is the placement the tier was built for. A merge combines two trees
nothing has ever run together, and what `full` adds over `fast` is every
configuration a plain `make` does not build at all — the knob kernels,
`kern_small`, the C toolchain, and a boot to a desktop on both 1bpp adapters.
Those are answers about a whole tree, and a large round of work landing is
when the tree last changed enough for the answer to move.

Do **not** run it:

* **on every commit to your own feature branch.** Nothing in a branch's
  fortieth commit makes that answer different from its thirty-ninth, and the
  branch is not what anybody else builds.
* **on a minor bugfix onto the integration branch** — a one-line fix, a
  greying predicate, a string, a comment. Run the row that is about the thing
  you fixed instead.
* **on a documentation-only commit or merge.** `checkdocs.py` is the gate.
* **on a commit that only moves the build number.**

**`soak` — at the end of extensive kernel surgery, or when asked.**

Run the whole soak when:

* you have finished a major piece of **kernel surgery** — the memory ladder,
  the scheduler, the window manager, the disk path, the graphics layer, the
  boot path — and are landing it. **At the END of that work, once**, not at
  each wave inside it; or
* somebody **asks** for it.

Nothing else earns two hours. Mid-way through the surgery the right thing is
a SUBJECT and not the tier: `python3 tools/os88test.py soak -k '<subject>'`
runs the rows about the one thing you touched, in minutes, and is the answer
you actually wanted; `python3 tools/os88test.py --list` names every row and
what it is about, which is how you find the pattern to pass.

The whole tier is `tools/os88soak.py`, never `make test-soak` (above), and a
run that long has two standing obligations: hold a waiting task for its whole
life, and report while it is in flight.

### Why `full` is CURATED and not "all of them"

A MartyPC boot to a settled desktop is ~7.5 seconds and an emulator row is
40–75 seconds, so three minutes is about four of them at `--marty-jobs 1`.
The default is 1 for arithmetic: N instances on an N-core box is the ceiling,
and past it every row takes longer in HOST seconds, which is what `secs`,
timeouts and `settle`'s patience are measured in (four rows: 175.6s at 1,
85.9s at 3). What still runs alone is a row marked `builds=True`, which
shells out to `make` and rewrites `build/` under anything reading it —
`tests/unit/t_registry.py` checks that flag against the script.

**What earns a `full` row is breadth per second.** `tests/bootsmoke.py` is
the model: one boot exercises the boot sector, FAT12, the `int 13h` splitter,
adapter detection, the heap ladder, `drv_boot` and the first paint, so it
fails for almost any serious regression. A row that can only fail for one
narrow reason belongs in `soak`.

### The host-side tests, and what each is defending

`tests/unit/`, python3 only. Each exists because of a failure this tree had.
The ones to know by name:

| row | what it would have caught |
|---|---|
| `api-abi` | The API table decoded out of `build/kernel.bin` and compared with `apps/os88api.inc`: two branches appending to the same tail merge CLEAN, and the result is two cells pointing at each other's addresses. |
| `mirror` | A constant written down in two files must agree in both — there is no linker to notice. |
| `image` | Every shipped floppy walked by an **independent** FAT12 reader: `KERNEL.SYS` contiguous (the boot sector has no chain walker), a standard BPB (SPEC.md §19.3), §19.6's attributes. |
| `pkg` | Package, driver and module headers, and every file on every image byte-identical to the artifact in `build/` it came from. |
| `diskverify` | The tree's own fsck over every shipped image. |
| `asmrules` | Unreachable code after an unconditional jump, a `cpu 8086` reachable from every root, and a kernel local block nothing jumps to (SPEC.md §18.4.2.1). |
| `registry` | Every test in `tests/` is registered in a tier or says why not. This is the row that stops the suite going back to ninety unlisted scripts. |
| `machines` | No row names a machine whose ROM the tree has not got (above). |

### Adding a test

docs/WRITING-TESTS.md, before the first line. `soak` is a real answer and
costs nobody any budget; the `registry` row fails the build until the row is
registered or exempted with a reason.

## The matrix

The **MartyPC** column is "is this the right tool for it", not "does the
emulator have the hardware": a ✅ means reach for it first.

| Capability | MartyPC | QEMU | How | Verified result |
|---|---|---|---|---|
| VGA 640x480x16 (mode 12h) | ✅ | ✅ | `make test`, or the `os8088_xt_vga` machine | MartyPC rasterises 12h: `vid_w=640 vid_h=480 vid_planes=4`, Minesweeper in 8 palette colours |
| CGA 640x200 mono | ✅ | ✅ | `make test VIDEO=cga` | QEMU dumps 640x400 (line-doubled) |
| Hercules 720x348 mono | ✅ | ✅ | `make test VIDEO=herc HERCSEG=0x7000` | docs/HERCULES-TESTING.md |
| The DOS end of the parallel link (SPEC.md §62) | ✅ | ➖ | `make dosstub`, then `os8088_5150_cga_lpt` | There is no DOS here and none the tree may ship, so `tests/dosstub` is a bootable floppy carrying an int 21h stub and `OS88NET.COM` inside its own image, on a 4.77MHz 8088 with a parallel port at 0x378. `tests/lptlink/partner.py` in its MASTER role drives the program's whole command loop. The stub has **one file handle**, so `NF_COPY`'s body is not exercised; the wire's own verdict is the 5150's |
| Adapter switching (SPEC.md §39.11) | ✅ | ➖ | `os8088_xt_vga` / `_5150_both_gla` / `_5150_herc_gla` / `_5150_cga_gla` | The page lists both cards on the two-card machines and one row on the single-card ones; the live switch works both ways; the choice survives a reboot through `SYSTEM.CFG`'s `VM` key; a disk asking for a card the machine lacks is refused. Hercules → CGA on a two-card machine needs `tools/martypc/patches/02-hercules-page1-decode.patch` — upstream's MDA decodes B8000 too and the CGA's raster stays black — and `tests/dualcheck.py` is what says the patch is still applied. **Hiding the page** (§31.10.1) verified on both single-card machines; **blanking the outgoing card** (§39.11.4) on `_5150_both_gla` in both directions |
| Two displays at once (SPEC.md §39.12–§39.19) | ✅ | ❌ | `tests/dualcheck.py`, or `make xt-multimon` | MartyPC is the instrument because only it reads the guest's answer back; `xt-multimon` (`gfxcard = cga` + `gfxcard_2 = hercules_plus`) is the same pair on a period bus, for looking at. A plain `hercules` there comes up text-configured, which §39.11.1.1's retry now gets past — a real GB101 that has never been written *is* an MDA. The symptom of a lost second card is a Control Panel with **no Display page** (§31.10.1), which announces nothing about why |
| PC speaker | ✅ | ✅ | `make test-snd`, or `MARTYPC_WAV=` | dominant 880.0 Hz (891.0 on MartyPC, inside tolerance) |
| AdLib / OPL2 | ✅ | ✅ | `make test-snd ADLIB=1`, or `os8088_5150_sb_gla` | dominant 880.0 Hz from a keyed 440 |
| Sound Blaster (DMA streams) | ✅ | ✅ | `make test-snd SB16=1 TESTAPPS=build/sbtest.img`, or `os8088_5150_sb_gla` | 2.00 s at 1000.0 Hz on both. MartyPC's is a DSP **2.01** (`0x48`+`0x1C` auto-init); QEMU's an SB16; `dsp_version` picks |
| Boot sound probe (SPEC.md §51.3.1) | ✅ | ⚠️ | `os8088_5150_sb_gla` / `_sbonly` / `_cga_gla`, fresh image | MartyPC's OPL2 answers the timer-flag dance; QEMU's `-device sb16` has an OPL *stub* that does not, so an SB16-only box reads as cardless there. `_sb` → row 0 `WANT` 1 and the Sound page on **Sound Blaster**; `_cga` → `WANT` 0; `_sbonly` → 0 by default, 1 under `SNDSNIFF=sb` |
| Scripted mouse / keys | ✅ | ✅ | `tools/os88mouse.py click X Y` (absolute), `tools/os88mouserel.py` (relative), `os88marty.py key`; `tools/mouse.py` on QEMU | MartyPC drives the REAL devices: a Microsoft packet through the UART, a keystroke through int 09h |
| **Screenshots** | ✅ | ✅ | `os88marty.py shot [--rendered]`, or `tools/shot.py` / `hercshot.py` | `shot` decodes VRAM on the 1bpp cards; `--rendered` asks the card what it rasterised and is automatic on VGA. On a CGA desktop the two routes differ by 0 pixels of 128,000 |
| Mouse on COM2 (SPEC.md §9.5) | ➖ | ✅ | `make test MOUSEPORT=com2` | both UARTs probe present, COM2 wins, COM1 retired |
| A **cross-wired IRQ** (SPEC.md §9.5.2) | ➖ | ✅ | `make test MOUSEPORT=com2irq4` | the Compaq Portable III: mouse at 2F8 driving IRQ4 |
| A **modem** on the other port | ➖ | ✅ | a socket chardev at 3F8 — see below | eight result codes claim nothing, move nothing, click nothing |
| **A PS/2 mouse** (SPEC.md §9.9) | ➖ | ✅ | `make test MOUSEPORT=ps2`; `tests/ps2mouse.py` | see below |
| **The VMware absolute pointer** (SPEC.md §9.11, §9.11.7) | ➖ | ✅ | `make vmmousetest && python3 tests/vmmouse.py`; interactively `make emu`, then `make run VMPORT=on` | **Every recipe that DRIVES the serial mouse runs `vmport=off`** (`$(QEMUMACH)` in the Makefile; `tests/ps2mouse.py`, `heapmap.py`, `vgadirty.py` in their own launch strings): left on beside `msserial` the backdoor wins the contest, QEMU splits coordinates from buttons across the two devices, and a drag never ends. A recipe that only boots a stock image need not care: no shipped disk carries `VMMOUSE.DRV` and the shipped kernel has no code that could name it. `tests/vmmouse.py` sets `$OS88_BUILD`/`$OS88_DEFINES` so `os88sym` resolves against `build/emuk/` |
| **The hardware clock** (SPEC.md §37.90/§37.94) | ➖ | ✅ | `make test`, drive the Date/Time page, `system_reset` — see below | `Hardware clock: AT 70h`, year stepped, panel closed, reset, and the bar still shows the new year |
| Performance benchmarks | ✅ | ✅ | `make bench` (from `tests/`, not in `all`) | numbers are always in flux — see below |
| **Flicker** — the double-draw flash | ✅ | ❌ | `os88marty.py flicker` (PERFORMANCE.md Part 3.1) | one sample per displayed frame, on all three adapters |
| Fullscreen exclusive (SPEC.md §53) | ➖ | ✅ | `make test TESTAPPS=build/fsxtest.img` | every FSXM mode the adapter owns sets, draws and restores; the desktop below the bar is byte-identical after a sweep |
| ...**which MONITOR it lands on** (SPEC.md §53.7.1) | ✅ | ❌ | `python3 tests/dispfsx.py [--app paint] [--far] [--noxt]` on `os8088_xt_vga_herc` | two assertions: while the bracket is UP its own display changes a lot and the other not at all; AFTER the round trip both cards equal a forced full repaint. The second alone passes on a broken kernel, because §53.6's exit `wm_paint_all` repaints the world. `--machine os8088_5150_cga_gla` is the one-card regression leg |
| Does an INCREMENTAL redraw agree with a full repaint? | ✅ | ❌ | `python3 tests/dispcorner.py [--only a\|b] [--under hello] [--dest seam\|near\|far] [--mode right\|below] [--single] [--selftest]` | do the thing, capture, force a repaint (poke `[cp_dirty]` with `WF_SAVEU` cleared, read the flag back — see "Prefer a self-checking harness"), diff. Leg C (SPEC.md §11.96.13.1) classifies a difference rather than counting it: `dither_split` excuses only a screen-phased dither replayed a row off, and `--selftest` proves the classifier on synthetic captures with no emulator. It prints WHERE and crops a PNG of both captures |
| **What the guest WROTE to a floppy** | ✅ | ⚠️ | `tools/os88flush.py <addr> diff 0` (docs/MARTYPC-DEBUG.md); on QEMU the mounted `.img` is written in place, so `os88disk.py --verify` it after `quit` | the only route to os8088's write path that is not os8088's read path. QEMU's ⚠️: writeback is all-or-nothing at exit, so no mid-session snapshot |
| Boot-sector relocation (SPEC.md §2.7) | ✅ | ✅ | `tests/bootfloor.py`; `make test RAMKB=<n>` — see below | both sides of the floor, on a machine |
| A machine that reports a **small** `int 12h` to the KERNEL | ✅ | ❌ | MartyPC `conventional.size` (`os8088_5150_gla_128k`, `_192k`, `_cga_gla_256k`), or 86Box `mem_size` | `RAMKB=` moves the sector only; the heap still sees the real answer. `tests/small128.py` is the 128KB row |
| Video **detection probe** | ✅ | ❌ | `make marty`, or `make xt-cga` / `xt-hercules` | MartyPC has a modelled CGA and MDA/Hercules, so the probe genuinely runs |
| 6845 programming | ✅ | ❌ | `make marty`, or `make xt-hercules` | MartyPC's `screen` reads the result back without a screenshot |
| Period-correct **CPU** timing | ✅ | ❌ | `make marty` (8088 only), or `make xt` / `286` / `386` | MartyPC agrees with the 5150 on 45 of 47 gfxbench rows. **Not the disk** — see the rule above |

`VIDEO=` forces a code path; it does not exercise the probe that would have
chosen it. QEMU emulates no CGA and no Hercules card, so what is untestable
there is the *choosing*, not the *drawing*.

---

## How much RAM the machine says it has

`boot/boot.asm` relocates itself to the top of conventional memory (SPEC.md
§2.7), found with `int 12h`. **SeaBIOS answers 639 whatever `-m` says**, so
the arithmetic and the refusal below the floor are unreachable on QEMU by
configuration. `RAMKB=<n>` assembles the sector to believe a number:

```sh
make test RAMKB=196         # where a MIN_RAM_KB machine puts it (196 on kern_big, 128 on kern_small)
make test RAMKB=64          # below the floor: must print RAM and stop
python3 tools/qmp.py build/qmp.sock 'xp /4xb 0x600'   # 00 00 00 00 = never loaded
```

Where it landed is a memory dump, not a screenshot: the sector's last two
bytes are `0xAA55`, so on a machine of *n* KB they are at linear
`n*1024 - 2`, and its first three are `EB 3C 90`.

- **`RAMKB` shares the knob stamp** and needs to: it touches neither
  `boot.asm` nor `kernel.bin`, so without the stamp `make` rebuilds nothing
  and boots the previous relocation.
- **It moves the sector and nothing else.** The kernel still asks the real
  `int 12h` for its heap; a small machine is a MartyPC `conventional.size`.
- **The boundary is arithmetic, so test it at the boundary.**
  `tests/bootfloor.py` builds both sides of the floor and boots them; the
  floor moves whenever the kernel's size does.

---

## Video

CGA on QEMU works because SeaVGABIOS's `int 10h AX=0006h` is a byte-exact CGA
framebuffer, so `screendump` shows it — at **640x400**, line-doubled, so a
crop's Y and H are twice the kernel's own. VGA is 1:1.

```sh
make test VIDEO=cga
python3 tools/shot.py build/qmp.sock /tmp/cga.png
python3 tools/mouse.py --screen 640x200 build/qmp.sock click X Y
```

Hercules needs its framebuffer relocated into spare RAM (B0000 is unmapped
under QEMU and swallows every write) and is **never** screendumpable — that
RAM is memory the VGA device has never heard of, so `screendump` returns a
black or stale VGA screen and does not error.

```sh
make test VIDEO=herc HERCSEG=0x7000
python3 tools/hercshot.py build/qmp.sock 0x70000 /tmp/herc.png   # LINEAR = HERCSEG*16
python3 tools/mouse.py --screen 720x348 build/qmp.sock click X Y
```

`HERCSEG` is a segment and `hercshot` takes the linear address. The full
recipe and its four traps: docs/HERCULES-TESTING.md.

**Every knob is tracked by a stamp file**, so a knob-built kernel is a
different kernel and changing the knob rebuilds it, its carved modules
(`ctrl.drv`, `format.drv`) and the boot sectors. A forced kernel stays on your
disk images until something rebuilds them; a release is built knob-free
(`rm -f build/os8088*.img && make`). Two guards that do not lie: look for the
knob's own bytes in the artifact rather than trusting `kernsize` (which
re-assembles with the knob and reports a binary the build may not have
produced), and `os88sym.py`, which asserts byte-identity with
`build/kernel.bin` and **refuses** rather than answering — pass the knob with
`OS88_DEFINES=<define>` (comma or space separated) when driving a knob-built
kernel, and `OS88_BUILD=<dir>` for a tree `tools/os88build.py` made.

---

## The mouse's port, and the modem on the other one (SPEC.md §9.5)

`make test MOUSEPORT=com2` gives QEMU a **live but silent** UART at 3F8 and
the mouse at 2F8 — leaving 3F8 unpopulated would test the single-port path
and none of the contest. Read the answer out of the kernel with
`tools/os88sym.py` and `xp`: `mou_bases` `03f8 02f8` (both present),
`mou_need` 8 (a contest), `mou_port` 2 with `mou_seen` 1, `mou_hpst` 2
(`mou_lockon` retired COM1).

All four combinations are knobs, through `-device isa-serial,iobase=,irq=`
(`-serial` cannot set an IRQ):

| | mouse | and the other port |
|---|---|---|
| `make test` | 3F8, IRQ4 | nothing |
| `make test MOUSEPORT=com2` | 2F8, IRQ3 | live but silent at 3F8 |
| `make test MOUSEPORT=com2irq4` | **2F8, IRQ4 — the Compaq Portable III** | live but silent at 3F8 |
| `make test MOUSEPORT=com1irq3` | 3F8, IRQ3 | live but silent at 2F8 |

On a kernel without §9.5.2 the two cross-wired rows never find the mouse:
`[mou_seen]` stays 0 through forty moves. One trap in writing that test: a
movement pattern that **nets to zero** returns the cursor to where it
started and reports a working mouse as broken. Drift in one direction.

### `comscan` — when the mouse is not found on real hardware

`make comscan` builds the field diagnostic (`tests/comscan`): not an os8088
package, because the thing diagnosed is the mouse, so nothing you have to
click can be required. `build/comscan.img` (360KB) and `comscan144.img` are
bootable floppies — the shipped boot sector loads anything at `KERNEL_SEG:0`
that honours its handoff — and `build/comscan.com` is the same program for a
DOS machine (`COMSCAN > COMSCAN.TXT` carries the report off).

It surveys 3F8/2F8/3E8/2E8 — a live UART at 3E8 is a whole bug on its own —
and per port: the BIOS list, os8088's divisor-latch probe with and without a
long settle, scratch register, loopback, part type, a raw dump, then a
1200 7N1 DTR/RTS pulse and a polled capture with the packet machine run over
it. **The measurement that matters is which IRQ line the card drives**: it
hooks int 0Bh/0Ch/0Dh/0Fh and arms RX one port at a time, with a runaway
guard (`IRQGUARD`) and a flush pass against no port first. Reading the 8259's
IRR with the lines masked does not work — a masked request is never
acknowledged, so a line asserted once reads as 1 forever. Two things learned
on hardware and built in: the loopback programs divisor 1 itself, because a
test that leaves the baud rate where the last test left it depends on the
machine (the Compaq's COM1 read as failing loopback for that reason), and
the survey prints the as-found divisor in a `DIV` column.

### The PS/2 mouse (SPEC.md §9.9), and why QEMU is the only instrument

`make test MOUSEPORT=ps2` gives the guest **no serial ports at all**, so both
UART rows are rejected by §9.5's probe and the only pointing device is the
`pc` machine's PS/2 mouse. `tests/ps2mouse.py` is the recipe automated and
`make test-full` runs it. What it asserts, readable by hand with `os88sym`
and `xp`:

| | |
|---|---|
| `mou_bases` | `0000 0000` — the contest cannot be entered |
| `mou_p2st` | **9**. Read this first on any failure: how far the handshake got (SPEC.md §9.4.4) — `2` is "no auxiliary port", `7` "the enable timed out" |
| `mou_p2` / `mou_p2id` | `1` and `00` — live, a plain 3-byte mouse |
| after one `tools/mouse.py to X Y` | `mou_seen` 1, `mou_port` **04** (`MOU_P2ROW`), `mou_line` **FF** (`MOU_P2LINE`), and `mouse_x`/`mouse_y` **exactly** the requested X/Y |

The last row is the one that catches a PS/2 driver's real defect:
`tools/mouse.py` pins against the edge clamp and walks back by exact deltas,
so landing on the pixel is a statement about sign handling and §9.9.3's Y
inversion. A mouse with Y inverted moves, clicks and drags perfectly and goes
the wrong way.

**The keyboard must survive it**: the 8042 has one output buffer for two
devices. Send keys and watch the BIOS buffer head/tail at `0040:001A`
(`xp/2xh 0x41a`, +2 per key) with the mouse live and while `mouse.py move`
runs. And the serial configurations must be unchanged: on the default `make
test` the PS/2 mouse is found and then retired by `mou_lockon` — `mou_p2`
back to 0 with `mou_p2st` left at 9.

**What QEMU cannot see.** Every assertion passes with §9.9.1's bit 4 rule
broken, because QEMU's i8042 raises IRQ1 only for keyboard-sourced bytes. On
a controller whose OBF interrupt is gated by bit 0 alone the same build stalls
at `mou_p2st` 4 or 5 — which is what `make MOUDIAG=1`'s rows on the glass
(§9.9.6) are for, and why `make 386-ps2` exists: the only 86Box machine here
with `mouse_type = ps2`, a Packard Bell Legend 300SX whose bus flags give its
8042 an auxiliary port at all. A working machine is a pointer that moves; a
broken one is the keyboard mouse (§9.6), arrows driving the pointer because
`[mou_ptr]` is 0 until a packet arrives.

### The RTC ladder (SPEC.md §37.90), and why QEMU is the only instrument

```
make test                                   # QEMU, MC146818 at 70h/71h
python3 tools/mouse.py build/qmp.sock to 8 8
python3 tools/mouse.py build/qmp.sock down
python3 tools/mouse.py build/qmp.sock to 8 40
python3 tools/mouse.py build/qmp.sock up            # chip menu -> Control Panel
python3 tools/mouse.py build/qmp.sock click 200 173 # the Date/Time row
python3 tools/shot.py build/qmp.sock /tmp/p.png --crop 159,131,322,152 --zoom 2
        # ...the caption `Hardware clock: AT 70h` is rung 1 claiming
python3 tools/mouse.py build/qmp.sock click 336 180 # the YEAR field
python3 tools/mouse.py build/qmp.sock click 420 203 # '-', twice
python3 tools/mouse.py build/qmp.sock click 420 203
python3 tools/mouse.py build/qmp.sock click 169 140 # the close box: §37.94's write happens HERE
python3 tools/qmp.py build/qmp.sock 'system_reset'
python3 tools/shot.py build/qmp.sock /tmp/q.png --crop 380,0,260,18 --zoom 2
```

**The reboot is the assertion and there is no shorter one.** The only way to
read an RTC from outside the guest is the guest, so a value that survives
`system_reset` proves `cp_flush_close_x` → `clk_rtc_write` → `clk_at_write`
→ the chip → the next boot's overlay reading it back. Use `system_reset`,
not the System menu's Restart, which reaches `cp_flush_close` again and writes
the chip a second time on the way out. `RTC=ns` on QEMU exercises the
**refusal** of an absent chip and nothing past it. `tests/dtwrite.py` covers
the flag half (`[clk_dirty]` spent by the panel's close) on MartyPC; the chip
half has no registered row and is this recipe.

### Can we boot a real 5150 BIOS instead of SeaBIOS?

**No.** `-bios` maps a file and does not change the hardware under it. QEMU
has no XT-class machine — every one puts an 8042 at 60h/64h where a 5150
POST reads its DIP switches through an 8255 PPI at 60h–63h, so the POST
fails its first configuration read and beeps. `-machine isapc` boots os8088
and is marginally more period-shaped, but the BIOS is still SeaBIOS. The
things SeaBIOS misrepresents — the int 1Eh table it never reads
(docs/FIELD-NOTES.md 5), short `int 13h` reads, interrupt stack usage kept on
an internal stack — are MartyPC's and 86Box's to show.

### A modem on the other port

A Hayes result code is a well-formed Microsoft packet (§9.5.1). QEMU can be
that device: a socket chardev at 3F8 and type at it.

```sh
qemu-system-i386 -drive file=build/os8088.img,format=raw,if=floppy -boot a \
  -chardev socket,id=modem,host=127.0.0.1,port=45881,server=on,wait=off \
  -serial chardev:modem \        # 0x3F8 - the "modem"
  -serial null \                 # 0x2F8 - a live UART, saying nothing
  -display none -qmp unix:build/qmp.sock,server,nowait -daemonize
# then: connect to 45881 and send OK/RING/NO CARRIER/CONNECT, CRLF-wrapped,
# pacing each burst by len*10/1200 seconds - it is a 1200-baud line
```

Two ways to get a green run that proves nothing:

- **`msmouse` speaks during boot**, so with a mouse on 2F8 the contest is
  over before the first byte of chatter. To test the *open* contest there
  must be no mouse anywhere — 3F8 the socket, 2F8 `-serial null` — and
  `[mou_seen]` must stay 0 forever.
- **Assert on more than the port.** A modem that claims nothing can still
  move the cursor and latch a right button. Check `mouse_x`/`mouse_y` and
  `mouse_btn` too.

Then the other way round — the socket at 3F8 and `msmouse` at 2F8 — and the
mouse must still reach its run and chatter afterwards be ignored. On a
two-port machine the first ~8 packets are counted and discarded, so
`tools/mouse.py`'s first `to X Y` after boot leaves the cursor **at 0,0**:
the pin over-drives and lands, the walk is eaten. Call it a second time.

### The identify burst (SPEC.md §9.4.1)

**QEMU tests the half that must refuse; MartyPC the half that must accept.**
`msmouse` ignores MCR/DTR and emits packets during boot, so a plain `make
test` gives `[mou_seen]` = 1, `[mou_hpst]` = 2, `[mou_ident]` = 0 — the old
code path exactly, the right regression baseline and no test of the new one.
MartyPC models the UART and the reply to a DTR/RTS raise, with **two live
ports**, so `[mou_need]` going `01 08` is the assertion there; dump memory
at the desktop with the mouse untouched, `mouse_x`/`mouse_y` still at
`[vid_w]/2, [vid_h]/2`.

The refusal half uses the socket-chardev harness above, with the sender
timed to land inside `mouse_init`'s drain window (~1.2 s after launch;
sweep a single `M` at 0.2/0.6/1.0/1.4 s to re-find it):

```
idn   idb0   ident  idany  need        verdict
01    'M'    01     01     01 .. 08    a mouse: identified, need lowered
02    'M'    01     01     01 .. 08    'M3' likewise
05    'M'    01     01     08 .. 08    identified, but past MOU_IDSTRICT
1f    'O'    00     00     08 .. 08    Hayes codes - rule 2
2e    'M'    00     00     08 .. 08    'M' + a banner - rule 3
08    'M'    00     00     08 .. 08    a trickle that never stops - rule 4
```

Rule 4 is isolated by sending `'M'` every ~150 ms across the whole window,
so a byte lands inside `MOU_IDQUIET` of its close while the count stays at
`MOU_IDMAX`. On a machine with no debugger `sysbench` prints the block
(§9.4.2, registry tag `'MO'`).

**Assert on `[mou_hpst]` too**: 0 means the poller has never dropped DTR at
all (§9.4.8) — one peek, no second reading. If it is non-zero, `[mou_hpt]` read
twice four seconds apart says whether it is *still* cycling: advancing by 58
means the mouse is being power-cycled every 3.19 s.
And check `[mou_idn]` is non-zero before believing a refusal — a sender that
never lands in the window reads exactly like a rule refusing — and that
`[mou_seen]` stayed **0**, since an identify must never settle the contest.

---

## Sound

`make test-snd` is `make test` plus a wav capture at `build/snd.wav`,
finalized when QMP `quit` stops QEMU — **run `tools/sndcheck.py` only after
`quit`**. The capture is stream-on time, not wall time: a silent boot yields
an empty file, which is a pass for `--expect-silence`.

Without `ADLIB=1`/`SB16=1` there is no card and the tone route falls to the
PC speaker:

```sh
make test-snd TESTAPPS=build/fmtest.img
# launch FMTEST, then:
python3 tools/qmp.py build/qmp.sock 'sendkey b' 'sleep 2' 'quit'
python3 tools/sndcheck.py build/snd.wav 880          # -> dominant 880.0 Hz
```

The two gate packages, each on its own scratch image:

```sh
# AdLib: click once. The patch sets carrier MULT=2, so a keyed 440 must SOUND
# at 880 - the doubling only holds if the patch bytes reached the operators.
make test-snd ADLIB=1 TESTAPPS=build/fmtest.img
python3 tools/sndcheck.py build/snd.wav 880          # -> dominant 880.0 Hz

# Sound Blaster: click once for a synthesised 1 kHz square, 20 chunks, 2 s.
make test-snd SB16=1 TESTAPPS=build/sbtest.img
python3 tools/sndcheck.py build/snd.wav 1000         # -> 2.00 s at 1000.0 Hz
```

The window says which half failed: FMTEST shows `K` (both verbs fine), `P`
(patch refused) or `N` (note-on refused); SBTEST shows `g:` grant and `o:`
open. `make test ADLIB=1` is the same card with no capture. With no card the
probe correctly finds nothing; a driver that failed to attach opens the
Control Panel on its Drivers page. On MartyPC, `MARTYPC_WAV=<prefix>` writes
one wav per source in the format `sndcheck.py` reads.

---

## Hard disks

### Booting FROM one (SPEC.md §52.10)

The boot chain — MBR, volume boot record, kernel — is testable **without the
installer, deliberately**: if a disk the fixture builds boots and one the
installer builds does not, the fault is in the installer.
`tests/hdboot.py` is the automated row; by hand:

```sh
python3 tools/os88hdd.py \
    --template build/martypc/run/media/hdds/default_xtide.vhd \
    --out /tmp/boot.vhd --kernel build/kernel.bin \
    --vbr build/boothd.bin --mbr build/mbr.bin \
    --file CTRL.DRV=build/ctrl.drv --file FORMAT.DRV=build/format.drv \
    --file HDD.DRV=build/hdd.drv --file HDDTOOL.DRV=build/hddtool.drv
cd build/martypc/run && MARTYPC_DEBUG_ADDR=127.0.0.1:9001 ./martypc_headless \
    --machine-config-name os8088_xt_hdd --mount hd:0:/tmp/boot.vhd &
python3 tools/os88marty.py 127.0.0.1:9001 run          # it starts PAUSED
python3 tools/os88marty.py 127.0.0.1:9001 verify       # the kernel is aboard
python3 tools/os88marty.py 127.0.0.1:9001 shot --rendered /tmp/hd.png
```

`os88hdd.py` writes what §52.10.4 says the installer writes: `boot/mbr.asm`,
one active partition, a FAT16 volume, `boot/boothd.asm` with the volume's
BPB over its first 62 bytes and the kernel's sector count at offset 508, and
`KERNEL.SYS` first and contiguous from cluster 2 — which the VBR requires. A
`.DRV` lands read-only, hidden and system, as the installer writes it.

**NOTE THE `run`.** `martypc_headless` comes up paused at the reset vector
and every debug command answers while it sits there, so a `verify` reporting
88% differing and `boot_ticks: 0` is a machine never started. **No floppy is
mounted**, so what the screenshot must show is a **Disk C** zone: `HDD.DRV`
is not loaded (no `SYSTEM.CFG`), so only `dsk_boot_from_x` adopting the boot
partition (§52.10.3) can have made it. On an installed machine every file the
boot reaches is on the hard disk — a volume holding only `KERNEL.SYS` boots
to a desktop where the Control Panel silently does not open, which is not a
bug.

**Then read `dsk_vtab`, do not count icons.** Tick the driver (which mounts
what it finds, §52.6.1), select **Hard Drive**, click **Mount**: the table
must be unchanged — one hard-disk row, the `DVK_BIOS` one the boot left —
and the caption `Already mounted`. Two zones for one partition is
§52.10.3.1.

What this cannot tell you, both the 5150's: whether the timing is tolerable,
and whether a real option ROM's AH=08h reports the geometry its AH=02h
translates with.

### The driver, its partitioner and its formatter

QEMU has an ATA disk at 1F0h and SeaBIOS gives it to `int 13h` as 80h, so
both rungs of §52.1's transport ladder are testable there, end to end:

```sh
make test HDD=40                 # a blank 40MB raw IDE disk, KEPT between runs
# System menu -> Control Panel -> Drivers -> tick Hard Drive
# -> select the 'Hard Drive' page -> Format -> pick a slot -> Format
#    (partitions AND formats, §52.2; a used slot asks for the click again)
# -> Close -> Mount              ... one icon per FAT partition: HDD C, HDD D
rm -f build/hdd.img              # start over from a blank disk
```

**Pair it with a host-side read**: `python3 tools/os88disk.py --verify-hdd
build/hdd.img` is the partitioned-image fsck (`--verify` is the floppy one
and refuses a FAT16 partition). Then **compare every copied file against its
source byte for byte** — that is how a chunk-size bug that truncated a 116KB
file to 64,512 bytes was found, with no error anywhere on screen.

**Persistence needs the panel CLOSED.** Nothing on the page writes
`SYSTEM.CFG` from a click; it is staged and written at the panel's teardown
(SPEC.md §31.8). So: mount, type a geometry, **click the close box**, quit
QEMU, `make test HDD=40` again — every volume back with no clicks. A run
that quits with the panel open reboots with the probe's numbers and reads
exactly like the blob not persisting. Minimizing is not closing; the System
menu's Restart is the other way that flushes. Both change `build/os8088.img`
(it gains `SYSTEM.CFG`), so rebuild the images when the starting state
matters.

What QEMU cannot show: the **MFM** rung against a real XT controller's ROM
(`make xt-mfm`, `hdc_1 = st506_xt`) and the 8-bit-bus behaviour that gates
rung 1 off an 8088. **And check the desktop on CGA**: `make test VIDEO=cga
HDD=40` — a third zone does not fit above the dock on 200 lines and wraps
into a second column (SPEC.md §26.1), invisible on VGA.

### Hibernate and resume (SPEC.md §87)

```sh
python3 tests/hibernate.py            # the boot partition, DVK_BIOS
python3 tests/hibernate.py --driver   # ...and the same VHD through HDD.DRV
```

Both on `os8088_xt_hdd`, both building their own fixture with `os88hdd.py`.
Each opens an About box as the witness, picks `Hibernate...`, takes the
button with the mouse on one pass and `Enter` on the other (different code
paths; the mouse one shipped broken once), waits for the ROM's text mode
(`os88marty.video_is_text`), restarts, and answers the question both ways.
**The assertions are memory reads**: `[hb_mode]`, `[hb_resumes]`, the
instance table, `[sch_lock]` and `[gfx_lock_flag]` back to 0, the tick
advancing, and `HIBERNAT.PTR` gone from the VHD, read on the host with
`tests/instdeep.py`'s FAT reader. It builds a VHD of its own per PROCESS
(`build/hiber-<pid>.vhd`) and removes it again: the path used to be fixed, and
three concurrent runs then mounted one hard disk read-write in three
emulators.
What it cannot see: time, a VGA palette coming back (the machine is CGA), and
IDE rung 1.

## Host-side tools live in `tools/`, and a set of them goes in a FOLDER

`tests/` is guest code; `tools/` is the host side. A few tools are gates in
their own right, run like the `tests/` scripts against a built tree:
`tools/sucheck.py` (the raise cache, SPEC.md §11.96) and
`tools/notepad/pixcheck.py` (Note Pad's incremental redraw against a forced
full repaint). `tests/wmartifact.py` aims the same question at two open
window-manager defects (docs/history/WM-ARTIFACTS.md is its report; both are
invisible in a screenshot and the second leaves its residue on the
**secondary** display).

**A tool that grows into several files gets a directory named for WHAT IT
DRIVES**: an application → its dock name (`tools/notepad/`); a driver → its
Control Panel checkbox label (SPEC.md §31.9); anything else → the subsystem
(`tools/martypc/`). Give the folder a `README.md` saying what the tool
answers, what the other instrument for the same subject is, and how it has
already lied — `tools/notepad/README.md` is the worked example.

### Name the FILE, never the row (`dispcp.open_named`)

**A scripted session must never write down which row a package is on.** The
listing is sorted by name (SPEC.md §19.4), a subdirectory synthesizes `..`
at slot 0 and the root does not (§19.5), and adding one package to `GAMES/`
renumbers every entry after it. A stale index does not error — it
double-clicks whatever sorted into that slot, and reports that the app "did
not launch" several steps later.

```python
dispcp.open_named(m, mo, S, settle, wx, wy, "MISSILE.O88")
```

It asks the guest which entry that is (`row_of`), scrolls it into view by
reading `[FS_SCRL]` back (the kernel's arithmetic, so no `fit` to track), and
clicks. It reads the **window's own cache**, not the global mount snapshot:
§18.9's quiet mount leaves `disk_nfiles` at 0 with `[dsk_lstale]` raised,
which is an ordinary state after mounting a RAM disk (`tests/rdmove.py`).
`open_row` survives for the one caller that means a position on the glass,
prints the entry it clicked, and takes `expect="NAME"` so the wrong file is
refused rather than launched. The arrow keys only reach the window while it
is **frontmost**, so raise it first. `tools/os88ui.py`'s `path()` is the
same thing one level up.

## Everything not shipped lives in `tests/`

Nothing under it is built by `all`, nothing in it reaches a shipped floppy.
**Gates** answer pass/fail; the ones with a guest package:

| Package | Asserts | Run it with |
|---|---|---|
| `fmtest` | the AdLib FM surface (SPEC.md §34.2/§51.4) | `make test-snd ADLIB=1 TESTAPPS=build/fmtest.img` |
| `sbtest` | the Sound Blaster streams (§34.5/§34.6) | `make test-snd SB16=1 TESTAPPS=build/sbtest.img` |
| `filetest` | the write path (§18.4); `build/filetest-frag.img` is the fragmented-volume variant. Pair it with `os88disk.py --verify` | `make test TESTAPPS=build/filetest.img` |
| `fsxtest` | fullscreen exclusive (§53): keys 0–8 cycle every mode, `x` a same-mode bracket, `t` a duration-0 tone; the window shows `fsx_caps` (01EF/000F/0011 by adapter) and the last result | `make test TESTAPPS=build/fsxtest.img`, also under `VIDEO=cga`/`herc`. `tests/fsxdisp.py` is the MartyPC row, on the 360KB twin `build/fsxtest360.img` |
| `socktest` | a TCP connection over the parallel cable (§62.11) from a package's worker, against `tests/lptlink/partner.py`'s `SocketBox` — real host sockets. The server holds its second send until the guest has served an empty read on a live socket, which is the one mistake that gives a plausible short page | `make socktest && python3 tests/socktest.py` |
| `brfetch` | the browser fetching a page over the cable (§71): the exact request line and `Host:` header the server saw, ink below the bar, the status line | `make browsertest && python3 tests/brfetch.py` |
| `ethcfg` | an address set BY HAND and remembered across a reboot (§72.7). Finds the Ethernet row at `[cp_nst]` rather than counting, because a one-adapter machine draws no Display page (§31.10.1) | `make ethertest && python3 tests/ethcfg.py` |
| `ethernet` | an NE2000, the stack, and the browser over it (§72): PROM address plausible, DHCP bound to slirp's addresses, a page fetched over TCP into a real host socket. `ETHDUMP=<file>` writes every frame to a pcap | `make ethertest && make browsertest && python3 tests/ethernet.py` |
| `drvscroll` | the Drivers page's pressed look costs one control (SPEC.md §31.1.2): 0 differing pixels in the row band either side of an arrow press *and* no flashing rect reaching into it, on the 1bpp framebuffer | `python3 tests/drvscroll.py [machine] [image]` |
| `drvcall` | a package reaching a driver (§20.11), and that the driver was handed the *package's* segment in `ES`; against `RAMDISK.DRV`'s two package verbs, so it runs on MartyPC | `make drvcalltest && python3 tests/drvcall.py [--adapter herc]` |
| `stackprobe` | the 384-byte task-stack margin (§8): its worker 0xCC-fills its own slice, spins so every interrupt lands there, and reports the high-water mark against `SCH_STACK` | `make stackprobe`, then `stkprobe360.img` on the real machine — see below |
| `xmtest` | the extended-memory **teardown** (§41.5/§29.4). Needs a store, so QEMU on a 386; the assertion is in `tests/xmcheck.py`, which reads `xm_tab` over QMP around the close | `make test TESTAPPS=build/xmtest.img` then `python3 tests/xmcheck.py build/qmp.sock` |
| `trkscrl` | the pattern view scrolls and reaches past one row (SPEC.md §45.12.2): a jump of *n* rows leaves the row area byte-identical to a full repaint and costs one scroll, asserted without a clock. QEMU because §45.9.1 turns the grid into one banded line on a tier-0 machine | `make trkscrl && python3 tests/trkscrl.py` |
| `assoctest` | the file type association gate (SPEC.md §54): six rows that must read PASS after **double-clicking `TEST.AST`**, not the program — the header rule routed the document, `OSAPI_ARG_FILE` handed over `TEST.AST` and is read-and-clear, the locator reads bytes, `OSAPI_ASSOC_SET` takes a registration and refuses honestly when the table fills. `TEST.AST` must already carry a bare document icon in the listing | `make test TESTAPPS=build/assoctest.img`; `tests/assocopen.py` is the MartyPC row |
| `trklog` | not a gate — a **recorder**: Tracker built with `-DTRKLOG`, one record per tick to `TRKLOG.TXT` (§45.14) | `make test SB16=1 TESTAPPS=build/trklog.img` |

`stackprobe`'s QEMU answer is not the answer: SeaBIOS services its interrupt
entries on an internal stack, so under QEMU only this kernel's own tick and
mouse handlers land on the slice. A real IBM BIOS runs int 09h on the current
task's stack and STIs early, so the tick and the mouse nest on top of it.
Boot `os8088-360.img` on the real machine (or `make xt`), launch
`STKPROBE.O88` off the probe floppy, hold a key down, mash the mouse, play a
module, then read High water. docs/plans/completed/STACK-SLOTS-PLAN.md §9 has the
field readings, and `make stkdiag` is the kernel measuring the same thing
itself.

`benchlib.inc` is the one shared source under `tests/` — the timing loop,
the 48-bit arithmetic, the report arena and the file writer `gfxbench` and
`sysbench` share. It is shared rather than copied so two harnesses can
disagree (PERFORMANCE.md Part 6 rule 7).

`trklog` is `apps/tracker` assembled a second time with `-DTRKLOG`, every
hook inside `%ifdef TRKLOG`, so the thing measured is the shipped code; the
shipped `TRACKER.O88` carries none of it. Keys, on the windowed splash:
**X** XT mode (5,500 Hz), **L** load `BEVERLY.MOD` (not P, the pattern-loop
toggle), **D** arm the log (`LOG nnnn /0512`), **F** fullscreen and back,
**SPACE**/**ENTER**/**HOME** stop, resume, restart, **W** write `TRKLOG.TXT`
(refused inside a bracket — the file API is UI-callback-only, SPEC.md §53.7),
**M** stamp FL bit 10h because you HEARD something (the only input that is
not a measurement), **Y**/**T** the display back on the mixer / the frame
clock back on the tick (recorded in FL 20h/40h on every record; T takes
effect at the next F), **K** XT mode's sample rate without leaving XT mode
(docs/FIELD-NOTES.md 16). `CLICK.MOD` (`tests/mkclick.py`) is four patterns
at four pitches so the position is audible. The buffer is a ring of the last
512 ticks (28 s), a heap claim taken at D and given back at D; read the file
off `build/trklog.img` with `os88flush` or a FAT12 extractor. The columns
beyond §45.14: **AR AP** what the screen showed (the card's row, since
§45.15), **SD** the lead between them in rows (near `1.19 x BPM / speed`;
63 means the stamp ring lapped), **FX DX** the longest drawing frame and feed
pass in the tick (00 is healthy), **PLAY** `CONS` interpolated between block
IRQs (§45.15.1).

### Frotz: the story harness, which is `trklog`'s shape again

`apps/frotz` built a second time with `-DZHARNESS` (SPEC.md 61.13): a
**teletype on COM4 (`0x3E8`)**, the story's output out a byte at a time, its
keystrokes back in, and four markers saying where it is.

```sh
make zh                                     # the harness interpreter
python3 tools/zharness.py ADVENT.Z3         # play its script, print the log
python3 tools/zharness.py ADVENT.Z3 --repl  # ...or type at it yourself
python3 tools/zharness.py --all --compare   # every story, diffed vs dfrotz
make zcheck                                 # the same, as a gate
```

It boots QEMU itself (the socket chardev is entry 3's shape), builds the B:
disk itself (the story arrives as `STORY.DAT`), and double-clicks its way in
— **a timeout waiting for `[[ZH:READY]]` is that walk, not the interpreter**;
`--shot` writes what the screen had on it, and the walk is retried once.
Three deliberate differences from a real session, each at its `%ifdef`: no
`[MORE]` paging, no echo on the wire, no status line on the wire. `--compare`
diffs the WORDS in order, not the lines; both sides' commentary is dropped;
the reference's upper window is recognised by its padding and dropped, which
fails safe. There is no way to opt a story out. **`refused, with a reason`
is a pass** (§61.4/§47) and a `@random` story can diverge run to run —
re-run before believing a divergence that names a random event.

**`make zgfx` asks what the transcript cannot** (SPEC.md 61.14): a story that
loses its quote box prints the same characters as one that keeps it. It
compares the interpreter's model of each row against the pixels, again after
a repaint, and each opening screen against the real curses Frotz's. Every
complaint leaves a PNG and a `.wire` in `build/zh/`; `make zscreens` is the
only part needing `frotz` and `pyte` installed, and each golden is two takes
so a story that rolls its epigraph still measures. Do not make it send keys:
`PgUp`/`PgDn` left `DREAMHLD.Z8` with no further prompt every time.

### Benchmarks

`fontbench` prices the primitive (SPEC.md §6.1.1), `typebench` the keystroke
(§11.94), `gfxbench` the whole drawing surface on whichever adapter it booted
on, `sysbench` the machine underneath. All four ride one disk:

```sh
make bench
make test                            TESTAPPS=build/bench.img
make test VIDEO=cga                  TESTAPPS=build/bench.img
make test VIDEO=herc HERCSEG=0x7000  TESTAPPS=build/bench.img
```

`netbench` measures nothing itself: `ETHER.DRV` brackets its own stages with
the PIT (SPEC.md §72.15) and `netbench` starts, stops and renders the block.
`make netbench && make test TESTAPPS=build/netbench.img`; open both windows,
**S**, transfer, **X**, **R**; **W** writes `NETBENCH.TXT`. No MartyPC
column, ever: under QEMU `calls` and `KB` are exact and `ms` is the host's.

### A benchmark that is meant for the FIELD is ONE BOOTABLE DISK

**The calibration machine has one floppy drive** (docs/FIELD-MACHINES.md),
so a harness on a data floppy means a swap mid-session and the numbers do
not get taken. A field harness rides the SYSTEM disk — `make field` and
`make npbench` are that shape; copy either rule. It must not be
write-protected (the report is the deliverable), and a rebuild of a shipped
app carries the app's own file name: `npbench` ships as `APPS/NOTEPAD.O88`
so §54's association opens the reference note by double-click.

**The disk to send is `make combo`**: `build/combo.img`, one 360KB bootable
floppy with the system, every application and all four benchmarks. The
Control Panel's Display page switches the primary at run time, so one disk
takes a set from both cards — run `GFXBENCH.O88`, switch, run again; it
names each report after the adapter it found. `make field` still builds the
narrow disks (docs/FIELD-MACHINES.md has the table).

### `gfxbench` and `sysbench` — the two that write a file

They page (`Space`/`PgDn`/`PgUp`/`Up`/`Dn`/`Home`/`End`) and **save the
whole report** with `S` or the Bench menu; `R` re-runs. `gfxbench` writes
`GFXVGA.TXT` / `GFXHERC.TXT` / `GFXCGA.TXT`, `sysbench` `SYSBENCH.TXT`, to
the current volume and directory (SPEC.md §19.2) — the bench disk's root, so
it must not be write-protected (on 86Box, the `wp://` prefix every launch
target now strips).

**Driving one from a SCRIPT is four steps and three have a trap in them:**

- **Reach the app through the Bench MENU, not a keystroke.** A scripted
  `m.key("KeyR")` did not reach `sysbench` here — the splash was still up
  150 s later — while `mo.menu(110, 8, 110, 26)` (`Run`) started it every
  time.
- **Do NOT `settle()` after starting the run.** The machine is deliberately
  frozen while a benchmark runs, so stillness means nothing and a `settle`
  never returns. Sleep a fixed generous span (~40 s on a 4.77 MHz 8088) and
  read the file.
- **Read the FILE, not the screen**:
  `os88flush.Flush(marty=m).volume(0).read("SYSBENCH.TXT")`, sharing the one
  debug connection an instance allows.
- **Keep the driver script out of the session scratchpad**; a vanished
  script reads as `can't open file`.

**On an extended desktop the name is the card the SANDBOX is on**, resolved
against §57.4's `VD` block, so two cards give two reports from one launch.
Check the `sandbox straddles` row before comparing two.

`gfxbench` is one package for Hercules and CGA on purpose — the same 1bpp
renderer over four numbers read from `OSAPI_VIDEO` — and runs on VGA for
contrast. It prices raw bandwidth first (the same loop against RAM and the
framebuffer, four access shapes), primitives at two sizes so per-call and
per-pixel terms separate, `gfx_blit4` against a solid and a four-pixel-run
source (a coalescer that stopped coalescing reads near 100), the same ten
characters as `fontbench`, and the gfx lock measured as `UNLOCK+LOCK` — a
package cannot take a lock it already holds, and what is in the pair is the
mouse cursor (SPEC.md §7.1); it is the one row an IRQ can land inside, so
`[bl_max]` and the `!` flag are what to read it against.

`sysbench` prices 8086-nominal clocks against a real 8088 per instruction
class, RAM bandwidth, the clock ladder, the API's far-call floor, what the
kernel's own interrupts cost per second of work, and the floppy — twice,
because the first read pays the motor spin-up. Two of its floppy blocks
exist to pin the numbers MartyPC's disk model takes on trust (Set 35): `seek N
cyl, pair` (read the rows as revolutions; what it measures is the distance at
which the cost steps up) and `1 sector, motor COLD` (waits for the BIOS
motor countdown at `0040:0040`, prints `0040:003F`, N = 1 by necessity; a
motor status other than `00` means the cold row is not cold). Both go
through `dsk_dbg_raw`, so they need a `DISKCNT=1` kernel — which every `make
field` disk is.

Reading their output: a method-`t` row of 0 counts finished inside one
tick; a `!` flag means one iteration came within a third of the PIT wrap;
under QEMU almost every row is noise and two are worse (the retrace period —
QEMU's status port toggles on every read — and the VRAM rows under
`HERCSEG=`, which measure plain RAM). **Treat every number as provisional and
cite the machine and date it came from.**

**Under QEMU the numbers are not time at all.** `-icount shift=3,sleep=off`
makes the PIT count guest *instructions* — reproducible, ±1 count, and not
microseconds:

```sh
make test TESTAPPS=build/bench.img QEMU="qemu-system-i386 -icount shift=3,sleep=off"
```

Under `-icount` guest and host time come apart (a 12 s boot takes 45–90 s),
so **poll the dump for the state you want; never sleep a fixed interval**.
And **there is no double-click in `tools/mouse.py`**: two `click` runs are
two processes a second apart, which the kernel reads as two single clicks.
A double-click has to be one process — `goto`, then `mouse_button 1`/`0`
twice ~0.12 s apart — and importing `mouse.py` for `goto` has a trap:
`importlib` runs the module with `__name__` set to the loader's name, so its
`main()` never fires and the pointer never moves, the clicks landing wherever
the last command left it.

### `tests/dispclose.py` — the close negotiation and the alert (SPEC.md 75, 27.15)

```
make && python3 tests/dispclose.py
python3 tests/dispclose.py --machine os8088_5150_herc_gla
python3 tests/dispclose.py --machine os8088_xt_vga
make small && python3 tests/dispclose.py --small
```

One boot, Note Pad through every branch of closing with unsaved work, ending
by reading `NOTES.TXT` **off the floppy with `os88flush`** rather than asking
os8088 whether it saved. Four things to copy into the next gate of this
shape:

- **The alert is found in the WINDOW TABLE, never by pixels**: an unowned
  window (`wm_owner` = 0xFF) the test does not already know — and exclude the
  file dialog, which is unowned too.
- **A shared rect keeps drawing and hit test consistent; it cannot make them
  right.** Two register bugs in `os88ui_arect` each drew perfect buttons
  that could not be clicked; the tell was Cancel opening a Save As dialog.
- **A settle is not a launch.** A package load satisfies `settle` — the
  machine is frozen under the gfx lock and perfectly still — so `wait_launch`
  polls the window table.
- **A scratch disk is rebuilt, never cached on existence.**

`--small` needs `os88sym.syms(("KERN_SMALL",), check=False)` and
**`WIN_SIZE` 28, not 34** (`W_ONDRAG`, `W_ONTIMER`, `W_TIMER` are inside
`%ifdef KERN_BIG`); read with 34 the table is plausible for slot 0 and
nonsense from slot 1 on.

## Modelling the old machine from a fast one

**The container is roughly three orders of magnitude faster than a 4.77 MHz
8088.** A constant sized while looking at QEMU encodes the wrong range and
fails structurally, not proportionally:

| what was sized against QEMU | what a real XT did |
|---|---|
| a 16-bit elapsed counter, one subtraction start-to-end | rows are 1.5M counts; it lapped silently into a small plausible number |
| `>= 32768 means the run overran` | most legitimate rows are 32768..65535; it discarded them |
| a ratio computed from `counts >> 4` | still 90,000; it overflowed the word and printed 696 for 134 |
| `OSAPI_WM_GROW` on every keystroke | free in the emulator; a visible flicker in a 13×13 corner at 33 ms a keystroke |

**Size every range from the slowest machine it will run on.** A 32-bit
accumulator folded per iteration cannot lap. MartyPC removes most of this —
a cycle-accurate 8088 is roughly the right machine — except for the disk,
where the rule at the top of this file applies.

### Three calibration numbers, so an estimate needs no machine

Measured on the 4.77 MHz 5150 (PERFORMANCE.md Part 2 is the full table and
CLAUDE.md the condensed one):

- **~756 µs fixed cost per `gfx_*` call** before a pixel is drawn — CPU-side,
  not bus-side. **A redraw is priced by how many primitive calls it makes,
  not by how many pixels it covers.**
- **~1 ms per 8×8 glyph cell**, four independent measurements agreeing. A
  40-cell line redraw is ~36 ms.
- **Instructions are the better proxy, not framebuffer traffic**: the 8088's
  floor is 4.34 clocks per instruction *byte*, so an 8086 cycle count
  under-reports by 1.01× to 4.34× depending on encoding length.

### Count work, don't time it — QEMU is exact about the first and useless at the second

On MartyPC `step` returns real cycles, so work and time are one question for
the CPU. Under QEMU the split is absolute — but the *amount of work* the guest
does is identical on both, so when the question is "is this slow because it
does too much?", instrument a counter and read it:

```nasm
dbg_cells:  dw 0                ; kernel/font.inc
...
font_run_cell:
    inc word [cs:dbg_cells]
```

Then `python3 tools/os88sym.py dbg_cells` for the address (on MartyPC,
`m.read(m.sym("dbg_cells"), 2)`; on QEMU `xp /2xh 0x<linear>` — `h` is a
word, HMP's `w` is four bytes). **Never take an address from `nasm -l`**: a
`.bss` label's address there is section-relative and fixed up afterwards, a
plausible small number pointing into `.text`. A package can write the same
counter with `mov ax, KERNEL_SEG / mov es, ax / inc word [es:off]`.

This settled SPEC.md §27.4: typing slowed as a note grew, the hypothesis was
that more characters were being redrawn, and the counter said **2 cells per
keystroke at every length** while the layout walk grew linearly. **Measure
before redesigning**, and **a counter is not a timer** — multiply by the
calibration numbers and say that you did.

The same check verifies `FSXF_FASTTICK` (SPEC.md §53.2.1): `[ticks]` must
advance at 18.2/s at the desktop, inside an armed bracket and after leaving
it, and `[sch_fast]` read N inside and 0 after. Both of that feature's bugs
booted and drew a correct first frame; one halved the tick rate and only the
rate reading caught it.

### Prefer a self-checking harness to a careful one

Three of the four bugs above were caught by **one number on screen
contradicting another**: `typebench`'s CHAR row does 1.33x `fontbench`'s PAIR
work, so it cannot be the smaller number. Put redundant quantities on the
screen — a raw count and a derived time, two rows whose relative sizes are
known, a ratio you can recompute from its columns — and label the harness's
own state (`snap:on`/`snap:off`): `typebench`'s VGA row refuted SPEC.md
§11.94's "the flag is a no-op on VGA" for years, three lines below a header
printing `snap:off`, because the number was filed as a fact about `font_run`
rather than about alignment. **When a measurement and a design rule disagree
inside one file, the measurement is not the thing to explain away.**

**A gate must not be able to pass by doing nothing.** `tools/sucheck.py`
covered Solitaire by clicking a hard-coded (300, 40) that was inside
Solitaire's own rect, so nothing was raised, `wm_su_take` was never entered,
and the run reported 78 differing bytes and PASS — a better figure than a
real run's 124. Now the click point is computed from the window rects read
out of the guest, and the claim map is an assertion.

**A hard-coded ROW NUMBER selects a DIFFERENT PROGRAM.** `tests/dispcorner.py`
opened "row 1 of B:, then row 3" believing that was `APPS` then `HELLO.O88`;
it was `GAMES` then `MISSILE.O88`, which has a worker drawing every tick, so
its two captures differed by 219 pixels and then 62 for the identical script
— **an unstable count is the tell; a redraw defect is deterministic**. The
row is looked up by name (`dispcp.row_of`) and the launched window's size is
asserted against `hl_tpl`'s 240x90. **A subject for a pixel diff must be
INERT**, which is a good part of what `hello` is in the tree for.

**And the CONTROL must be a control.** "Open and close the Control Panel"
ends in `wm_paint_dmg` (SPEC.md §11.91) over the panel's own rect, so both
captures came off incremental draws and a region the panel never covered
compared to itself. Poke `[cp_dirty]` instead — `ui_task`'s `.chk_cp` is its
only consumer and it is `gfx_lock` / `wm_paint_all` / `gfx_unlock` — **and
read the flag back**, because the poke happening is not the repaint running.
And `wm_paint_all` alone is not enough inside a window: `wm_draw_win` puts a
valid raise cache back *instead of* running `W_PAINT` (§11.96), so clear
`WF_SAVEU` across the forced repaint, which `wm_su_ck` tests. Desktop dither
is drawn directly and is honest either way.

### What the emulator cannot show at all

Not "shows inaccurately" — cannot show. **Do not call all of these
"flicker"**; they are three defects with three causes (PERFORMANCE.md Part 1
is the vocabulary):

- **A visible redraw.** A window's whole content painted again. On hardware
  you *watch it happen*, and on Paint or a full Disk window it is
  **seconds**; under QEMU it is microseconds and a screendump either side is
  identical. The single most expensive mistake available in this codebase.
- **A double-draw flash.** Anything drawn twice — the erase-and-letter pair
  leaves a line blank for tens of milliseconds, on every keystroke. Note
  Pad's (SPEC.md §27.2) and the grow box's were both found by a person
  watching the real machine. `os88marty.py flicker` measures it now.
- **Perceived latency and input overrun**, a property of the real machine's
  speed against a real person's typing (why the tracker stops animating its
  grid on tier 0, §45.9.1).

And one the emulator reports as a **success**: an optimisation that kept its
shape and lost its substance. `gfx_blit4`'s first version emitted one call
per run as designed and decoded every pixel by hand inside them; QEMU priced
it identically. Rewriting something whose *reason* is speed means verifying
the reason survived, not the structure. It has since been priced on the
target: `runs × 0.5 ms` (SPEC.md §5.4).

**A mono-primary two-card 5150** used to be one more: MartyPC derives SW1-5/6
from whether a CGA is present and exposes no DIP override.
`tools/martypc/patches/03-video-dip-config.patch` adds `video_dip`, and
`os8088_5150_both_gla_mono` — both cards, switches mono, os8088 on Hercules
with `avail = 0x06` — is the only machine that reaches §39.11.1's
`vid_cga_alias`. Its MDA is listed first, or the boot gate watches the wrong
card (docs/MARTYPC-DEBUG.md).

**A status line from hardware that is NOT THERE.** SPEC.md §18.97's floppy
probe decides whether drive B exists from the uPD765's ST3 bit 4 and ST0
after a recalibrate. MartyPC answers both for a drive its config does not
have (`os8088_5150_cga_1fd` reads `ST3 = 0x79`, `ST0 = 0x29` — a present
drive's answers) and QEMU's `fdctrl_handle_sense_drive_status` answers
`0x28 | (track == 0 ? 0x10 : 0) | unit` off a track that is 0 for a drive
that is not there. Both say *present* unconditionally, for different
reasons, so **no emulator here can produce the absent verdict**. That is the
safe direction — the probe fails towards *keep* — and `make FDDABSENT=1`
splits the halves (§18.97.2): it forces the verdict for unit 1 with no port
touched and fills §57.5's block with the field 5150's bytes, so the
**decision** is testable here while the **conversation** stays the 5150's.

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
    print("tier", m.read(m.sym("cpu_tier"), 1)[0],
          "row1", m.read(m.sym("dsk_vtab") + 16, 3))
PY
# -> tier 0, row1 (0xFF, 1, 0): retired, desktop shows A: alone
make test FDDABSENT=1   # QEMU is tier 2
# -> row1 (0x00, 1, 1): kept, desktop shows A: and B:
```

**Testing §18.97 is three runs**: a two-drive machine must be **untouched**
(`make FDDPROBE=0` against the default, `m.vram()` diffed on `_cga_gla` and
`_herc_gla`: 0 pixels); a one-drive machine must not probe at all
(`os8088_5150_cga_1fd` reads `eqp=01 ran=00` in §57.5's `FD` block and row 1
of `dsk_vtab` stays `DVK_BIOS`); and the mechanism, in a **scratch** kernel
with `desk_init`'s count gate (`cmp dh, 2` before the `dsk_fdd_probe` call)
forced and `dsk_fdd_probe`'s `test al, 0x10` neutralised, must complete and decode — `step=02` (`FDD_S_SEEKOK`) with
a plausible ST0, not a hang and not `step=05`. Revert both.

## MartyPC — the first thing to reach for

`make marty`; the whole recipe, the machine list and the protocol are
docs/MARTYPC-DEBUG.md, and `tools/os88ui.py` is where a script starts. What
it gives that neither of the others does:

- **A cycle-accurate 8088 running a real BIOS**, agreeing with the 5150 on
  45 of 47 `gfxbench` rows (PERFORMANCE.md Set 11).
- **A debugger that costs the guest nothing**: memory, registers, ports,
  breakpoints, single-step, cycle counts, over a socket. `verify` diffs
  `KERNEL_SEG` against `build/kernel.bin` in one command.
- **A modelled CGA and MDA/Hercules**, so SPEC.md §39.1's probe runs.
- **A PC speaker, an OPL2 and a Sound Blaster** (DSP 2.01, ours — upstream
  has no DSP), captured with `MARTYPC_WAV=`.
- **The floppy the guest wrote, back on the host**: `tools/os88flush.py`
  (`diff`, `ls -R`, `get`, `verify`), the only route to the write path that
  is not the read path. Its `writes` counter is not a dirty flag; `dirty()`
  compares content.
- **Input through the real path**: `key` enters the keyboard buffer (int
  09h), and a mouse packet is clocked through the UART so `mou_isr` decodes
  it — more than a poke to `[mouse_x]` and more than QEMU's `msmouse`, which
  ignores DTR.
- **An `int` breakpoint on 13h against an unmodified shipped kernel** answers
  "how many `int 13h` calls does one file load issue?", where it used to
  need `DISKCNT=1` and a test package. The timing of those calls is still the
  5150's.

Three things a script gets wrong, each of which reads as a broken feature:

- **Click with `tools/os88mouse.py`, never `os88marty.py mouse` or
  `os88mouserel.py`**, unless the mouse itself is under test. Those are
  RELATIVE — a real packet, dead-reckoned from the edge clamp, and the clamp
  eats overshoot without saying so. `os88mouse.py` reads the live cursor out
  of the debug registry (SPEC.md §9.4.3) and re-sends until it agrees; it
  fails loudly when it cannot converge.
- **`dblclick` and `menu` are their own verbs.** Every double-click detector
  compares the two presses' birth ticks against a **9-tick** window
  (`UI_TDBLT`, `DESK_DBLT`, `FM_DBLCLK`, `FD_DBLCLK`); two `click`s are 1.5 s
  apart, and packets sent faster than 1200 baud carries them are dropped.
  `dblclick` proves all four button edges against `mouse_btn`, measures the
  gap in the guest's own ticks at `0040:006C` (healthy: 2–4), and raises if
  the window was missed. A menu cannot be opened with a click: `menu_track`
  polls a level, so press-and-release in place opens and closes it.
- **One connection at a time.** `Mouse(marty=m)`, `Flush(marty=m)` — never a
  second `Marty` on the same instance; it is refused naming the holder. Two
  machines are two `launch`es.

`launch` gives every instance its own port, run directory and disks; take
the address off the object. `settle(m)` gates the boot on the desktop's own
structure, because stillness alone returns during POST and "left text mode"
hangs on Hercules, whose MDA reports text mode in every mode. `m.sym(name)`
is where a kernel symbol lives.

---

## What 86Box is genuinely for

**A machine that is not an 8088**, a period bus under a card rather than a
modelled one, and a second opinion on the video probe — with a person
watching, because it has no debugger and no automation socket. CLAUDE.md
carries the target list, one per `vm/` directory (`make xt`, `xt-640`,
`xt-cga`, `xt-hercules`, `xt-ega`, `xt-multimon`, `xt-mfm`, `286`, `286-525`,
`386sx`, `386`, `386-ps2`, `486`, `pentium`, and the application machines).

`xt-multimon` is the two-card XT (`gfxcard = cga` + `gfxcard_2 =
hercules_plus`), a second instrument for a machine MartyPC already has:
what it adds is a real 6845 pair on a period bus, what it lacks is
`dualcheck.py`'s reach into guest memory, so it answers *does it look right*
rather than *is it right*. `xt-z` gives the XT a 720KB drive as B:
(`fdd_02_type = 35_2dd`), because a 360KB disk does not hold a story
library. The `486` and `pentium` machines are the *fast* end: 8086 real-mode
code runs on them verbatim, so they answer whether constants sized against a
4.77 MHz 8088 still behave two orders of magnitude up — typematic deadlines,
the tracker's ring refill, Arkanoid's pacing — which neither MartyPC nor
QEMU can.

It is not installed in the web container and needs BIOS ROMs.

Three traps before blaming the OS: it silently clamps `mem_size` to the
machine's maximum; a `wp://` prefix on an `fdd_0N_fn` path mounts that floppy
write-protected, which the OS reports as `Write protected` and which means
`SYSTEM.CFG` does not survive a reboot — every launch target strips it,
because 86Box rewrites its config on exit and puts it back; and an
unrecognised `cpu_family` is **silently replaced** at that family's default
speed. The cheap check for any candidate setting is to launch a throwaway
copy of the config, `kill -TERM` it, and read the file back — 86Box rewrites
it with whatever it actually accepted.
