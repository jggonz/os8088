# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

os8088: a Macintosh System 1-style GUI OS for the Intel 8086, written entirely in real-mode NASM assembly, booted from floppy. Pre-emptive multitasking, overlapping windows, serial mouse, a bottom dock, and loadable software packages that run as closable, multi-instance apps — all in 256KB of RAM. One binary drives VGA, Hercules or CGA, picked at boot.

**SPEC.md is the binding contract.** Every symbol name, register contract, constant, and data layout is pinned there. Update SPEC.md *before* changing any interface, not after.

**PERFORMANCE.md is the other one you must read before you write code.** The
target is a 4.77MHz 8088 and you are testing on a machine ~1000x faster, which
means the emulator is exact about how much *work* the guest does and useless
about how long it takes. Three visible defects cannot be observed here at all
— a **visible redraw** (a full window or screen repaint: on real hardware you
watch it happen, and on Paint or the Task Manager that is *seconds*, not a
flicker), a **double-draw flash** (anything drawn twice — background, then
content — which is a smaller area and so reads as a flash, but is still very
plainly visible), and **input overrun**. A fourth is worse: an optimisation
that keeps its shape and loses its substance measures as a *success* here
(`gfx_blit4`'s first version emitted the right number of calls and decoded
every pixel by hand inside them, which QEMU prices identically).
PERFORMANCE.md carries the calibration numbers, now **measured on the target
machine** (`tests/gfxbench` + `tests/sysbench` on a 4.77MHz IBM 5150, Part 9)
rather than modelled: **~756us of fixed cost per `gfx_*` drawing call**
whatever it draws, **~1ms per 8x8 glyph cell**, **~71ms per 78-cell row of
text**, and the **8088 instruction floor of 4.34 clocks per instruction
byte**. The first is the one to internalise — *a redraw is priced by how many
primitive calls it makes, not by how many pixels it covers*, which is the
opposite of what every estimate in this tree used to assume. **That floor has
since been taken apart** (Part 9 Set 3, SPEC.md §5.7): one `gfx_pixel` was
196 guest instructions of generic rect machinery across eleven routines with
no hot spot anywhere, a third of it push/pop pairs and near call/rets rather
than drawing, and seven changes took ~20% off it with the output
byte-identical on all three adapters. They are seven RULES, not seven tidy-ups
— a one-way flag is tested before it is recomputed, the edge masks and the
bank base are tables rather than variable shifts, `bb_col` preserves nothing
because its only caller reloads everything anyway, and `gfx_nextrow` is
inlined in every row loop because *a fill walks its rows three times*. Part
2's 756us is still the figure to ESTIMATE with: the improvement was measured
in instructions under `-icount`, not in microseconds on iron, and an inferred
number does not replace a field one. PERFORMANCE.md also carries
the standing budget every redraw path here has already been measured down
to — so a change that reintroduces a full repaint is a regression against a
documented number, not a neutral refactor — and how to count work with a
counter read over QMP. docs/TESTING.md is **which tool to reach for** and
where a test can *run* — MartyPC first, and its ordering is a deliberate
reversal of this tree's old QEMU-first habit; PERFORMANCE.md is what the
target machine *costs*.

**docs/FIELD-MACHINES.md is the third one: who has the hardware, what is in
it, and what a run costs them.** The
project is calibrated against ONE machine - `Elendilon`'s IBM PC 5150, 8088 at
4.77MHz, 640KB, with a Hercules AND a CGA card in it (both permanent, each on
its own monitor), ONE 360KB floppy, an ST-225 on an ST11M, and an AST
SixPakPlus carrying 384KB and the MM58167 the clock ladder's rung 2 was
written for - and every measured number in PERFORMANCE.md Part 2 came off it.
It is kept **entirely period on purpose**, which is what makes its floppy and
disk timings mean what they say, so "put a Gotek in it" is not a way to
shorten the seven-step path an image takes to reach it - `make field` is. It is a register keyed on the
full FORK NAME of whoever owns the iron (`Elendilon/os8088`, not a bare
handle - the file is written to be merged upstream, where a handle alone does
not say which tree the hardware belongs to), and it is in the repo for a reason
worth stating: a session is told which account it is running as and forgets it
at the end, nothing in a commit says which contributor owns a 5150, but a
fork's name (`Elendilon/os8088`) is visible to every session and every reader.
It also carries what a field run is FOR - time, and the three defects QEMU
cannot show - against what to keep in the container, which is every question
about work. Two rules in it bind whoever reads a result: **a number is not a
field number because a human handed it to you** (the same owner tests
routinely on PCem, which models period hardware at period speed, so its
figures are in the right units and do not announce themselves - ASK which
machine a report came from), and **the 5150's C: is a real DOS 3.3 install**,
so nothing may format, partition, write or delete on it. `make field`'s two
images are built on demand and SENT, never committed.

**Two standing rules for work on any branch of the `Elendilon/os8088` fork,
written down in that file's last section.** They are that fork owner's
preference, not a property of the project. **Send the 360KB pair
(`build/os8088-360.img` and `build/apps360.img`) after EVERY commit, without
being asked** - and "send" means ATTACH the files (`SendUserFile`), because a
path into the session's `build/` is a container the owner cannot reach.
**And do not boot an image after building it**, on request or after a commit:
by the time one is being built, the change it carries has almost always just
been driven as part of the work, so a second cycle re-establishes what the
first already did - the owner asks when they want one exercised harder. It
relaxes neither the testing that earns a commit nor the rule that a MERGE
onto the integration branch rebuilds and boots first, a merge being two trees
that were never tested together.

**"Merge to elendilon" means the BRANCH, not the repository.** `elendilon` is
the integration branch this work lands on — feature branches merge into it and
it is what gets tested on the iron; `main` is behind it and is not the target.
The name collides with the fork's (`Elendilon/os8088`) and with the owner's
GitHub handle, which is exactly why it needs writing down: a session reading
"push to elendilon" can plausibly hear "push to that person's repository",
which is where the branch already lives, and do nothing. `git ls-remote
--heads origin` settles it in one call. A merge there rebuilds and boots
before it is pushed — but it commits nothing under `build/`, which is
gitignored outright (see below): the two "Rebuild the shipped images" commits
in this history are what that policy exists to make impossible.

**THE CLONE IN A FRESH SESSION IS SHALLOW, AND GIT WILL LIE TO YOU ABOUT
ANCESTRY.** This has now cost one whole piece of work, so it is a rule rather
than a note. On a shallow clone `git merge-base`, `git log A..B` and
`git merge-base --is-ancestor` do not error — they return *confidently wrong
answers*, because the graft boundary is indistinguishable from a set of root
commits. A session concluded from `git merge-base origin/main HEAD` returning
nothing that `main` and `elendilon` had unrelated histories, and PORTED three
commits as new SHAs that it could have merged; the real merge base was
`52d31a2` all along, exactly where the branch was cut. Before any claim about
what is ahead, behind, merged or unrelated:

```sh
git rev-parse --is-shallow-repository     # true => every answer below is a lie
git fetch --unshallow                     # ...so do this first
git rev-list --max-parents=0 <ref> | wc -l   # >1 root = SHALLOW, not unrelated
```

That last line is the tell, and it was visible and ignored: this repo has ONE
root, and the shallow clone showed six. **A repository does not usually have
six root commits.** If you find yourself explaining an implausible history
shape, check the clone depth before you build anything on it.

**Upstream squash-merges the integration branch, and a fresh branch is then cut
from `main`. That is the CYCLE, it is deliberate, and it is not to be
"fixed".** `jggonz/os8088` keeps a linear, one-commit-per-feature history on
purpose: the last merge commit on `main` is PR #14 and every commit since is a
squash. A squash carries the branch's *content* into a brand-new commit with no
ancestry link to the branch's *commits*, so the integration branch is
**disposable** — cut from `main` at a squash, lived in for one round of work,
replaced by a fresh cut. The gap therefore self-heals at the cycle boundary and
needs no maintenance merge in between; what is lost at a re-cut is anything
that never made it into the PR.

What does *not* self-heal is `main` moving **mid-cycle** with work of its own —
which is what PRs #56/#57/#58 were, and what `elendilon` had to go and fetch.
**`docs/UPSTREAM.md` is the playbook** for that and for everything else about
this boundary: how to tell the branch's own work coming home from upstream work
that has to be fetched, how to adapt an incoming package to a kernel the branch
has moved on from (the branch's SDK is normally a SUPERSET of the one the code
was written against, so nothing fails to assemble and *every difference is
silent* — greying, glyph tables and worker stacks have all arrived broken this
way), how to resolve the merge without `--ours` quietly eating something only
`main` has, and what the PR back has to carry given that the squash turns its
title into `main`'s whole history entry — with the shallow-clone rule above as
its Rule 0, because every question at this boundary is an ancestry question.

**docs/FIELD-NOTES.md is the fourth one, and it is the shortest: what real
hardware found and the harness could not.** Open, reproduced, unfixed — with
what has already been *ruled out* for each, so an investigation starts from
evidence. Two live entries: a periodic ~1/3s audio tail-off in Tracker
(A/B'd against the pre-fsx commit and present in BOTH, so it is older than that
work and not the bracket) and a heap-fragmentation refusal where the total says
there is room and the largest run does not. The third, a **stale Disk-window
listing** that reported a perfectly good package as "Bad package", is FIXED
(SPEC.md §22.8) and worth reading anyway for what it says about this harness:
a package writing a file remounted the GLOBAL snapshot, `fmv_sync` re-listed
only on a drive/cwd change, and a display index taken from the window's own
cache then resolved against a listing that had shifted under it — pure
bookkeeping, reproducible under QEMU in four clicks, and it took a 5150 to
find because nobody here had opened a Disk window and saved a file from a
package. Read it before you spend a day re-deriving any of them.

## Commands

**BEFORE YOU RUN ANY OF THESE: `make marty` IS THE DEFAULT TEST TARGET, AND
`make test` IS THE FALLBACK.** This block used to start with the QEMU targets
and put `make marty` twenty lines down, which is most of why sessions keep
reaching for QEMU — a skim reads the list before it reads the rule. So the
rule goes first:

> **Develop on MartyPC.** Anything that runs on an 8088 — which is every
> adapter, every package, the whole kernel, all three of SPEC.md §39's video
> paths, input, screenshots and sound — is tested with `make marty`. It is a
> **cycle-accurate 4.77MHz 8088 running a real period BIOS with real CGA,
> Hercules and VGA cards in it**, and it agrees with the field machine to
> 0–4% on 45 of 47 `gfxbench` rows.
>
> **Go to QEMU when you have a specific reason, and the list is short**:
> 286/386 (86Box also does these, and better), the hard-disk driver
> (SPEC.md §52 — `make test HDD=40`; no MartyPC config here has a disk
> controller), and SPEC.md §9.5's COM2 / cross-wired-IRQ / modem mouse cases
> (`MOUSEPORT=`, a socket chardev). "It is quicker to type" is not a reason;
> neither is "I already know the QMP commands".
>
> **And for anything with a disk in its TIMING, neither one is the
> instrument** — that is the 5150 (docs/FIELD-MACHINES.md), because no
> emulator here is disk-accurate and MartyPC's error is 30x in the flattering
> direction.
>
> Why this is a rule and not a preference: QEMU is the emulator **furthest
> from the target**. It runs the guest at host speed, on a CPU that is not an
> 8088, through SeaBIOS rather than a period ROM. Nearly every entry in
> docs/FIELD-NOTES.md is something QEMU showed as fine — and SPEC.md §18.91's
> `AL` bug is the worked example of the sharper failure: QEMU ran the buggy
> binary *correctly and quickly* and said nothing, while the real machine was
> issuing 4.6x the disk traffic.
>
> `make marty` builds from source with cargo and takes a few minutes the
> first time in a container (`libudev-dev` + `pkg-config` on Linux). **Build
> it at the start of a session, not when you first need it.**

docs/TESTING.md is the full matrix; docs/MARTYPC-DEBUG.md is the recipe.

```
make          # build all six floppy images into build/
              # --- QEMU targets: the FALLBACK, per the rule above ---
make run      # boot in QEMU with emulated serial mouse (1.44MB images)
make run-640  # same, as a maxed-out 640KB machine (-m 1M; QEMU/SeaBIOS can't boot below 1MB, int 12h caps at 640K anyway; SeaBIOS's EBDA makes it 639K)
make run-720  # same, off the 720KB pair (os8088-720.img + apps720.img)
make test     # boot headless with QMP socket at build/qmp.sock for scripted testing
make test ADLIB=1 # ...with an emulated AdLib at 388h, so the sound DRIVER
              # (SPEC.md 51.4) has something to attach to. SB16=1 likewise.
              # QEMU HAS both cards; the two gate packages verify them
              # mechanically (docs/TESTING.md). Sound is NOT 86Box-only.
make test HDD=40 # ...with a 40MB blank IDE disk at 1F0h, for the hard-disk
              # DRIVER (SPEC.md 52). Without one its probe correctly finds
              # nothing, which is the right answer and not the one you want to
              # be testing against - the ADLIB= reasoning exactly. The image
              # (build/hdd.img) is created once and KEPT, because partitioning
              # and formatting it is the thing under test; rm it to start over
make test-snd # make test + PC speaker captured to build/snd.wav; verify with
              # tools/sndcheck.py (note: the wav holds speaker-ON time only, not
              # wall time - a silent boot yields an empty capture, and QEMU leaves
              # the RIFF sizes zeroed, which sndcheck.py absorbs)
make debug    # boot QEMU halted, waiting for gdb on :1234
make xt       # boot 360KB images on an emulated IBM PC/XT in 86Box
make xt-640   # same XT with a full 640KB RAM (vm/xt640/86box.cfg)
make xt-cga      # XT + real CGA card, 256KB (vm/xt-cga)
make xt-hercules # XT + real Hercules card, 256KB (vm/xt-hercules)
make 286         # 86Box AT clone: 286 @ 12.5MHz, 1MB, VGA (vm/286)
make 386sx       # 86Box Shuttle HOT-304: 386SX @ 16MHz, 2MB, VGA (vm/386sx)
make 386         # 86Box Micronics: 386DX @ 25MHz, 2MB, VGA (vm/386dx)
make xt-sound    # ...the XT again with a Sound Blaster 2.0 in it (vm/xt-sound)
make 286-sound   # 286 + SB16 (vm/286-sound)
make 386-sound   # 386DX + SB16 (vm/386-sound)
make 486         # 86Box AMI 486 (SiS 471): 486DX2 @ 66MHz, 8MB, VGA, SB16 (vm/486)
make pentium     # 86Box ASUS P/I-P55TP4XE: Pentium 133, 16MB, VGA, SB16 (vm/pentium)
make bench    # build the testing apps in tests/ into build/bench.img and
              # bench360.img. ON DEMAND ONLY — `all` never builds tests/ and
              # nothing under it ships. Run one with
              # `make test TESTAPPS=build/bench.img` (docs/TESTING.md)
make comscan  # the SERIAL PORT SURVEY (tests/comscan) - the field diagnostic
              # for "the mouse was not detected on real hardware" (SPEC.md
              # 9.5). Builds build/comscan.img (360K, BOOTABLE - no DOS, no
              # os8088, no mouse needed, because the thing being diagnosed is
              # the mouse and anything you have to click is unreachable),
              # comscan144.img, and build/comscan.com for DOS - whose output
              # goes through int 21h, so `COMSCAN > COMSCAN.TXT` captures the
              # report to a file. It scans 3F8/2F8/3E8/2E8, including the two
              # the kernel never probes, and answers the one question no
              # emulator can: WHICH IRQ LINE the card actually drives
              # (docs/TESTING.md)
make field    # ...and the FIELD disks: herc, cga, cga720, flop1 and cqdiag,
              # BOOTABLE 360KB system disks with the benchmarks in their root.
              # Shaped by the machine the project is calibrated against
              # (docs/FIELD-MACHINES.md): it has ONE floppy drive, so a
              # benchmark on a second disk means a swap; and it holds a
              # Hercules AND a CGA permanently, so the CGA needs a kernel
              # told to ignore the Hercules — built in build/cgak/, never in
              # build/, where it would boot the wrong card for everyone.
              # EVERY one of them is DISKCNT=1 (SPEC.md 18.94.1) - there is no
              # dskdbg.img any more, because both reasons it was a disk of its
              # own expired: the counters cost the image 0 bytes (same
              # 142-sector rung) and 12 instructions per int 13h CALL against
              # a 65ms sector, and "the published word is an ABI that depends
              # on a knob" stopped being true when SPEC.md 57's registry made
              # the block findable BY TAG. A later change removed the reason
              # and nobody re-asked the question - worth knowing as a shape.
              # A BENCHMARK KERNEL IS NOT BOUND BY KERN_BUDGET (640KB on every
              # field machine); what it IS bound by is parity, because
              # `boot ticks` and every heap row measure the kernel that is
              # RUNNING. tools/fieldsize.py, run by `make field`, reports
              # whether the field kernel and the shipped one share a
              # KIMG_PARA rung - same rung, exactly comparable - and never
              # fails the build, since growing is allowed and only has to be
              # known about
              # --- THE DEFAULT. Everything above this line is the fallback ---
make marty    # the MARTYPC DEBUGGER (docs/MARTYPC-DEBUG.md): a remote debug
              # server bolted into MartyPC's headless frontend, pinned to one
              # upstream commit in tools/martypc/. Memory, registers, I/O
              # ports, breakpoints, single-step and cycle counts on a running
              # os8088 with NO code in the guest at all - it costs the guest
              # not one cycle and answers on a machine that has hard-frozen.
              # Needs cargo, and on Linux libudev-dev + pkg-config; build it
              # at the START of a session rather than when you first need it.
              #   python3 tools/os88marty.py 127.0.0.1:9001 verify
              # dumps KERNEL_SEG and diffs it against build/kernel.bin, which
              # is FIELD-MACHINES.md's self-validating dump as one command
              # Machines: os8088_5150_cga (default), _5150_herc, _5150_cga_gla,
              # _5150_sb (AdLib + Sound Blaster), _xt_vga (mode 12h),
              # _xt_vga_sb (that plus sound, and the one to run with --turbo:
              # 7.16MHz is the fastest MartyPC has, and the CGA PANICS there,
              # so a turbo machine is a VGA one - it is a CONTROL for "is this
              # cost CPU-bound", never a source of a PERFORMANCE.md number),
              # _xt_hdd (XT-IDE, SPEC.md 52's rung 0). Add
              # MARTYPC_WAV=/tmp/cap for one wav per sound source.
make clean
```

**MARTYPC IS WHAT YOU DEVELOP ON. QEMU IS A FALLBACK WITH A SHORT LIST, and
that is a REVERSAL of how this tree has worked.** Everything here was
QEMU-first for years, and QEMU is the emulator FURTHEST from the target: it
runs the guest at host speed on a CPU that is not an 8088, through SeaBIOS
rather than a period ROM, with no CGA and no Hercules card in it at all. It
is exact about how much WORK the guest does and it is not a machine - nearly
every entry in docs/FIELD-NOTES.md is something QEMU showed as fine. `make
marty` is a cycle-accurate 4.77MHz 8088 running a real period BIOS with a
debugger attached, and it agrees with the field machine to 0-4% on 45 of 47
gfxbench rows. **Use it for anything that runs on an 8088**, which is the
whole of this OS bar the 286/386 targets. **Screenshots, scripted input,
SOUND and VGA have all stopped being reasons to start QEMU** - each was one
once, and each is now a MartyPC command.

**The whole of QEMU's remaining list**, so that "a legitimate need" is a
thing you can check rather than a thing you can argue: **286/386** (86Box
covers these too and models the machine rather than just the CPU), the
**hard-disk driver's RUNG 1** (SPEC.md §52.1 - the IDE task file read
directly, gated on `CPU_286` because an 8088's `in ax, dx` loses the drive's
high byte, so MartyPC's 8088 can never clear that gate; **rung 0 is
MartyPC's**, `os8088_xt_hdd`, and it is the rung the target machine uses),
and SPEC.md §9.5's **COM2 / cross-wired-IRQ / modem** mouse cases (`MOUSEPORT=`
and a socket chardev - MartyPC can put its mouse on either port but the
cross-wired and modem cases are not built). That is the list. Speed of
typing is not on it, and neither is familiarity with QMP.

**One live caveat, and it should be deleted once it stops being true.** VGA
mode 12h on MartyPC is NEW - verified on the desktop, a Disk window and
Minesweeper's 16 colours, and *not yet* on Paint's canvas, SPEC.md §32's
back buffer, `gfx_line_runs`, or SPEC.md §53's Mode 13h/Mode X brackets. It
is the default for VGA work all the same, because the alternative is a
machine that is not an 8088 at all. But **a disagreement between MartyPC and
QEMU about a VGA pixel is not yet automatically MartyPC's win** - go and find
out which is right, and then delete this paragraph.
**CLICK WITH `tools/os88mouse.py`, NOT WITH `os88marty.py mouse` (SPEC.md
9.4.3).** This is the first thing to know about driving the UI here, and it
is written this high up because every agent so far has found it the
expensive way. `os88marty.py mouse` is RELATIVE - a real Microsoft packet
through the real UART, which is exactly what you want it to be - so aiming
at a control means dead reckoning from the kernel's edge clamp, and dead
reckoning DRIFTS: a packet is a signed byte per axis so a long move is
several of them, the UART is 1200 baud so anything sent under ~25ms apart
can be dropped, and the clamp silently eats overshoot. **The failure is
silent and it always looks like a bug in the thing you are testing** - the
click lands three pixels outside a 16px control, nothing happens, and the
session concludes the feature is broken. It has cost several sessions real
time.

`os88mouse.py` closes the loop instead: the kernel publishes a pointer to
`mouse_x` in the debug registry's `'MO'` block, so it READS where the cursor
actually is, sends the exact remaining delta, and re-reads until it agrees -
pixel-exact in about 0.8s, and it SAYS SO and exits non-zero when it cannot
converge rather than clicking into empty desktop.

```
python3 tools/os88mouse.py 127.0.0.1:9001 where          # (320,100) buttons 00
python3 tools/os88mouse.py 127.0.0.1:9001 click 445 153
python3 tools/os88mouse.py 127.0.0.1:9001 dblclick 150 90    # NOT two clicks
python3 tools/os88mouse.py 127.0.0.1:9001 menu 12 8 40 45    # press/drag/release
python3 tools/os88mouse.py 127.0.0.1:9001 drag 200 78 200 120
```

**`dblclick` is a verb of its own and two `click`s are NOT a double-click** -
the trap that has cost the most harness time here. `click` ends in a 1.5s
settle and every detector in the system (SPEC.md 22/26/38, `ui_tdbl`'s title
bar) compares the two presses' BIRTH TICKS against a **9-tick** window; take
the sleep out and the opposite trap closes, because packets sent faster than
the 1200-baud UART can carry them are **dropped** and the guest decodes one
press instead of two. Either way it sees a single click - a file row SELECTS
instead of launching, a title bar DRAGS instead of zooming - and nothing says
so, which is how "the double-click feature is broken" gets reported about a
working kernel. So `dblclick` proves all four button edges against the
published `mouse_btn` and measures the gap in the guest's own 18.2 Hz ticks
(`0040:006C`, the BIOS counter - the kernel's clock and the kernel's units,
not host timing). It prints the span and RAISES if an edge never arrived or
the window was missed; a healthy one reads 2-4 ticks. **And the debug server
takes ONE connection**: a script wanting the mouse driver and the framebuffer
must share it (`mo.m`), because a second `Marty` does not error - it hangs.

**`menu` is a separate verb because a menu cannot be opened with a click**:
`menu_track` draws the pull-down and then polls a level, so press-and-release
in place opens it and closes it in the same breath (SPEC.md 9.6.1's flashing
menu, seen from the harness side). It does NOT place the cursor by poking
`mouse_x` and must never learn how - that would skip the UART, `mou_isr` and
the packet decoder, which are the three things a scripted click exists to
drive. The packet still does all the work; the registry only reports where it
landed.

`os88marty.py key` enters the emulator's keyboard buffer so the guest sees a
keystroke through the 8255 and int 09h, and `mouse` builds a real Microsoft
3-byte packet and clocks it into the serial controller so mou_isr decodes it
- both drive MORE of the real path than a guest-side poke to [mouse_x] would,
which is why no debug module was written for it. And: `os88marty.py shot out.png`
reads the framebuffer out of VRAM and decodes SPEC.md 39.3's banked layout,
verified against QEMU's CGA at 60.0% lit on both. That route is CGA and
Hercules only - they are 1bpp so the bytes are the pixels, where mode 12h is
four planes behind the Graphics Controller and not flat-readable - and
`shot --rendered` is the one that covers everything, asking the CARD what it
rasterised instead of asking memory what is in it (colour, every mode, every
adapter, automatic on VGA; the two agree on 0 pixels of 128,000 on a CGA
desktop). **And VGA mode 12h works**, which these docs twice said it did not:
marty_core ships a register-level VGA whose `vga` feature is ON BY DEFAULT
and which rasterises 12h correctly, and the actual defect was one line in the
headless crate's Cargo.toml - `marty_frontend_common` taken with
`default-features = false` compiled out the arm that REQUESTS the VGA BIOS,
so the machine came up with a VGA card and no video BIOS and nothing said so.
Two things sent that diagnosis wrong and are worth recognising: the card's
`is_in_graphics_mode()` answers FALSE in mode 12h (a field initialised to
false and never assigned), and its framebuffer is four-bytes-per-pixel RGBA
where the others are one-byte indices - read wrongly it yields a
plausible-looking histogram rather than an error. `field_w`/`field_h` is the
honest question: 800x524 is mode 12h's raster. **And sound is all
three tiers now**: `MARTYPC_WAV=` captures one wav per source in the format
`tools/sndcheck.py` already parses, and the machine has a PC speaker, an OPL2
and a **Sound Blaster** - the last of which is OURS (`devices/sblaster.rs`,
in our patch: upstream has the OPL2 and the 8237 and no DSP), a **DSP 2.01**,
so os8088's driver takes the classic `0x48`+`0x1C` auto-init path where
QEMU's SB16 makes it take another. `tests/sbtest` answers 2.00 s at 1000.0 Hz
on both, and its underrun leg stops at exactly the granted 2,400 bytes.
**One trap in the input, and it does not announce itself**: MartyPC's mouse
scales deltas by 0.25 by default, so an unscaled move lands a QUARTER of the
way and every click misses in a way that reads as a broken hit-test - the
`mouse` command forces the scaler to 1.0 for exactly that reason, and there
is no TOML key for it. docs/TESTING.md is the full ordering;
docs/MARTYPC-DEBUG.md is the recipe.

**MARTYPC IS CYCLE ACCURATE AND IT IS NOT DISK ACCURATE. If a disk is in the
path, its timing is WRONG** - 30x fast on a 16KB read (0.27 s against the
5150's 8.07) and 17x fast on a boot (PERFORMANCE.md Set 11) - and that catches
plenty that is not obviously about disks: a boot time, a package launch, a
Tracker module load, a `SYSTEM.CFG` save. **Nor will it catch a disk
CORRECTNESS bug.** SPEC.md 18.91's `AL` bug is the worked example - the BIOS
moved nine sectors and answered `AL = 1`, the kernel believed it and re-read
the rest one at a time, and on the 5150 that was 148 sectors in 34 int 13h
calls for a 32-sector file. **The same binary on the same image under QEMU
moved 34 sectors in 6 calls**: correct, fast, and silent about the bug. The
boot sector carried the identical bug for as long again. An emulator's floppy
controller returns what its author believed the hardware returns; the hardware
is under no such obligation. For anything with a disk in it the instrument is
the 5150 and there is no substitute.

**The other debugger is SPEC.md 58's `DEBUG.DRV`, and it is the one that works
on real iron.** A loadable driver - not a `SERDBG=` kernel, because a knob
kernel is a different binary and the machine you debugged is then not the
machine that ships - owning COM4 at 2E8, because os8088 hooks the IRQ of every
UART answering at 3F8/2F8 and a monitor on 2F8 fights the mouse prober for its
own port. Not wanted by default, so a machine that never ticks it in the
Control Panel pays one `drv_tab` row and a file on the floppy. The two are
complementary: MartyPC costs the guest nothing and answers on a frozen
machine, DEBUG.DRV goes where no emulator can.

**Nothing in `build/` is tracked — never commit a binary.** `build/` is
gitignored outright and no artifact inside it is force-added: not the kernel,
not the boot sectors, not the six floppy images, not a driver's `.drv`, not a
package's `.bin`/`.o88`. A binary this tree produces belongs in a *release*,
where a version can be attached to it — a GitHub release and os8088.com, both
fed by `.claude/skills/release-os8088`, which builds fresh from a clean
checkout. Developers build from source; the toolchain is `nasm` + `python3`,
and anyone running `make run` already has the heavier dependency (QEMU)
installed.

This is a reversal, and the history is worth one paragraph so it is not undone
by a well-meaning "the images should be in the repo" commit. 41 artifacts used
to be force-added so a clone could boot with no toolchain. Nothing made them
follow a source change, so they went stale in silence, and `make check-images`
existed to catch that by building everything a second time and comparing byte
for byte. It caught real staleness — two "Rebuild the shipped images" commits,
and a merge that shipped a Paint two fixes out of date. The reason to stop was
not that the check failed but that the cache carried no information: the
toolchain is deterministic on purpose (`tools/os88disk.py` pins the volume
serial and every FAT timestamp), so `make` reproduces any of those bytes exactly
and a committed copy was a correctness obligation bought for nothing. That
determinism is still load-bearing — it is what lets anyone rebuild a released
image and get the same bytes — it just no longer has a target guarding it.
`check-images` is gone, along with its STALE/ORPHAN/SCRATCH failure class,
binary merge conflicts, and the sharpest trap of the three: QEMU mounts the
`.img` files **writable** and the OS writes to them, so any test that saved a
file or clicked a Control Panel setting used to dirty a shipped artifact. Those
images are scratch now, and a test may dirty them freely.

Two build knobs exist only for testing the video fallbacks (SPEC.md §39.9):

```
make test VIDEO=cga                    # force the CGA path on a VGA machine
make test VIDEO=herc HERCSEG=0x7000    # force Hercules, framebuffer in RAM
python3 tools/hercshot.py build/qmp.sock 0x70000 out.png   # LINEAR = HERCSEG*16
python3 tools/mouse.py --screen 720x348 build/qmp.sock ...  # MANDATORY here
# ...the whole recipe, and the four ways to get it silently wrong, are in
# docs/HERCULES-TESTING.md
```

A third does the same for the clock (SPEC.md §37.90) — QEMU has an MC146818 and
nothing else, so the other three rungs of the RTC ladder are unreachable without it:

```
make test RTC=bios     # int 1Ah instead of the chip
make test RTC=none     # no clock at all: the 4 July 2026 fallback
make test RTC=ns       # the MM58167 probe against a machine that has none -
                       # it must REJECT and boot, not hang or invent a clock
```

`RTC=` shares `VIDEO=`'s stamp file, so changing it rebuilds the kernel; the
shipped images are always built without either.

A fourth takes the floppy transfer back to one sector per int 13h (SPEC.md
§18.91/§18.92) — the A/B for a class of bug **only real hardware can judge**:

```
make FLOPPY1=1         # AL=1 again, in BOTH transfer loops
```

A fifth lies to the boot sector about how much RAM the machine has (SPEC.md
§2.7). The sector relocates itself to the top of conventional memory, and
QEMU answers 639KB whatever `-m` says — so the small-machine cases are
unreachable here without it:

```
make test RAMKB=128    # boot with the sector where a 128KB machine puts it
make test RAMKB=104    # ...and below the floor: it must print RAM and stop
```

It shares the `VIDEO=`/`RTC=` stamp, so changing it rebuilds the sector — and
without that nothing at all would rebuild, since the knob touches neither
`boot.asm` nor `kernel.bin`. It moves the SECTOR only: the kernel still reads
the real `int 12h` for its heap, so this is not a small-machine simulation.

**The boot sector batches too, and that is where the boot time is** (SPEC.md
§18.93). It read `AL = 1` — 131 sectors, one int 13h each, at PERFORMANCE.md's
the then-measured **238 ms per sector** (the `AL` bug; ~65 ms since), so
**over thirty seconds** of every boot, which
is the largest single cost in it. It installs §18.92's table too, into
**`0000:0580`** — above the BIOS data area and *below* `KERNEL_SEG`, so no heap
claim can reach it and nothing needs restoring at handoff — and with EOT = SPT
the **track bound IS the EOT bound**, so `read_run` needs no test of its own
and came out smaller than the version that read the ROM's EOT every call. It
stops at the track, the sectors wanted and the 64KB DMA page; simulated
exactly, the 131-sector kernel is **10 calls on 1.44MB and 16 on 360KB (8.2x)**,
so roughly 31 s becomes 4–5 s. Honouring the ROM's EOT instead gives 30 calls
and 6–9 s, because a 9-sector track then costs eight sectors and then the ninth
alone. The splash is ticked **once per run** — `spl_tick` takes an absolute
position, so the bar's arithmetic is untouched and only the repaints drop,
which is itself worth seconds on the target. **On the iron none of that
speedup exists**: A/B'd with `make FLOPPY1=1` on the 5150 the batched boot is
**715 ticks against 621 — 13% SLOWER** — and a 16KB file read 15% slower
(PERFORMANCE.md Part 9 Set 13, docs/FIELD-NOTES.md 7). The call counts are
exactly as predicted and the *time* is not, because "nine sectors a
revolution instead of one" is a claim about revolutions that drive,
controller or media does not honour. Both emulators show the predicted win
and neither models rotational latency, so neither can arbitrate. **Do not
cost a disk change against the 9x**. **The mechanism is now known and it is
ours** (Set 14): `sysbench` calls `int 13h` itself, and on that machine a
whole 9-sector track in ONE call is **384 ms — 1.92 revolutions, 11,985
bytes/second** (the media is 2:1 interleaved), the same nine sectors as nine
calls is **10.02 revolutions**, and os8088's own read is **1.34 revolutions
per sector**. So the drive, the controller, the interleave and the BIOS are
all exonerated — `int 13h track, 1 call` *is* the batched read done right and
it is **6.3x faster than `dsk_xfer` achieves**. Whatever §18.91 is issuing is
not reaching the hardware as multi-sector commands, and the next step is a
CALL counter (`DISKCNT` counts sectors). That also kills the "should the
default flip" question: both paths are doing the same thing at the hardware.
**SPEC.md §18.94 is the instrument that settles the last step**: `make
DISKCNT=1` publishes `dsk_xfer`'s counters at the fixed offset `0060:000E`,
so `sysbench` reports what one 16KB read actually issued — sectors, int 13h
calls, the longest run, controller resets — and times a raw `int 13h`
**through the kernel**, holding `sch_lock`, because calling it from a package
hard froze the 5150 (docs/FIELD-NOTES.md 10). It answered in one run (Set 15): the
5150 moves **148 sectors in 34 calls, longest run 9, no resets** for a 16KB
file that is **32 sectors**. So the splitter WORKS — 4.35 sectors a call, and
each call costs about what the BIOS charges — and **the fault is 4.6x the
traffic**, 116 sectors nobody asked for. The same binary on the same image
under QEMU moves 34 sectors in 6 calls with zero mounts, so the extra is
machine-dependent, which is why reading the source never found it. The target
is measured now too: DOS copying one 170KB file off that disk is **~13,390
B/s**, the BIOS's own one-call track read **11,570**, 2:1 interleave by
arithmetic 11,520 — three figures within 16%, against os8088's 1,912.
**And the (LBA, run) trace found it** (Set 16): the LBA advances by one while
the run counts down, 34 calls over 33 distinct LBAs with nothing read twice —
**`dsk_xfer` asked for nine sectors, the BIOS moved nine, and answered
`AL = 1`.** §18.91 believed it and re-asked for the rest one at a time, which
is why the data stayed correct, why nothing ever errored, and why **batching
measured SLOWER than no batching**: the same call count plus the run
arithmetic, for nothing. `CF = 0` is the contract and `AL` is not, so
`dsk_xfer` advances by the request now (`make DISKAL=1` restores the old
behaviour), and `sysbench` verifies `BENCH.DAT`'s contents — every byte of
sector *n* is *n* — because a floppy that got 5x faster and quietly wrong is
the worst available outcome. **Measured on the iron: 8.29 s → 2.09 s for a
16KB read, 1,912 → 7,457 bytes/second, 3.9x** (Set 17), and the BOOT **726
ticks → 181 — 39.88 s to 9.94, 4.0x** once the boot sector took the same fix
(Set 18). Still 1.55x short of
the BIOS's own 11,570, and §18.94's trace found a third of it: **the
directory sector was read TWICE**, `dskw_find` locating the entry and
`dskw_ent_load` re-reading the same sector to copy it out, at seven call
sites. A re-read of a sector that has just passed the head is a whole
revolution. `dskw_find` takes the entry while the buffer still holds it
(SPEC.md §18.4.3) — **6 int 13h calls → 5** on a 16KB read and **3 → 2** on a
small file, which is the bigger win in practice. Copying there rather than
tagging `dsk_secbuf` is what makes it safe: no window, so no invalidation
anyone can forget. What is left is structural — a run coalesces only to the
track and the DMA page, and a file's first cluster is rarely track-aligned. **And the verify row did not run in that
set**, because it was written inside the `DISKCNT=1` block and the field
boots the plain kernel: *a guard that only runs on the build you are not
shipping is not a guard.* It is unconditional now. **The BOOT SECTOR had the
same AL bug and was missed with it** — `read_run`'s `.done` believed AL too,
which is why `boot ticks` did not move while the file read got 4x faster; 140
sectors at a revolution each is 33 of the 40 seconds. Both loops read `CF`
now, under the one `DISKAL=1` knob. And **`make BOOTDIAG=1`** trades the
sector's `os8088: disk error` for int 13h's **status as two hex digits** —
the sector has four spare bytes, so it is a knob, and it is what turns "it
will not boot on that machine" into one boot instead of a bisect (0C = media
type unidentified, 04 = sector not found, 09 = DMA page crossed, 80 = no
answer).
- **A TEST package reads kernel state through the debug registry (SPEC.md
  §57), and shipped software never does.** One word at `0060:000E` names a
  list of `(tag, offset)` pairs ended by tag 0; the tag is two ASCII
  characters and is **also the block's own first word**, so a reader can check
  that the offset it followed landed where it meant to and a human reading
  `xp` over QMP sees `MO` and `DD` rather than counting words. It replaced a
  fixed word per instrument, which is what the tree had until the first
  paragraph filled up at three (`boot_ticks` 0x0C, mouse 0x06, disk 0x0E) and
  the fourth had nowhere to go. An API slot is the wrong answer twice: half of
  these are knob-built and a slot that exists in one build and not another is
  an ABI that depends on a knob (§20.8 rule 4), and a slot is a permanent
  promise where a debug block's whole value is that it changes with the code
  it describes. A reader that cannot find its block **says so and continues** —
  that is what lets one build of `sysbench` run on a plain kernel and a
  `DISKCNT=1` one.

**A multi-sector floppy read is judged by the BIOS, not by the emulator.**
int 1Eh is a far pointer to an 11-byte diskette parameter table whose byte 4
is **EOT**, and the IBM PC/XT ROM ships **EOT = 8** — a DOS 1.x number every
DOS overwrites at boot. A *single*-sector transfer never consults it, so it
was inert here for years; the BIOS issues READ DATA with the **multi-track
bit set**, so once §18.91 started batching, a run reaching sector 9 of a
9-sector track flipped to the other head and returned **head 1's sector 1**
— `CF = 0`, full count, wrong bytes. Every package loaded with correct
opening sectors, validated its header, drew its window and hard-froze on the
substituted code. SeaBIOS never reads the table, so QEMU cannot show any of
it, and the boot sector reads `AL = 1`, so the batching was the only
multi-sector int 13h in the system. `dsk_dpt_init` owns the table now
(copied from the ROM's and patched, because the other ten bytes are *this*
machine's drive timings) and `dsk_xfer` writes EOT = `[disk_spt]` before
every call. **The wrong diagnosis is worth knowing too**: a real BIOS *can*
return a short count where SeaBIOS never does, that fix is right and is
kept, and it changed nothing — a short read is the BIOS telling you it moved
less than you asked, and what was happening was the BIOS moving exactly what
it promised out of the wrong place. docs/FIELD-NOTES.md note 5.

`VIDEO=` is tracked by a stamp file, so changing it rebuilds the kernel — without that,
make sees an up-to-date `kernel.bin`, boots the previous adapter, and it reads exactly
like the probe being broken.

**Installing the toolchain in a fresh container (read this before fighting apt).**
`nasm` installs normally. `qemu-system-x86` does **not**: the package index
lists the `noble-updates` build, whose `.deb` 404s on `archive.ubuntu.com` and
then times out against `security.ubuntu.com`, so a plain
`apt-get install qemu-system-x86` burns several minutes and fails. Two things
fix it, and both are needed:

```
apt-get update                       # the shipped index is stale; this is slow
V='1:8.2.2+ds-0ubuntu1'              # the BASE noble version, not -updates
apt-get install -y --no-install-recommends \
        "qemu-system-x86=$V" "qemu-system-common=$V" "qemu-system-data=$V"
```

`-t noble` is **not** enough — it still resolves to the `-updates` version.
Pin all three packages explicitly. `--no-install-recommends` skips the
gstreamer/libcaca display extras, which 404 the same way and which a headless
`-display none` run never touches. If a previous attempt is wedged, clear
`/var/lib/dpkg/lock-frontend` and re-run `dpkg --configure -a` first, and do
not `pkill -f apt-get` from inside a Bash tool call — the pattern matches the
call's own shell and kills it.

**`libudev-dev` 404s the same way, and it bites FIRST** — it is `make marty`'s
dependency, the DEFAULT test target, the one this file says to build at the
start of a session, where QEMU is the fallback with a short list. MartyPC
depends on the `serialport` crate, whose build script hard-fails without
`libudev-dev` + `pkg-config`, so a fresh container's first `make marty` ends
in a cargo error several minutes in. Installing them out of the shipped index
fails the same way QEMU does — the index names
`libudev-dev_255.4-1ubuntu8.14`, which is gone from the pool:

```
apt-get update                       # ...and that is the WHOLE fix here
apt-get install -y --no-install-recommends libudev-dev pkg-config
```

**Do not carry the QEMU recipe's version pinning over to it.** Same symptom,
opposite cause: QEMU needs an OLDER version than the index names, because the
`-updates` build is the broken one; `libudev-dev` needs the NEWER one a
refreshed index names (`…8.16` today), because the stale entry is what has
been superseded. Pinning `libudev-dev` to the base version reinstates exactly
the 404 you are trying to escape. `pkg-config` installs normally either way.

Requires `nasm`, `python3`, and — per the rule above, MartyPC first —
`cargo`; `qemu-system-i386` only for the fallback's short list. No linker anywhere — everything is `nasm -f bin` flat binaries (deliberately, to avoid Apple's Mach-O-only toolchain).

There are no unit tests. **`docs/TESTING.md` is the matrix of WHICH TOOL to
reach for and what each can and cannot do**, with a verified recipe per
capability — read it before concluding anything is untestable here, and read
its ordering before defaulting to `make test`. The short version: **MartyPC
first** (`make marty`) for anything on an 8088 — all three adapters,
scripted input, screenshots and **sound**, every one of which used to be
QEMU's alone — then QEMU for 286/386, then 86Box for a machine that is not an
8088, then the 5150 for **anything with a disk in it**. Testing under QEMU is
boot `make test`, then drive it over QMP.

Its **"Modelling the old machine from a fast one"** section is the part that
has cost four bugs, and most of it is about QEMU: this container is ~1000x a
4.77MHz 8088, so every constant sized while looking at it encodes the wrong
range. MartyPC removes a good deal of that (a cycle-accurate 8088 does not
have a clock that tells you nothing) and removes **none** of it for the disk,
where its error is 30x and flattering. **FLICKER IS MEASURABLE NOW** and
PERFORMANCE.md Part 3.1 is the method: `os88marty.py flicker` samples the
card's RENDERED framebuffer once per displayed frame - which is exactly how
often an eye samples it, a CRT showing whatever the raster last read - and
counts the pixels whose value before an operation and after it are the SAME
but which showed something else in between. That is the double-draw flash as
arithmetic. A Disk window repaint flashes 1,963 pixels for 166 ms on CGA; an
idle desktop and a bare pointer move measure zero, which is what says the
instrument is not manufacturing defects. Three traps: inject the input while
PAUSED, check `settled`, and always read the BBOX - a count alone
misattributes, which is how 42 pixels of "text flash" turned out to be the
mouse pointer blinking under the gfx lock. Not on Hercules: MartyPC's MDA
does not rasterise graphics mode, so measure on CGA and reason across
(SPEC.md 39.5 is one renderer for both).

```
python3 tools/mouse.py build/qmp.sock click 180 150      # absolute mouse click
python3 tools/mouse.py build/qmp.sock to X Y / down / up      # for menus: position, press, drag (`to` while held), release
python3 tools/mouse.py --screen 640x200 build/qmp.sock ...    # MUST match the adapter (SPEC.md §39): the harness
                                                              # pins against the kernel's own edge clamp
python3 tools/qmp.py build/qmp.sock 'sendkey h'
python3 tools/qmp.py build/qmp.sock 'screendump /abs/path/shot.ppm'   # raw NetPBM, ABSOLUTE path
python3 tools/shot.py build/qmp.sock out.png [--crop X,Y,W,H] [--zoom N]  # ...or straight to PNG
python3 tools/qmp.py build/qmp.sock 'quit'
```

Testing quirks (learned the hard way):
- Never inject raw HMP `mouse_move` — QEMU's msmouse backend truncates large deltas (big negative deltas flip positive). Always go through `tools/mouse.py`, which chunks moves to ≤60px and derives absolute position by pinning against the kernel's edge clamp.
- Menus need press/move/up sequences (`mouse.py down` / `to` / `up`), not `click`.
- Double-clicks compare birth ticks with a 9-tick (~0.5s) window: two separate `mouse.py click` invocations are too slow. Position with `mouse.py to X Y`, then send both clicks over one QMP connection: `qmp.py build/qmp.sock 'mouse_button 1' 'sleep 0.08' 'mouse_button 0' 'sleep 0.12' 'mouse_button 1' 'sleep 0.08' 'mouse_button 0'`.
- Small changes (e.g. one revealed 16px Minesweeper cell) are easy to misread as "nothing happened" in a full 640x480 screendump — crop and zoom before concluding a click was lost. `tools/shot.py <sock> out.png --crop X,Y,W,H --zoom 6` is that in one command, and it also spares you `screendump`'s NetPBM: there is no Pillow in a fresh container and nothing else in the tree reads a `.ppm`. **Its Y is the HOST scanout's**, so on `VIDEO=cga` (dumped 640x400 — QEMU line-doubles 640x200) a crop's Y and H are twice the kernel's; VGA is 1:1. Hercules is not screendumpable at all — `tools/hercshot.py`, below.
- `mouse.py down X Y` / `up X Y` now goto-then-press (any other argument shape errors out); bare `down`/`up` still act at the CURRENT cursor position — historically `down X Y` silently ignored the coordinates, a footgun that read as a kernel bug.
- Unpaced `mouse_move`/`mouse_button` sequences over one QMP connection outrun the 1200-baud msmouse: the button packet is processed at a stale position and drags silently do nothing — interleave `'sleep 0.1'` (or more) between moves and presses.
- `mouse.py`'s derived absolute position can be 1–2px off after a run of moves. On narrow targets (the Disk window's 14px scroll bar) a click can silently land just outside the rect — aim at the visual center of the glyph, and when a click "does nothing", screendump and check where the drawn cursor actually sits before suspecting the hit-test.
- Run `tools/sndcheck.py` only after QMP `quit` — a still-running QEMU's wav capture is partial and under-reports duration (and quitting with an SB stream underrun-paused flushes a residual ~20 ms blip at the file's very end; see docs/SOUND-PLAN.md Phase 4).
- **QEMU mounts `build/apps.img` writable, and the OS writes to it.** Any test that saves a file, makes a folder or deletes one leaves those changes on the image. It is untracked scratch, so this no longer dirties anything shipped — but it does *persist*, and `make` will not undo it: the image is newer than every `.o88`, so it is skipped, and the next boot still shows your scratch files. `rm -f build/apps.img build/apps360.img build/apps720.img && make` is the reset. Do it before a run whose starting state matters, and before cutting a release.
- **A previous session's QEMU may still be running.** `make test` then fails with `cannot create PID file`, but the stale instance keeps answering on `build/qmp.sock` — so every screendump succeeds and shows the OLD kernel, which reads exactly like a change that did nothing. `make test` prints the error; if it scrolls past, `ps aux | grep qemu-system` and compare its start time against `build/kernel.bin`'s mtime.
- To measure drawing work rather than guess at it, drop a counter in the kernel (`inc word [cs:dbg_x]` at the top of `font_char` / `gfx_fill`, with the `dw 0` in `.text` so it has a fixed offset), get that offset from `nasm ... -l /tmp/k.lst`, and read it over QMP with `xp /2xh 0x<KERNEL_SEG*16 + offset>` — HMP's `w` is 4 bytes, so `h` is what you want for a word. Editing any include *before* `font.inc` moves the offset, so re-derive it after every rebuild.
- **QEMU emulates no CGA and no Hercules card** — only VGA-class devices. `make test VIDEO=cga` works because SeaVGABIOS's `int 10h AX=0006h` is a byte-exact CGA framebuffer, but it never exercises the detection probe. **Hercules IS automatable under QEMU** — `docs/HERCULES-TESTING.md` is the recipe, and it is worth reading before you conclude otherwise, because the three ways of getting it wrong all produce a black image or a machine that ignores every click rather than an error. In short: `make test VIDEO=herc HERCSEG=0x7000`, then `python3 tools/hercshot.py build/qmp.sock 0x70000 out.png` (**`HERCSEG` is a segment, hercshot takes the LINEAR address** — that extra zero is the commonest mistake), and drive it with `tools/mouse.py --screen 720x348`. A QMP `screendump` shows you the *VGA* device and will never show a Hercules pixel; it does not error, which is how "Hercules mode doesn't work" gets concluded from one screenshot. What is genuinely out of reach is only the detection probe and the 6845 programming — `make xt-hercules` is the test for those two.
- `tools/mouse.py` paces its moves explicitly (one connection, `sleep` between packets) because the msmouse backend runs at 1200 baud and drops a move whose predecessor is still in flight. On a fast host the old one-process-per-move spacing was not enough, and the symptom is a cursor that never moves while every screendump still looks plausible.
- Only QEMU is routinely verified. `vm/xt/86box.cfg` keys are best-effort guesses and 86Box rewrites its own preference keys on exit (harmless drift — except that it silently clamps `mem_size` to the machine's maximum: `ibmxt` caps at 256K, which is why `vm/xt640` uses `ibmxt86`, the 1986 board revision; the same trap rules out `ibmat` for the 1MB 286, which 86Box clamps to 512K). The cheap way to test a candidate machine without booting it: launch 86Box on a throwaway copy of the config, `kill -TERM` it, and read the config back — 86Box rewrites it on exit with whatever it actually accepted.
- The AT-class targets (`286`, `386sx`, `386`) boot the **1.44MB** images, not the 360KB ones, and they have a CMOS the XT does not: on a fresh `vm/<machine>/nvr/` the BIOS stops at its setup screen and wants "EXIT FOR BOOT" picked once. That is a one-time cost per VM directory, not a failure.
- 86Box's `wp://` prefix on an `fdd_0N_fn` path mounts that floppy **write-protected**, and int 13h then answers status 03h — which the OS faithfully reports as "Write protected" (`FERR_WPROT`). **Neither floppy carries it any more, on any of the ten machines.** The data floppy lost it when SPEC.md §18.4 writes arrived; the boot floppy kept it on the reasoning that the system disk had no valid BPB and so could never be written anyway, and that stopped being true at SPEC.md §19.3 — it is a FAT12 volume with `SYSTEM.CFG` in its root, so protected, every Control Panel setting silently failed to survive a reboot. `make xt` and friends strip the prefix off **both** keys at launch (`UNPROTECT`), because 86Box rewrites its own config on exit and has twice put it back. If saving to A: or B: starts failing on 86Box, check this before suspecting `diskw.inc`.
- **The system disk carries the settings, which is why it stopped being protected.** Since it became a FAT12 volume (SPEC.md §19.3), `SYSTEM.CFG` in its root is where the whole Control Panel lives, and it is rewritten when the panel is CLOSED — `cp_flush_close`, one floppy write per session rather than one per click (SPEC.md §31.8). **No Control Panel page writes on a click, and a new one must not either — this is a rule, not a default.** Every setting added since has looked like the one that is different and none has been: a `SYSTEM.CFG` write is *2+ seconds of completely frozen UI* on the floor machine (mount, data, FAT, directory, FAT, remount, all under the gfx lock with the cursor stopped) landing in the middle of a click, and what the setting *does* already happened on the spot — only the record of it waits. A page sets `[cp_wdirty]` and returns; `cp_flush_close_x` is the only caller of `cp_flush_x`. The Drivers page is the one that most looks like an exception and is not, which also means **a test that ticks a driver and then resets from outside measures nothing**. (**There are no exceptions left, a driver's own page included.** The hard disk's Mount and Unmount were the last — the driver's `OSAPI_DRV_CFG` verb 2 rather than this mechanism, which is exactly how they escaped the rule — and that verb is retired: SPEC.md §51.9/§52.6. It also cost what nobody had noticed, because the write remounts A: to reach the file: clicking Mount made C: current and then silently made A: current again. `cp_flush` now has no `.text` thunk at all, so the rule is the build's rather than the reader's.) **Minimizing is not closing** and deliberately does not write, and neither does a hard reset from outside — so a persistence test has to click the close box on the LEFT of the title bar and let the OS run. Write-protected, those writes fail and **nothing persists across a reboot** — the driver list, the sound route, the clock options and the back-buffer setting all come back at their defaults. That is not a bug in `ctrl.inc`. The 86Box profiles no longer protect the boot floppy for exactly this reason, so persistence now works there as it does under QEMU — **at the price QEMU already charged**: a machine that writes its settings changes `build/os8088.img`, which is untracked scratch but persists across boots, so `rm -f build/os8088.img build/os8088-720.img build/os8088-360.img && make` is the reset after any 86Box session that touched the Control Panel and before a run whose starting state matters.

## Architecture

### Hard rules (from SPEC.md §1 — these break silently if violated)

- **Three video adapters, one binary (SPEC.md §39).** VGA 640x480x16 planar, else
  Hercules 720x348 mono, else CGA 640x200 mono, probed at boot by `kernel/viddet.inc`.
  **`SCREEN_W`/`SCREEN_H`/`ROW_BYTES` are VGA reference values, not the truth** — the live
  screen is `[vid_w]`/`[vid_h]`/`[vid_stride]` and the derived words in §39.2. New code that
  clips, centres or anchors to a screen edge must read those, or it is wrong on two adapters
  out of three.
- **…and the machine may have MORE THAN ONE, and may be moved between them
  while it runs (SPEC.md §39.11, `kernel/vidsel.inc`).** `vid_detect` answers
  *which adapter am I driving*; `[vid_avail]` answers *which ones could I
  drive*, and on the machine this project is calibrated against — a 5150 with
  a Hercules AND a CGA, each on its own monitor — that is two. The Control
  Panel's **Display** page (§31.10) lists them and its **Activate Mode**
  button switches there and then; the old Display page, which was always
  about double buffering, is called **Buffer** now (§31.3). Four things are
  easy to undo. **`vid_probe_avail` runs from `kmain` AFTER the mode is set**,
  because a VGA in mode 12h decodes A000 only and so leaves B000/B800 free for
  a second card to answer at — probed earlier, a VGA whose BIOS came up in
  mono text answers at B000 *as itself*. **Memory answering at B800 is not a
  CGA**: a Hercules has 64KB in two pages and page 1 IS B8000, so
  `vid_cga_alias` writes at B000:8000 and looks for it at B800:0000 — MartyPC's
  MDA decodes the whole 64KB whatever 3BFh says and reported a CGA on a machine
  that has only a mono card. **`vid_switch` drops the back buffer and spends
  the cursor's deferred hide BEFORE the geometry moves**, or `gfx_unlock`
  restores a save-under through the new addressing and smears the arrow
  permanently; and it re-runs `desk_rowcalc` and `wm_refit`, the two things
  `vid_apply` does not own. **The caller owes the repaint** — the panel posts
  `[cp_dirty]`, `drv_boot` redraws the splash — because `vid_setmode` clears
  the framebuffer and the two callers want different things put back.
  `SYSTEM.CFG`'s `VM` key remembers the choice and `vid_switch` REFUSES a card
  the machine does not have, so a settings disk carried between machines
  cannot boot one to a dead monitor. **One adapter is not a choice, so the
  page is not drawn at all** (§31.10.1) — and the Display item is LAST in
  `cp_items` precisely so that hiding it is a shorter list rather than a
  remapping every row below it would have to agree about. `[cp_nst]`, not
  `CP_ITEMS`, is §31.9's static/driver boundary now; miss one of those sites
  and a loaded driver's row dispatches as the Display page. **And the card
  the machine LEAVES is darked** (§39.11.4), but only when the two kinds are
  two cards — on a VGA the CGA row IS the VGA doing mode 6, so blanking "the
  outgoing card" there would blank the one being programmed.
- **8086 only.** `kernel.asm` opens with `cpu 8086` and the build uses `-w+error`, so NASM rejects anything newer: no `pusha`, no `push imm`, no `shl reg, imm` other than 1 (use CL), no `movzx`, no 32-bit registers.
- **Near model — for the kernel.** CS = DS = `KERNEL_SEG` (0x0060) for all kernel code and every task; **SS = `LOW_SEG`**, because every task stack lives outside the kernel's own segment (just above it). **Every** inter-module call in the kernel is near — there is no far code and no second code segment (SPEC.md §33). ES is scratch but must be restored unless documented. **SS ≠ DS means `[bp+disp]` addresses SS** — code holding a kernel pointer in BP needs `[ds:bp+…]`. **A package owns its own segment** (SPEC.md §20.1), so every crossing of that boundary is a far call in one direction and a dispatcher call in the other; see "Packages own a segment" below.
- **Register discipline.** Every public routine preserves all registers except documented outputs. ISRs push DS/ES, load DS = KERNEL_SEG, `cld` before string ops. Critical sections use `pushf`/`cli` … `popf`, never `cli` … `sti`.
- **Section discipline.** Four sections, all declared with their attributes at the top of `kernel.asm`; modules switch with a bare `section <name>` and **must switch back to `section .text` before the file ends**, or the next include's code silently lands in the wrong one. `-w+error` turns the tell-tale warning into a build failure.
  - `.text` — kernel image, `KERNEL_SEG`.
  - `.bss` — kernel scratch. Free on disk with `-f bin`.
  - `.lowbss` — task stacks + disk buffers, in `LOW_SEG` just **above** the kernel image. Reached through SS or ES, **never DS** (SPEC.md §2.1).
- **Label hygiene.** One flat namespace; every module-internal label carries its module prefix (`vga_`, `mou_`, `sch_`, `wm_`, `inst_`, `menu_`, `ui_`, `dsk_`, `dskw_`, `ld_`, `fm_`, `ico_`, `desk_`, `dock_`, …) or is a NASM local label.
- **Greying a control follows SPEC.md §47 — read it before disabling anything.**
  Seven binding rules. The load-bearing one is rule 1: **disabled is a FLAG,
  not a colour** — `call <ok-test>` then `call gfx_pen_cf`, which sets
  `[gfx_color]` = `CDGRAY` and `[gfx_dis]` together (`gfx_pen_dis` /
  `gfx_pen_live` for the unconditional cases, and `gfx_unlock` clears the flag
  like it clears the clip region). The flag exists because on mono grey text
  rounds to *black* — a disabled label was pixel-identical to a live one — so
  `font_ink` masks a flagged glyph to a checkerboard, the 1bpp Macintosh's
  greyed-out menu item. Keying that off the colour instead caught Minesweeper's
  dark-grey 8, which is why it is a flag. The rest in one line
  each: grey **the whole control** — ring, box, frame, icon *and* label, never
  just the caption; one predicate shared by the greying, the click refusal and
  the explanation; grey a **fact** (no hardware, wrong adapter) and never a
  guess — if the only test is doing the thing, do it and report; a greyed
  control explains itself, so a refused click on it says nothing more; every
  partial redraw path applies the same pen as the full paint; and words like
  `'Save Gif (NoRam)'` are still worth adding, but they now say *why not*
  rather than *whether*. **A greying change is not done until it has been looked
  at on a 1bpp adapter** — the two mono adapters differ from VGA in kind, not
  just in depth, and a glyph there is a checkerboard while a ring is dotted.
  **Dithered text is the answer on mono and not a compromise** — go and look at
  the Display page's `Double Buffered` on CGA — and a claim that mono cannot
  show greyed text is a misconception this tree carried in six places at once.
  **A package sets the pen with `OSAPI_GFX_PEN` (slot 0x0310)**, never with
  `OSAPI_SET_COLOR`: the colour alone leaves the flag clear, which is a dotted
  frame around a solid-black caption — rule 2's own failure, and what every
  package here shipped until the slot was published.
- **A kernel notice names the thing that failed (SPEC.md §54.4.1).**
  `ui_note` takes the message, the line above it **and the window's title**
  from the caller. It used to bake the last two in as the Task Manager's,
  because `ui_tm_open` was its only caller — so the association open path
  reported "Cannot open the Task Manager" over a window titled *Task Manager*
  when a **document's** program was missing. A second caller is where a
  hard-coded string becomes a lie; the window is reused, so `W_TITLE` is
  restamped per call, before `wm_show`.
- **Memory budget — read `docs/KERNEL-MEMORY.md` before spending any.** Two guards bind the kernel, they bind *different* things, and they are named for what they bound rather than numbered (they were "guard 1" and "guard 2" until the numbering turned out to be why the distinction kept getting lost). **`KERN_BUDGET` — the FOOTPRINT**: the whole kernel — image, `.bss`, the cold segment, the FAT window, the disk buffers and every task stack — is one contiguous span from `KERNEL_SEG`, measured against it (84KB, with the kernel at 84,480 — **1,536 bytes spare**, one step UNDER the four the fifth move settled on as the standard, so the next feature has to ask; it has moved eleven times, every raise asked for and granted, and twice downward: the fifth to put the guard back within reach of ordinary growth after an optimisation pass, and the eleventh to hand back the step SPEC.md §53.6.1's removed XMS desktop stash had cost. The tenth granted 4KB in advance for incoming quality-of-life work and its terms said to give back whatever that work did not spend — the constant's own comment in `kernel.asm` is the history, and `docs/KERNEL-MEMORY.md` carries the bisect recipe rather than a figure, so the next author measures rather than trusts). **`KERN_BUDGET` can be raised again, and for one release it could not** — move 9 landed it exactly on guard 5's ceiling, because the kernel had to end below a boot sector nailed to linear 0x15000. The sector relocates to the **top of conventional RAM** now (SPEC.md §2.7: `int 12h`, its last byte on the machine's last byte), so it is above every kernel that could fit the machine at all and guard 5 is a statement about `MIN_RAM_KB` = 128 instead — a ceiling of 126,976, some 44.5KB above the budget. A further raise is therefore the same decision the earlier ones were, and **not** an invitation to take the slack. When the kernel approaches *that* ceiling the answer is two kernels off one tree, a full one and a minimum one, rather than a raise. **`KERN_CODE_MAX` — the SEGMENT**: `.text` + `.bss` must fit the kernel's own 64KB segment, which no budget and no conversation can raise — 16-bit offsets. Two mechanisms relieve `KERN_CODE_MAX` and neither relieves `KERN_BUDGET`: the **boot overlay** (`.ovl`, SPEC.md §2.5 — run-once init code landed in the FAT window and overwritten by the first mount, costing nothing at all) and the **cold segment** (`.cold`, SPEC.md §2.6 — resident code with a CS of its own, DS still `KERNEL_SEG`; it holds 20.1KB now — the five file modules and the Control Panel, and since §53.6.1's removal nothing else at all — which is 30% of the kernel's code). Moving a module cold to fix a *footprint* overrun is a no-op that looks like a fix, and because the two rungs it moves between round separately it usually costs a 512-byte step. The levers on the footprint itself are deleting kernel code, moving a feature out to a package (SPEC.md §28's precedent), and asking for the raise. A near call or branch crossing either boundary is a bug NASM cannot see; `tools/os88ovlchk.py`, run by `make`, refuses it. The menu save-under is a heap claim rather than a reservation. **`make` reports what a change cost** — `tools/kernsize.py` prints the per-section deltas (`.text`, `.bss`, `.cold`, `.lowbss`, `.ovl`), their sum, the 512-byte rung each rounds into with the slack left in it, and a loud line when a rung is CROSSED; `--modules` is the same measurement per `%include`, and it costs the shipped kernel nothing by construction rather than by claim — the markers go into a temporary copy and it refuses to report unless the instrumented and plain binaries are byte-identical; it never fails the build, and `--bless` records the new figures in docs/KERNEL-MEMORY.md, which is where the baseline lives so the document cannot go stale quietly. **Report both numbers, and do not call a change that crossed no rung "free"**: it cost the machine nothing *yet*, and it spent slack that belonged to the next feature — the doc's Accounting section is the rule. **Growing past the budget is a decision to take with whoever asked for the feature, not a build fix.** The heap starts where *this build's* kernel ends, so it moves whenever the kernel does. Task stacks are **256 bytes** with a `SCH_MAGIC` canary at the bottom of every slice, checked at each switch away — an overrun halts in `sch_stkdie` instead of corrupting the next task's stack. Re-run the fill probe (KERNEL-MEMORY) before trusting a smaller number, and remember the probe under QEMU understates a real BIOS's interrupt stack use — SeaBIOS services its interrupts on an internal stack; an IBM BIOS lands them on whichever task stack is current.

### Concurrency (SPEC.md §7 — the crux)

Pre-emptive round-robin scheduling: the int 08h PIT hook chains the BIOS tick, saves the register frame on the task stack, swaps SP, and irets into the next ready task. Tasks are dynamic (MAX_TASKS=12): `task_spawn` takes an argument word (delivered in the task's DX) and returns the slot; a task terminates only via `task_exit` (self-exit; usually through `inst_task_die`), which frees the task slot and the instance record inside one IF=0 window. One drawing mutex (`gfx_lock`) guards all VGA access and hides the cursor; public drawing routines *assume* the caller holds it. Background tasks (Timer, Bounce instances, and a package's optional worker) re-check window visibility *under* the lock and then arm a clip region (below). The mouse ISR draws the cursor itself only when the lock is free, deferring to the next unlock otherwise. Task switching pauses during floppy transfers (the tick still runs — the motor needs it).

**What the lock COSTS is the cursor, not the mutex (SPEC.md §7.1).** The flag
is one byte and six instructions; everything else in `gfx_lock`/`gfx_unlock`
is the arrow — the lock erases it, the unlock saves what it will cover and
draws it again. A field log put the pair at **21.8% of a Missile Command
session with no pixel of the game in it** (PERFORMANCE.md Part 9 Set 4), and
because it is entirely kernel-side, every app on every machine was paying it.
Three things were wrong with it and each is a rule now. **The arrow is 8x12
and the tables are 16x16** — every save, restore and draw pass walked the
whole cell, and `CUR_GW`/`CUR_GH` are *asserted against the bitmap at
assembly time* because both failures are invisible anywhere but on the glass.
**The width retires the cell's third framebuffer byte**: a 16-bit row with a
zero low byte cannot reach one however far it is shifted, so that byte was
always 0 and its branch never taken, once per row per pass. And **on a 1bpp
adapter the cursor is ONE pass, not three** — the framebuffer is the
renderer's own target there, so `cur_put_mono` reads the byte under the arrow,
banks it, ORs the white outline in, ANDs the black body out and writes it
back in one row loop; the masks compose, `(under | white) & ~black` being
exactly what two passes always produced. VGA's planar draw is fused the same
way — Set/Reset **off**, Bit Mask = the white row, CPU byte = `white & ~black`,
so a set bit is colour 15 and a clear bit colour 0 and both land in one store.
That one depends on **every black bit also being a white bit**, which is why
the two bitmaps are now one `CUR_ARROW` list expanded twice and asserted at
assembly time rather than two tables thirty lines apart. `cur_geom`'s answers are **banked
and replayed** by the erase rather than re-derived, because a save and a
restore that disagree by a byte smear the cursor across the screen. Measured
by `gfxbench`'s `GFX_UNLOCK+LOCK pair` row (which is measured *backwards* —
`OSAPI_GFX_LOCK` from a callback is a deadlock, unlock-then-lock is not):
**17.82 → 5.41** counts on Hercules and CGA alike, **1.54x** on VGA,
framebuffer byte-identical on all three at every cursor position. **And a MOVE writes every byte exactly
once** (SPEC.md 7.1.2): it used to be erase-then-draw, two walks, so every byte
in the overlap was written twice and the glass could catch the background in
between - which on a real Hercules reads as the whole arrow washing out to
white while the mouse moves (docs/FIELD-NOTES.md 6). The fix is NOT a union
walk and needs no gate on how far the cursor went: the two passes still walk
the old cell and the new cell, and each byte is written once because **pass 1
skips the bytes pass 2 will write** and **pass 2 takes their background from
the save buffer rather than the screen** - so non-overlapping cells degenerate
to the old behaviour by themselves. It costs a second 24-byte save buffer,
swapped by pointer so nothing is copied, and `cur_sptr` is in `.text` with a
real initialiser because `.bss` arrives zeroed and 0 there points at the API
jump table. Verified by a 37-move walk over texture (every column delta, every
shift phase, both clipped edges) returning to its start: **0 differing pixels**,
against **98** with the background source deliberately broken. What was deliberately
NOT done, and is written down in §7.1: a *lazy* hide, which would take the
untouched case to nothing but needs the check at every path that writes the
framebuffer — §11.3's "a primitive not on that list is a hole" with a worse
failure mode, since a missed one smears the cursor permanently rather than
clipping something short.

### The clip region (SPEC.md §11.3 — how a covered window keeps drawing)

`wm_obscured` answers a boolean, and every background painter used it as a veto: one covered pixel and the whole frame was skipped, because the `gfx_*` primitives take **absolute screen coordinates and clip only to the screen edge**, so a covered window that drew would paint over the window on top of it. `wm_clip_set` replaces the veto with a region — the window's content rect minus every visible window above it in `wm_zord`, drop shadows included, into a 16-rect list. While it is armed the seven clipped primitives draw only inside it — and a primitive that is *not* on that list is a hole, not a design decision: `gfx_fill_pat` was off it for as long as it existed, which let the Task Manager's memory map (claim bands, buffer texture, region patterns — nearly all of it) paint its full width across whatever window was on top. `gfx_blit4` and `gfx_scroll` are still off it, deliberately and documented in SPEC.md 11.3, because a blit cannot take a sub-rect without advancing its source to match.

Four things are load-bearing:

- **The hook is at the PUBLIC entry, above the `cmp byte [bb_on], 0` dispatch.** One implementation then covers the VRAM path, the back-buffer path, VGA and both mono adapters — because on mono the software renderer *is* the direct path (§39.5). Below the dispatch it would work on VGA and silently no-op on Hercules and CGA, which is the expected failure mode; `make test VIDEO=cga` and `tools/hercshot.py` are what catch it. Same reasoning that places `bb_mono_chk`.
- **`gfx_unlock` clears the clip.** The region is computed from `wm_zord` and the window rects, which the UI task mutates only under the lock, so it is valid for exactly one lock hold and meaningless after. Dying with the lock is also what keeps the drag outline and the menu highlights unclipped (rule 2) without either of them knowing the region exists.
- **`wm_paint_all` is never clipped.** It draws back to front and the painter's algorithm resolves overlap for free. Clipping is for asynchronous single-window drawing only.
- **Two primitives clip whole-shape, not per-pixel**: `font_char`'s 8x8 cell and `ico_core`'s icon body, via `wm_clip_test` — neither can draw half a shape, and both already skip one that would cross a *screen* edge. And `gfx_xor_rect` decomposes into four `gfx_xor_fill` strips first, because an outline is not the intersection of its bounding rect with anything.
- **The granularity rule, which is the sharp edge.** Fills clip per pixel and glyphs clip per whole cell, so **anything that erases a rect and then draws text into it must not let the two disagree**. Ungated, a window cut horizontally by another window's edge gets its visible rows white-filled and then no text back in them — it goes *blank*, not stale, and re-blanks on every update. Two ways out, both in the tree: erase per cell behind a `wm_clip_test` on that cell (`app_clk_render`), or gate the whole erase+draw pair on a `wm_clip_test` of the whole rect and skip both (`fr_status`).

  **The whole-rect gate is charged to the wrong thing when the cut is VERTICAL**, and the Task Manager is where that showed: a window overlapping the list's right-hand columns cuts exactly one glyph cell per row, and the gate threw away every row to protect it — so a partly-covered Task Manager stopped listing anything new, and an app launched while it was covered never appeared until something forced a full repaint. `tm_row_draw` is the answer: a row is split into `TM_NCHUNK` chunks of `TM_CHUNK` characters, and a chunk is the unit of "did this text change" (`tm_chunksum` against its own word in `tm_rowck`). **It is NOT the unit of "may this be drawn", and assuming it was cost five characters at a time** — for that the answer is still the 8x8 cell, because that is what `font_char` can draw or not draw. A whole-chunk clip test throws away all five cells to protect the one an edge crosses, and a package row is `' PAINT …'`, so the split falls `" PAIN"` | `"T …"` and a window edge in the second chunk erased the T and nothing else; it reads as letters going missing in arbitrary positions, sometimes several at once, and shows as *blank* rather than stale whenever the row is new. So a chunk the region cuts — and only that chunk — goes through `tm_chunk_cells`, which tests, erases and letters one cell at a time. A vertical edge then costs the one character it actually crosses; a horizontal cut still fails every cell and so still draws nothing, which is what the gate was right about. The cut chunk's check word is forgotten either way, because a chunk that took that path was by definition not drawn whole. **The content check comes first**, and getting that order wrong is a real regression on a 4.77MHz machine: an occluded Task Manager must cost a hash per chunk, not a redraw per row. The string is zero-padded to the full chunk span so every chunk hashes deterministically and a row that got *shorter* changes the chunk it lost its characters from. **The band is wider than its chunks at both ends**, the pen being inset from it: the last chunk's fill runs on to the band's right edge, and `tm_row_lead` erases the left inset — which is where the memory list's legend square lives (`rowx+6`, against a pen at `rowx+16`), so without it a row that went away left its square behind forever. `wm_clip_test` is API slot 0x0180 for exactly this. Solid-only drawing is unaffected — Bounce erases and redraws with `gfx_fill` at both ends.

Overflow (more than 16 rects) degrades to CF=1, "skip this frame" — exactly what `wm_obscured` used to say, so it cannot regress anything. `wm_obscured` stays, and `cp_tick` and `tm_update` still use it: it is the cheaper answer for a drawer that repaints its whole pane in one go.

### Coming to the front costs one window; going away costs a rectangle (SPEC.md §11.90/§11.91)

Neither `wm_show` nor `wm_front` calls `wm_paint_all`. **Coming to the front
reveals nothing** — the window moves up, so for every other window the covered
area can only grow — and the full pass was a whole-screen planar dither plus
every visible window's frame and `W_PAINT`, paid to raise one window. Both go
through `wm_raise`, which draws four things in order: the menu bar
(`menu_activate` just handed it over), the dock (the owning instance may be new,
and the *active* tile moves) — **and neither of those usually draws anything**,
because both are incremental now: `menu_draw_bar` is gated on `[menu_bdirty]`,
which only `menu_relayout` and a fullscreen/save-under overdraw set, and
`dock_paint` keys each tile on its icon plus live/minimized/active so a focus
change costs two tiles and a quiet desktop costs none — then **the outgoing
front window's title bar**
(`wm_draw_title` — the pinstripes and the two boxes belong to the frontmost
window alone), then this window, last and therefore on top.

How much of *this* window is drawn is the one thing the two entry points
disagree about. `wm_show` always draws it whole: a newly visible window has no
pixels on screen. `wm_front` asks **`wm_obscured` before `wm_lift`**, while the
z-order still says what was on top — nothing was, so only the pinstripes
changed, so only `wm_draw_title` runs. A click on a background window's title
bar is that case, and it now costs two title bars and the chrome. Raising a
window that is *already* frontmost repaints no window at all.

Three traps:

- **`wm_top` is read BEFORE the visible bit goes on** in `wm_show`. `wm_create`
  has already appended the new window to `wm_zord`, so once it is visible
  `wm_top` answers with *itself* and the outgoing front never loses its stripes.
- **A window over the dock costs a rectangle, not the screen.** The strip is
  drawn under windows, and `wm_fit` keeps a window above it but `ui_grow`'s
  clamp is looser, so a grown window can hang over it — where `dock_paint`
  would draw on top of a window instead of under it. That used to be
  a veto that sent the cheap path back to `wm_paint_all`, so **one** oversized
  window made every focus
  change, show and un-minimize a full-screen repaint. `wm_dock_under` owns it
  now: `dock_paint` reports in CF whether it drew anything, `wm_dock_clear`
  whether a window is on the strip, and only if both say yes does
  `wm_dmg_wins` — §11.91's mark-and-draw pass, factored out for this — put
  those windows back. **Fullscreen is no longer a veto either**: a window
  raised over a fullscreen one reveals nothing like any other, so `wm_raise`
  just skips the chrome (`wm_fs_vis`) rather than the caller repainting the
  screen to avoid drawing it, and `wm_paint_all` starts its walk AT the
  fullscreen window because everything below is covered by construction.
- **`wm_front` on a hidden window falls back** rather than draw a window that
  has no pixels on screen. `wm_show` is the entry point for that.

**Hiding, destroying and dragging do reveal — but only inside the rect the
window vacated**, and `wm_paint_dmg` is that argument (SPEC.md §11.91). It takes
an inclusive damage rect and repaints the desktop dither clipped to it, the
drive zones it touches, the chrome (always — a tile leaves, the focus cue moves,
the bar may lose its owner), and then the windows. `wm_hide` and `wm_destroy`
pass the window's frame rect; `ui_drag` passes the union of where the window was
and where it is. A window closing on the left of the screen no longer redraws a
window on the right.

Four things hold it up:

- **A window is marked if it overlaps the damage — *or* overlaps a window
  already marked below it.** The second half is not optional: a marked window is
  redrawn *whole*, so it would paint over anything it overlaps. Marking runs
  bottom-to-top over `wm_zord`, so one pass reaches the transitive closure. And
  nothing in that pass may keep a loop counter in a general register —
  `wm_win_rect` writes all four.
- **A touched drive zone is folded into the rect, not special-cased in the
  marking** (`desk_dmg_zones` grows the rect to it), because a zone is drawn
  whole and a window over it must therefore be redrawn. The **dock is not**:
  the strip is full width, so a rect grown to reach it is full width for the
  damage's whole height, which erased the drive icons out from under any
  window tall enough to touch the bottom of the screen. It is a per-window
  test in the marking pass instead — a window whose rect reaches
  `[vid_dock_y0]` is marked, and nothing else moves.
- **A wholly covered window is not drawn at all.** `wm_covered` is §11.3's
  region arithmetic seeded with the *frame* rect instead of the content rect;
  empty means every pixel it would write is written again by something above it.
  `wm_paint_all` uses it too. The visible consequence is that **`W_PAINT` does
  not run on a wholly covered window**, so a paint proc must be a repaint and
  nothing else. The overflow degradation is the *opposite* of `wm_clip_set`'s:
  more than 16 fragments means "not covered, draw it", because skipping on a
  maybe loses pixels. This is not the old `wm_obscured` veto coming back — a
  *partly* covered window is still redrawn in full.
- **Hiding the front window promotes the one underneath**, and the promotion is
  visible. After the marked windows are drawn, `wm_paint_dmg` re-asks `wm_top`
  and owes it one `wm_draw_title` if it was not redrawn in this pass. Forget it
  and the new front window sits there looking inactive until something else
  repaints the world.

An empty damage rect is legal and means "nothing was revealed, but the chrome
changed": `wm_destroy` passes one when the window was **already hidden**, which
is the second half of closing a task-owned app (the close box hides, the worker
destroys a moment later). That used to be a second whole-screen repaint for two
strips' worth of change.

The one consumer that had to follow is the file manager, and it got cheaper at
both ends. A window that posted a load has `'Loading...'` in its status line,
and nothing repaints it any more; `files_poster` arms `wm_clip_set` on that
window and calls **`fm_status_only`** — one *line*, not the window's whole
content. The double-click that posted the load does the same. Both fall back to
`fm_repaint` when `wm_clip_test` says a clip edge crosses the line, because a
fill clips per pixel and glyphs clip per cell, so the line would go blank rather
than stale (the granularity rule). `files_poster` also needs `fm_win_of`, the
reverse of `fm_vp_set`, because `[ld_pwin]` holds the poster's **state block**,
not its window — a distinction that silently draws a Disk window's contents
through a garbage rect if you miss it.

### The menu bar is three segments and the dock diffs its marks (SPEC.md §12.9/§30.3)

**Only the segment that changed may put pixels on a strip.** The menu bar
carries four things on four completely different cadences — a logo that
never changes, the active application's menus, a clock that changes once a
minute, and §12.8's file-activity widget — and they shared one flag, so any
of them moving redrew all of them as a full-width white fill followed by
every glyph being put back. Measured on a cycle-accurate 5150/CGA with
PERFORMANCE.md Part 3.1's instrument: switching application was **9 frames
flashing = 149 ms, worst 534 transient pixels** across the whole bar, and it
is **1 frame = 17 ms, worst 37 px — those 37 being the mouse arrow**.

The segments are `0..MENU_TXT_X0-1` (logo), `MENU_TXT_X0..[vid_clk_hx]-1`
(menus) and `[vid_clk_hx]..w-9` (clock); the widget BORROWS the right end of
the menus and gives it back through `menu_inval` rather than forcing the lot.
Both interior boundaries are glyph-cell boundaries — `[vid_clk_hx]` is
floored to a multiple of 8 now — because that is what earns `font_run`'s
single-pass path (§6.1), where a cell row is one store on 1bpp and the span
is never momentarily blank. The layout follows: `MENU_NAME_X` 38 → 40,
`MENU_TITLE_PAD` 12 → 16, a title's pen at `MB_XL + 8`. Tracker moving its
pattern columns to earn the same path is the precedent.

Five things are easy to undo. **`menu_bcell` is the composition buffer AND
the record of what is on the glass**, so composing IS the diff and no second
structure can fall out of step; the first and last differing cells bound one
`font_run`. **Spaces are content** — the segment is composed whole, out to
the clock, so a bar that lost a menu blanks those cells in the same opaque
pass that letters the rest, and there is no fill in the path at all.
**`[menu_bovr]` is the only flag that erases anything**, which is why
`menu_track` no longer ends in `menu_force` (it would redraw the logo on
every menu click — nothing there damages the bar: the highlight is XOR and
both droppers floor at `MBAR_H`) and why `fsx` now does. **The About cell is
composed FIRST and skipped in the loop**, because §12.7 appends it last and
positions it leftmost, and composition must run in x order. And **it is
sound only because the bar can never be covered** — windows clamp to
`y >= MBAR_H` — which is `fm_sel_bar`'s licence in another place.

**The dock is the same argument at the tile.** A focus change is its
commonest event and it cost two tiles *erased to white and rebuilt* — fill,
frame, two-pass masked icon blit, marks — to move one 1px rectangle. The
erase existed because "the marks are not nested", and the reason dissolves
once you notice **both marks are their own inverse**: active is a `gfx_frame`
toggled by `gfx_xor_rect`, minimized is already a `gfx_xor_fill`. So
`dock_paint` branches on `old XOR new` and **never touches the icon unless
the icon changed**. That needed the key rearranged — the rotated icon
pointer's top three bits used to land ON the mark bits, fine for *did
anything change* and useless for *what changed*, so they are folded into the
high byte instead; a live tile's key now always has bit 0 set, so 0 means
"no tile" by construction and the old collision bump is gone. `dock_force`
also gained a **span** (`dock_force_x`), because `wm_paint_dmg` forced all
640px and every tile for any damage that touched the strip.

**Verified by BYTE IDENTITY, not by looking**: `make REDRAWFULL=1` builds
the old paths as a reference kernel, and the same scripted session driven
through both must agree pixel for pixel — 15 steps on CGA (including
minimize, restore, a window dragged over the strip and back, and a package
load through the progress widget) and 10 on Hercules and VGA mode 12h, **0
differing pixels each**. Keep that knob working: "the picture is the same,
only the number of times it was drawn changed" is the whole claim, and a
screenshot of one build cannot check it.

### Retitling costs a strip, and the dock stopped being a trap (SPEC.md §11.92/§39.7)

A caption changes on an **event**, never on a paint — so a window knows what it
wants to be called *after* the frame carrying that caption has been drawn.
`wm_title_set` (API slot 0x0228) is the correction: BX = window, AX = the new
`W_TITLE` (or **0**, "the bytes it already names changed underneath it"), lock
held, and it draws `y .. y+TITLE_H-1` and **nothing else** — no content fill, no
`W_PAINT`, no other window. Three ways out, picked by the granularity rule:
nothing above it → `wm_draw_title`; wholly covered → draw nothing (answered
*before* `wm_clip_test`, which reads an empty list as "disarmed, draw freely");
anything in between → `wm_paint_dmg` over the strip, because a fill clips per
pixel and a glyph per cell and the caption would go blank rather than stale.

The file manager is the reference consumer and lost `[fm_full]` — a flag that
escalated its next repaint to the whole frame — for `[fm_tdirty]`, a **pointer**
to the window owing a caption. Deferred and a pointer because `fm_settitle`'s
callers disagree about the lock: `fm_go`/`fm_mount`/`fm_view` hold it,
`fm_kinit` runs before the window exists on screen, and `fmv_sync`'s
folder-vanished path arrives from `ld_run`, which holds none. `fm_title_flush`
spends it, and only `fm_repaint` and `files_poster` call that.

**`wm_fit` takes one pixel off both height clamps** (`dock_y0 - MBAR_H - 1` and
`dock_y0 - h - 1`). The drop shadow is on row `y+h` and `wm_dock_clear` tests
`y+h` with `jae`, so a frame that merely *reaches* the strip is already on it —
and every window later shown over that one pays a `wm_dock_under` pass. One
subtraction fixes every fixed-size template at once, which is why Solitaire,
Arkanoid and the Task Manager needed nothing beyond keeping their own derived
layouts in step. **`wm_dock_snap`** then handles what the user does by hand:
called by `ui_drag` and `ui_grow` after their own clamps, it moves a window
**up** off the strip, and only when both gates open — less than `DOCK_H`/2 rows
covered (past that it was deliberate; leave it and let `wm_dock_under` pay) and
`dock_y0 - 1 - h >= MBAR_H` (a window taller than the desktop band is left
completely alone, because Paint grown to nearly the whole screen is a legal
size). In `ui_grow` a snap moves the **origin**, which nothing else in a resize
does, so bank the old rect's last row before the call and union against it.

### A write marks the folder; the focus spends the mark (SPEC.md §22.8)

§22.1's rule is "paints read the cache, actions re-sync", and the **write**
side of it was never implemented: a window's cache is rebuilt by the file
manager's own operations and by nothing else, so a *package* writing through
the file API changed a folder up to four windows might be showing and none
was told. That cost the wrong FILE, not a stale display — a double-click
resolves the row against the cache and hands `loader_run` a directory INDEX,
which is then resolved against globals where a new name has sorted itself
into place (§19.4) and shifted everything after it, so the loader reads the
entry next door and says **Bad package** about a file nobody clicked
(docs/FIELD-NOTES.md 4).

A byte per window (`FS_DIRTY`) and two hooks. **`fmv_mark`, called from
`dskw_sync`** — the one routine a successful file operation passes through,
so no new operation can forget it — marks every window on
`([disk_drive], [dsk_cwd])`, which at that moment *is* the folder that
changed. **`fm_focus` spends the mark** when the window comes to the front,
because a re-list is a MOUNT and a window nobody is looking at should not pay
for one.

Four things are load-bearing:

- **The cache and the pixels are made current TOGETHER.** The flag is not a
  cache flag: a cache rebuilt without a repaint is the same bug with the
  stale half moved onto the glass, the rows naming one file while the
  hit-tester resolves another. So `fm_focus` takes AL = *who is going to
  draw*: `wm_raise` calls it **before it draws anything** with AL = 0 and
  takes CF = 1 as "draw this window whole" — so a raise puts the new listing
  up instead of drawing the old one and then the new — and the UI task's own
  pass, which has nobody to defer to, asks for AL = 1.
- **The UI pass is the catch-all, gated on one byte** (`[fm_fchk]`, set by
  `fmv_mark` and by `menu_activate`). It covers the two focus gains no raise
  reaches: the front window being dirtied under the pointer, and **promotion**
  — losing the front window hands the bar and the pinstripes to the window
  underneath (§11.91) without ever calling `wm_front`. Every step of that
  deferred chain names the NEXT step in its own "nothing to do" jump, so a
  step inserted in the middle is a step the jump above it has to be pointed
  at; getting that wrong leaves the whole catch-all unreachable and only the
  raise path working, which is a bug that half-works convincingly.
- **The mount is usually skipped.** `fmv_take` (the memory half of a re-list,
  factored out of `fmv_bcast` so the two cannot disagree) is taken when the
  globals already are this folder, are not `[dsk_lstale]` and came from a
  mount that worked. That used to be the common case *because* the write had
  just remounted the folder it wrote to — but the kernel had already paid for
  that mount whether or not the window was ever looked at. §18.4 stopped
  paying it, so this is usually a real `fmv_load` now: **one mount, moved
  from the write to the look.** A user who never focuses the window pays
  nothing; one who does pays what the write used to charge everybody.
- **Only something that makes cache AND pixels current may clear the flag.**
  `fm_focus` does; `fmv_reload_all` does (`fmv_repaint_all` is its other
  half); `fmv_bcast` clears it for the ACTING window only, because its caller
  repaints that one and never the siblings; `fmv_load` and `fmv_sync` clear
  nothing at all — `fmv_sync` re-lists a background window to act in its
  folder and draws no pixels. And `fm_kinit` clears it because a `KD_POOL`
  block is reused.

### A paste repaints the Disk windows, not the screen (SPEC.md §22.3)

`fmv_reload_all` moves every Disk window's cache and **`fmv_repaint_all`**
puts the pixels back. It used to be `wm_paint_all`, so a drag-and-drop
between two Disk windows repainted the desktop dither, the drive zones, the
chrome and every other app on screen. A paste **reveals nothing** — no window
moved, none was hidden, only contents changed — so what it owes is the
windows whose listings changed and whatever overlaps them: `wm_dmg_wins`,
§11.91's mark-and-draw pass on its own, the same one `wm_dock_under` borrows.
Measured on one dragged file: 238 fills and 245 glyphs down to 138 and 169,
and no `wm_paint_all` at all.

Two traps. It is **one call over the UNION** and not one call per window —
`wm_dmg_wins` redraws a marked window *whole*, so per-window two overlapping
Disk windows cost four window paints, which measured WORSE than the
`wm_paint_all` it replaced; inside one call the marking is transitive and
every window is painted once. And the windows are redrawn whole rather than
in place because of §11.3's granularity rule: `fm_repaint` is one big fill
plus ~40 `font_str`s, the fill clips per pixel and the glyphs per cell, so a
partly covered window would come back with blank rows where a clip edge
crossed it.

### Selecting a file costs two inverted bands (SPEC.md §22.2/§38.3)

The selection in a Disk window and in the Standard File dialog is an XOR
fill, and XOR is its own inverse — so moving it is "invert the band it is
leaving, invert the band it is arriving at" and **nothing else redraws**.
Both had been ending a row click in a full repaint (the Disk window's whole
content; the dialog's whole list plus its scroll bar), about 130 glyphs and
a dozen fills to move one strip — and the first click of a **double**-click
paid it too, which is what made a double-click flash. `fm_sel_bar` /
`fdlg_sel_bar` are that operation, factored out of the two painters so the
painter and the click path cannot disagree about which pixels a selected row
owns (the `fm_hit` argument again), and range-checking their own argument so
a caller may hand them a selection it has not looked at. A click on the row
**already** selected now draws nothing at all.

Four things are load-bearing:

- **Correct only because `W_ONCLICK` fires on the frontmost window alone**
  (SPEC.md §13). Nothing is on top of it, so what is on screen inside its
  content is exactly what the last paint put there, XOR included; inverting
  a stale band would produce something that was never drawn. §11.3's
  granularity rule does not apply either way — an inversion erases nothing,
  so there is no fill and no glyph to disagree.
- **The Disk window's one other change is the editor line.** `fm_edit_end`
  runs first and cancels a half-typed name, rewriting the status line, so
  `fm_onclick` banks `FS_EDIT` into `[fm_wased]` *before* ending it and falls
  back to `fm_repaint` when it was set.
- **The dialog's name box follows the selection**, so the question is not
  "did the selection move" but "did the box", and only the setter can answer
  it: `fdlg_setname` returns **CF = 1 when the text, the length or the caret
  changed** and `fdlg_pick` passes it through. That is what lets a second
  click on the same row draw nothing while still putting the file's name back
  over anything typed since — same behaviour, no repaint.
- **`fdlg_sel_bar` asks `fdlg_rows`, not `[fdlg_shown]`.** The latter is
  painter scratch, valid inside one draw and meaningless on a click.

The two modules do **not** share a row painter and should not be made to:
the Disk window is resizable with a runtime `fm_layout`, two view modes,
icons and a per-window heap cache, while the dialog is a fixed-size modal of
`equ`s that reads the global mount snapshot directly *because* it is modal
(SPEC.md §38.2) — the exact opposite rule. What they share is the smaller
true thing: the entry format (§19), `dsk_get_dir`, `fm_ultoa`, and the
one-place-for-geometry discipline that `fm_hit`, `fm_thumb`/`fdlg_thumb` and
now the two `*_sel_bar`s all follow.

### A scroll moves the rows and nothing else on the window (SPEC.md §22.11)

All eight ways to scroll a Disk window — the two arrow cells, the two halves
of the track, Up/Down/PgUp/PgDn — ended in `fm_repaint`, so moving a list
redrew the header, two framed buttons, every visible row, the whole scroll bar
and the status line. **Not one of those four can change when the view
scrolls**: the header counts files, the buttons are labels in fixed rects, and
the status line is §22.7's `Size … Free …` or a §22.9 verdict — questions about
the selection and the volume, which a scroll moves neither of. Measured on a
cycle-accurate 5150 with PERFORMANCE.md Part 3.1's flicker instrument, one row
cost **16 frames of visible redraw = 262 ms, 15 of them flashing, worst 2,772
transient pixels over the whole content**; it is **5 frames = 83 ms, 2
flashing, worst 320 px** over one row and the bar, and a scroll at an **end
stop** — which used to repaint the window to show the same pixels — now draws
**nothing at all**.

Three tiers behind one entry (`fm_scroll_by`, so the clamp, the tier choice and
the write-back cannot drift between the mouse and the keyboard):
`fm_scrollpaint` (list view, `|d| < fit`: one `gfx_scroll`, the `d` rows it
exposed, two XOR bands and the thumb), `fm_rows_only` (the grid, or a jump of a
windowful — the row band only, header and buttons and status line still
untouched), and `fm_repaint` when a clip region cuts the band, which is §11.3's
granularity rule taken exactly where `fm_status_only` takes it.

Four things are worth knowing before touching it. **The blit rounds INWARD**,
which is the opposite of Note Pad's §27.7.2 and for a reason: `NP_MARGIN` is 8
there so rounding `x1` down stays inside the content, while a Disk row's icon
starts *four* pixels in and rounding down would put the blit's left edge on the
window frame and then outside the window altogether. **The delta the drawing is
a function of is the one the CLAMP took**, not the one the click asked for.
**A scroll draws no text, so a status line owed elsewhere is still owed** —
`fm_onkey` had to start banking `[fm_statowed]` because "every path below draws
the line one way or another" stopped being true. And the bug this shipped
before an A/B diff caught it: **an icon does not erase what it lands on.**
`ico_core`'s white pass is the icon's own *silhouette* (`ico_app16`'s is a
diamond), not its 16x16 cell, so redrawing one leaves every pixel the previous
row's icon lit outside the new outline — a three-column stripe of stale icon
edge, on the three window positions in eight that reach it and no others. The
strip is erased first. Verified by driving 25 scroll captures through this
kernel and through one with `fm_scroll_by` stubbed to `stc`/`ret`: **0
differing pixels** on CGA at two byte phases, on Hercules and on VGA mode 12h.

### The mono adapters reuse the back-buffer renderer (SPEC.md §39)

There is **no second graphics driver**. `kernel/vgabb.inc` was written as a latch-free,
port-free *software* renderer over `vga12.inc`'s coordinate core, targeting a RAM back
buffer — and nothing in it cares that the target is RAM. Point it at the framebuffer
(`[vid_rseg]`), tell it there is one plane instead of four (`[vid_planes]`), and route its
row advances through `gfx_nextrow`, and it *is* the Hercules/CGA renderer. The planar bodies
in `vga12.inc` are simply unreachable on mono and keep their assembly-time constants.

Consequences that are easy to undo by accident:

- **`[bb_on]` means "use the software renderer"** — permanently 1 on mono. The narrower
  `[bb_dbl]` means "a back buffer is armed and must be flushed", and is what `gfx_flush`, the
  Control Panel and the Task Manager's RAM figures read. Conflating them makes a mono machine
  claim double buffering and bill 150KB it never allocated.
- **`gfx_rowbase` and `gfx_nextrow` read their parameters through `CS`, not `DS`.**
  `bb_xfer` runs with DS pointed at the framebuffer (save) or the caller's buffer (restore);
  through DS they would fetch framebuffer bytes as a scan-line stride.
- **`gfx_nextrow` touches DI and flags and nothing else.** Several callers are inner loops
  with no spare register and one is inside IRQ4.
- **The banked layout needs a bank's rows to stay inside its own 0x2000 window.** Hercules
  uses 7,830 of 8,192 and CGA 8,000. `viddet.inc` asserts it; a stride or height change
  breaks it silently.
- The cursor is the one path with no `bb_*` twin (its save-under bypasses the buffer by
  contract), so `cur_put_mono`/`cur_get_mono` are the only genuinely new renderer loops in
  the port — and being outside the `bb_*` family is exactly what let them be **fused**
  (SPEC.md §7.1): save, white pass and black pass are the same bytes in the same order, so
  on 1bpp they are one read-bank-paint-write loop rather than three walks.
- Colours reduce to black / white / a 50% dither (§39.4). The shipped apps' palettes were
  chosen so every distinction they carry in colour survives the reduction.

### Double buffering (SPEC.md §32 — conditional, VGA only)

Unavailable on a mono adapter by design: the renderer already writes the framebuffer
directly, so there is nothing to double, and `bb_init` refuses to set `[bb_avail]` there.

**Off by default, switched at runtime.** `bb_init` only probes int 12h and sets `bb_avail` if conventional RAM ≥ 500KB (500 not 512, so a real 512KB machine still qualifies after the BIOS takes its cut). `bb_on` starts 0, so every machine boots drawing straight to VRAM; the Control Panel's **Display** page (SPEC.md §31.3) flips it via `bb_set`, which seeds the buffer from VRAM (`bb_sync`, GC4 Read Map Select per plane) on the way in and flushes it on the way out. While on, every `gfx_*`/font/icon draw renders into a 4-plane back buffer — a 150KB heap claim (SPEC.md §50), not a pinned segment (`kernel/vgabb.inc`, software or/and/xor — RAM has no VGA latches) and `gfx_unlock` flushes the dirty rect to VRAM before the cursor reappears; `menu_track` flushes once for the pull-down because it draws while holding the lock. Below the floor `bb_avail` stays 0, the page says so and refuses the click, and a 256KB machine can never leave the VRAM path.

Two things keep it affordable, because the flush (VRAM) costs ~24× the render (RAM):

- **`[bb_mono]`** — all four planes hold identical bytes as long as everything is drawn in colour 0 or 15, which is the whole UI (its greys are 0/15 dither). While set, the flush copies *one* plane with Map Mask = 0Fh and the hardware fans it out: a quarter of the VRAM writes, and no mid-flush colour fringing. `bb_mono_chk` retires it one-way on the first other colour (a Minesweeper digit); the planes are always fully rendered, so the flush just reverts to four passes. It hangs off `gfx_fill`/`font_char` ahead of the `bb_on` dispatch, so it tracks colour even while buffering is off — `bb_set` can arm the buffer at any time and seeds it from VRAM.
- **Transient overlays never enter the back buffer.** The drag outline and the menu highlights are XOR overlays drawn and erased inside one held lock — the cursor's contract — so they call `vga_xor_rect_vram`/`vga_xor_fill_vram` direct, like the cursor calls `vga_save_vram`. Routed through the buffer, a 1px outline dirtied the whole window rect and flushed it twice per drag pass. The public `gfx_xor_*` still dispatch to the buffer: packages reach them through the API table and their output is persistent.

### Fullscreen exclusive — the app borrows the machine (SPEC.md §53, `kernel/fsx.inc`)

§11.2's fullscreen surface is a real window in the desktop's mode; fsx is the
other thing: `fsx_run` (slot 0x02C8) is a **bracket, not a latch** — called
from a window callback with the gfx lock held, it far-calls the app's
exclusive main through the window's own dispatcher and does not return until
that proc does, so **the kernel never runs while the video mode is foreign**,
by construction rather than by gates. While it runs, `sch_switch` passes only
the exclusive task, its `FSXF_KEEPWORKER` worker, and `TF_SERVICE` tasks (the
task record's old padding byte at offset 7 — set only by the driver-worker
cell, so a Sound Blaster stream keeps refilling mid-game); the held lock
keeps the cursor off; and **entry walks `inst_tab` calling
`snd_release_inst` on every live instance but the caller's** — a frozen
owner's duration-0 tone would otherwise ring all session AND hold the tone
channel's priority against the app (§48's permanent-refusal shape), and the
`[snd_gen]` bump is what makes the thaw safe. **`FSXF_FASTTICK` (SPEC.md
§53.2.1) is the second flag**, and it exists because a kept worker's turn
costs the drawing half a whole 55 ms quantum — the periodic no-row frame in
Tracker's field logs. It reprograms **PIT channel 0** so IRQ0 arrives three
times a tick; `sch_isr` divides, so only every third entry chains the BIOS,
bumps `[ticks]`, runs the wake scan and `snd_tick`, and **`[ticks]` does not
change rate** — `task_sleep`, every timeout, `fsx_wait`'s pacing and the BIOS
clock are untouched by construction (measured: 18.19/s at the desktop, in the
bracket and after it). It is scoped to the bracket because the eligible task
set there is small and known, and must not become a general fast scheduler.
Two things pay for it: `sch_account` no-ops itself while armed (its timestamp
is `[ticks]*65536 + phase` against a period the sub-tick divides — the gate is
*inside* `sch_account`, because `task_yield` and `task_cycles` reach it too),
and §34.4's speaker PWM pacer parks it across a clip for the same radix
reason. Two bugs it sprang, both of which assembled and booted: the arm left
the divisor's table index in the register the flag was stored from (N became
6, and the tick ran at half rate), and `mov al, <N>` clobbered the app's entry
offset, which `fsx_run` still owed `wm_pkgcall`. `fsx_mode` sets any of nine
`FSXM_*` modes the adapter's caps bit allows (`fsx_caps`: VGA 0x1EF, HERC
0x011, CGA 0x00F — Mode X included, 13h plus the canonical unchain/retime)
and fills the caller's 16-byte FSI block; **the kernel's `[vid_*]` live
block is never touched**, which is why restore is `vid_setmode` + drain
(keys + evq) + disarm + the desktop back — and the desktop back is **one
`wm_paint_all`, on every machine and every tier**. **There was an XMS
snapshot here and SPEC.md §53.6.1 is now the record of why it went**: on a
286+/VGA machine with a store, the desktop's four planes (150KB) were saved
at the first `fsx_mode` and written straight back at exit, which is correct
about the pixels and wrong about the desktop — a bracket takes real TIME, and
behind it the menu bar's clock moves, the Timer counts, a Bounce steps, and
the exclusive app's **own** window content is usually exactly what the
session was spent changing. The feature had already conceded that in
miniature and nobody read it as general: the stash path redrew the menu bar
afterwards *because the clock was stale*. `wm_paint_all` asks every window
what it looks like now; a snapshot answers with what it looked like before.
Removing it was worth 84 bytes of `.text`+`.bss` and 190 of `.cold` — enough
to drop the cold rung 40 → 39 × 512 — and `KERN_BUDGET`'s **eleventh move,
86,528 → 86,016**, the second downward one. (§22.9's status line spent a step
in the same round, so the tree stands at 84,480 with 1,536 spare; that is the
removal's delta, not the standing total.) Two things it did NOT take with
it: **§41 stays** (the `xm_*` slots are a published package ABI, §20.8 rule
4, read by the Task Manager and `sysbench` — this was one kernel-side
consumer of the store, not the store), and **§41.8's permission stands** —
`xm_copy` under the gfx lock is legal because a window callback is its
intended caller and always holds it; the old "never under the gfx lock" was
unenforced conservatism whose stated 286-CPU-reset reason did not survive
scrutiny, and the stash was merely the first thing to rely on the
correction. `fsx_wait` is the frame clock
AND the present (it runs `gfx_flush` while a back buffer is armed and the
mode unswitched — the bracket never unlocks, so it is the only flush a
buffered renderer gets); every retrace poll is `[ticks]`-bounded. Missile
Command is the reference consumer: its four content-space primitives grew
Mode X twins, `mc_track` answers 320x240 under `[mc_fsx]`, and the arcade
runs at its own raster ('m' or the Game menu; the item reads `Mode X (Vga)`
on 1bpp adapters — §47's say-why-not). Two traps already sprung:
`OSAPI_FONT_GLYPHS` answers **DX = the glyph table's segment** (LOW_SEG —
reading it through KERNEL_SEG renders deterministic mush), and a crosshair
XOR-shown on the exclusive surface must be forgotten (`[mc_chshown]`) before
the thawed worker "erases" it onto the desktop. **Tracker (§45) is the
`FSXF_KEEPWORKER | FSXF_FASTTICK` consumer** — its audio worker keeps feeding
the ring through the freeze while the bracket draws the FT2 screen (verified:
a Sound Blaster wav has real signal produced entirely inside the bracket),
and the sub-tick is that same worker seen from the drawing side: its turn
costs 18 ms instead of 55 — **and,
in XT mode, the reference consumer of a TEXT mode** (§53.4's bare contract,
§45.13): `FSXM_TEXT80`, `FSI_SEG` out of the block because it is B000 on
Hercules and B800 on the CGA/VGA family, cursor hidden with int 10h, and the
frame is a `rep movsw`. **Paint (§42.7) is the SAME-MODE consumer** — it sets no
mode at all, so it needs no caps bit and runs on all three adapters out of
one body, and what it borrows the machine for is the LOCK: `pt_wait` runs
once per mouse sample of every stroke and windowed it is an
unlock/yield/lock round trip. It is also the one that takes the bracket
**without** the §11.2 surface, and §42.7 has the measurement: for an app
whose repaint is expensive, §11.2-plus-bracket makes step 4's
`wm_paint_all` draw a window that is still `WF_FULL` and then repaints it
again as a window, so leaving cost 3 full-canvas draws where the bracket
alone costs 1. Inside, it draws its own XOR crosshair and polls its own
input into its own `W_ONCLICK`/`W_ONKEY`. `tests/fsxtest`
is the gate (docs/TESTING.md); `task_sleep` inside a bracket degenerates to
an immediate resume, so pacing is `fsx_wait`, never sleep.

### Instances (SPEC.md §29 — how apps live and die)

Everything running — built-in kind or loaded package — is a record in `kernel/instance.inc`'s `inst_tab` (12 × 32B). Boot is clean (no instances); menus call `app_launch` (new instance, or front the existing one at the kind's cap), the close box calls `app_close_win` (task-less: synchronous teardown; task-owned: die flag `I_STATE=2` + hide, the task tears down at next wake), and the title bar's right-hand minimize box hides to the dock (`kernel/dock.inc`, bottom strip rows 456..479, one tile per live instance, stable slot↔tile mapping). A tile carries two independent marks: **minimized** XOR-inverts its interior, **active** — the instance owning the frontmost visible window — doubles its border. Two different kinds of mark on purpose, and a heavier border is the one that survives the 1bpp reduction. `wm_owner[]` maps window slot → instance. The Task Manager lists *instances*, not tasks — one row per `inst_tab` slot plus a "System" row — because task-less apps (About, Disk, and any package that has not claimed a worker) only ever run inside window callbacks. Those callbacks are timed at the `W_PAINT`/`W_ONKEY`/`W_ONCLICK` dispatch sites and billed to `I_CYC` via `task_cycles`/`task_debit`, which *move* the cycles off the running task so the rows still add to one total.

A package may claim **one** worker task from a callback (`OSAPI_TASK_SPAWN`/`OSAPI_TASK_ALIVE`, SPEC.md §20.6 → `inst_pkg_spawn`/`inst_pkg_alive`) — the first time two packages can be pre-empted against each other, and the first time a package instance takes the *task-owned* close path instead of the synchronous one. The trap: a worker that returns or exits on its own leaks its instance record and its region for the session, because `app_close_win` then sets a die flag nobody ever reads. It must call `OSAPI_TASK_ALIVE` every loop, and that call is where it dies. Two kernel-side rules hold the feature up: `inst_pkg_spawn` fences the package's BX with an **ownership test** (the record must be a package whose own `[I_SPTR, I_SPTR+I_SIZE)` contains the entry in AX), because attaching a worker to a stranger's record puts *both* instances on the wrong teardown path; and `task_spawn` runs its slot scan and its `T_STATE` publish under one `cli`, because this is the first time two different tasks can spawn at once.

### The menu bar belongs to the active app (SPEC.md §12/§12.2/§12.3)

The bar is **chip menu → active application's name → that application's menus**, and only the chip (System) menu is fixed. `kernel/menu.inc`'s `menu_bar` is therefore a *runtime* table rebuilt by `menu_layout` every time the owner changes, not the static `menu_table` it used to be. Ownership is a **window**, `[menu_win]`, and the menus hang off the window record's new `W_MENUS` word (`WIN_SIZE` 18 → 20 — `wm_idx2ptr` multiplies by `WIN_SIZE` now instead of open-coding the stride, which is what broke the first time it changed).

Three one-line hooks move it, and nothing else in the kernel knows the bar exists: `wm_front` activates the window it raises (so launching, raising, un-minimizing and dock clicks all follow for free); the event ladder's window branch activates the clicked window too (a click on the *already* frontmost window never reaches `wm_front`, and the bar still has to follow); and `menu_check`, run at the top of every `menu_draw_bar`, hands the bar to `wm_top` the moment `[menu_win]` names a window that stopped being visible — one validation covering close, minimize and hide. It **promotes rather than reverting** because the title bar does: losing the front window promotes whatever was under it and `wm_paint_dmg` gives that window the pinstripes (§11.91), so a bar that fell back to Locator instead made the screen say two different things about which app is active. `wm_top` answers 0 when nothing visible is left, and 0 *is* Locator, so the old fallback is still the last rung. A deliberate switch to Locator (clicking the bare desktop) is sticky — `[menu_win]` = 0 leaves `menu_check` at its first test.

**The gfx lock's cursor hide is DEFERRED, and `wm_clip_set` is what spends it**
(SPEC.md 7.1.4). `gfx_lock` used to erase the arrow for the whole hold, so a
window refresh blinked the pointer off and on - super visible with the mouse
sitting still, and worst with the Task Manager, which refreshes twice a second
and takes a visible span on a 4.77MHz machine. Now the lock only PROMISES the
hide: `cur_unlazy` takes it unconditionally on every path that can draw
anywhere (GFXCLIP's unclipped branch, the off-clip-list primitives, the VRAM
XOR twins, `gfx_flush`, `gfx_save`/`restore`, `fsx_run`), and `cur_lazyck` -
called by `wm_clip_set` - takes it only if the cursor is REACHABLE, because an
armed region CONFINES the clipped primitives to it. **The default is hide; the
win is the exception**, and it is free for all twelve `wm_clip_set` callers.
Measured: 30 of 31 Task Manager refreshes kept the cursor up, **0** cell
violations. **The trap that cost the boot: `vid_setmode` must NOT call
`cur_unlazy`** - `viddet.inc` runs from the splash BEFORE the rest of the
kernel is off the floppy (SPEC.md 15.3), so a call from there into `mouse.inc`
executes whatever is not loaded yet: black screen, no clue. **And the trap
that cost the background tasks (SPEC.md 7.1.4.1): a promise that SURVIVES puts
a `cur_move` inside `gfx_unlock`**, and `cur_move` clobbers `ax bx cx dx si di
bp es` while `gfx_unlock`'s contract is *clobbers: flags*. `cur_lazyend` saved
AX and BX, so one mouse packet landing inside such a hold wrecked the caller's
BP and SI - which SPEC.md 14's background tasks hold *for the life of the
task*. Bounce then read `[ds:bp+I_WIN]` out of a wrecked BP, got a window whose
visible bit was clear and went blind for the session (ball stepping invisibly,
last square frozen on screen); the Timer stopped counting the same way. Nothing
faulted, nothing froze, and the task table showed both tasks waking on
schedule, so it read as *a worker is broken* rather than as a register bug -
279 corruptions in 279 `cur_move` calls, 0 after. **A routine reached from
`gfx_lock`/`gfx_unlock` inherits THEIR register contract, not its own**: the
pair sits under every drawing site in the machine and no call site can
compensate.

**A poll loop that drops the gfx lock must pace itself to the TICK** (SPEC.md
7.1.3) - and this one is also why the LAZY HIDE was dropped (7.1.1): a
measured 5,020 lock/unlock pairs across a session were ~4,900 THIS loop, so
pacing it left 120, of which 46 genuinely draw or move the mouse. The lazy
hide's own justification came from a measurement taken before its neighbour
was fixed; re-measured, it buys ~144 ms across several minutes for a check at
every framebuffer-writing path, a permanent-smear failure mode and a new
package ABI contract. **Re-measure before building an optimisation whose case
predates its neighbours' fixes.** `gfx_unlock`/`task_yield`/`gfx_lock` is the idiom for polling without
holding the mutex, and there are four loops of that shape - `ui_drag`'s
tracking pass, `ui_grow`'s and both of `fm_drag`'s. Three waited out the tick;
`fm_drag`'s `.wait`, which runs while the button is down and the pointer has
not yet moved far enough to be a drag, did not. It draws NOTHING, so every
round trip was a cursor erase and redraw for no reason: **20,761 pairs per
second of held button against 21** once paced. On the 5150 it cannot really
turn over that fast - each pair costs ~1.9 ms - so what it did instead was
spend the whole machine, with the pointer blinking under a held button. The
rule is not that the poll is expensive (it is a few compares) but that
**taking the lock back is**. `.wait`'s linger sits BETWEEN the unlock and the
lock rather than after both, unlike its siblings': they hold the lock through
their wait because an XOR outline is on screen and must stay lit, and `.wait`
has drawn nothing, so a free lock lets the mouse ISR move the cursor and
background tasks paint.

**The app's NAME is a menu, in every application, and its bottom item is
always `Close` (SPEC.md §12.7).** That cell used to exist only when a window
had registered an About handler, so most applications had a bare label where
their name is and no way at all to quit from the bar; now `[menu_win]` being
non-zero is the whole condition and `wm_about_get` decides only whether the
cell has one item or two (`About <Name>` above `Close`). `Close` is the
close box reached from the bar — `app_close_win` on `[menu_win]`, run inline
in `ui_dispatch` because that runs with **no lock held**. Three things hold
it up. `menu_abstr` (`'About '` + the name) is built on every relayout now
and not only in the About case, because the cell's *title* is
`menu_abstr + MENU_ABPFX_LEN` — the same bytes from the name onward, which
is what keeps both of the cell's strings kernel data and its `MB_SEG` 0
under a package. **Which item is `Close` is RECORDED** (`[menu_abclose]`)
and tested before the cell is routed to the application, because deriving it
from the item count at dispatch time is a second place that has to agree
with `menu_ab_cell` — and the failure is a package's About handler called
with an item index it was never written for. And Locator gets no such cell
at all: its About is the System menu's, its `Close Window` is already in its
File menu, and a Disk window — which does get one, labelled `Locator` —
closes that window, which is exactly what its close box does. No package
changed and no `.o88` was invalidated.

**A dock tile has a context menu, and its one item is the same `Close`
(SPEC.md §30.2).** `ui_rdown` used to answer a right-press with no window
under it by doing nothing at all; it reaches `dock_rclick` now — never
`dock_click`, which toggles minimize — while the bare desktop still gets
nothing, because activating Locator would move a focus the press did not
have to. Two things in it are worth knowing. `dock_hit` is `dock_click`'s
find lifted out whole and shared, so "which records have a tile" (§29.2
rule 3) has one answer rather than two that can drift. And **a record
pointer does not survive the tracker**: `menu_popup` holds the button and
yields, so a package worker can reach `inst_task_die` and free the very
record the menu is about — the SLOT is banked and re-resolved afterwards,
which is safe because nothing can refill it while the UI task is standing
inside `menu_drop`. The popup anchors at `[vid_dock_y0] - DOCK_CTX_H` rather
than at the press, or it lands on the tile it is asking about.

**Locator** is the kernel acting as an application (the Finder analogue): the desktop, the drive icons, the Disk browser (up to **four** windows, each on its own drive and folder) and the menus that launch everything else. It is not an instance — it is just the menu set the bar falls back to when no window owns it, and **clicking the bare desktop switches back to it** (the `.desk_icons` branch, before `desk_click`). `menu_loc_set` is an ordinary app menu set whose `AM_ONCMD` is 0, the one value reserved to mean *dispatched by the kernel*: `ui_dispatch` recognises it and rebuilds a `CMD_*` from `ui_loc_base` instead of calling through, which is how the old flat command dispatch survives intact behind the new (cell, item) return. `fm_kinit` points every Disk window at `fm_menus` — Locator's *second* set, same `AM_NAME` but a real `AM_ONCMD` — so the file browser reads as Locator's own window rather than an app called "Disk", and the bar carries File/Folder/View/Special while one of its windows is active.

For an application, the whole interface is `OSAPI_MENU_SET` plus the `OS88_MENUSET`/`OS88_MENU`/`OS88_MENUSET_END` macros in `apps/os88api.inc`. The command handler is **a window callback reached through the bar**: called on the UI task under the gfx lock, billed to the instance, same rules as `W_ONCLICK` — it may draw and may call the file API, must never take the lock, and **must repaint itself**, because the kernel does not repaint after it returns.

One trap the bar has already sprung once: **every string in a menu set is an offset in the owning window's segment**, so `menu_bar` carries a `MB_SEG` word *per cell* and `[menu_dseg]` names the dropped one. With a single "active app's segment" instead, the System menu's own items were read out of the package's segment and every one of them drew as `O8` — the first two bytes of the package header.

### The current directory belongs to the INSTANCE (SPEC.md §19.2.1)

`[disk_drive]`/`[dsk_cwd]` are one pair for the whole machine, and §19.2's
rule — every file name resolves in the current directory — made that pair
every *application's* idea of where its documents were as well. The
consequence was not a stale display but **a file written to the wrong folder
in silence**: Note Pad's Save As commits into `B:\DOCS`, the user opens a
Disk window on C:, and Note Pad's next plain Save resolves `NOTES.TXT` in
C:'s current directory and puts it there, with no error anywhere. Four
packages — Note Pad, Tracker, Paint, ArtfulType — each carried their own
copy of the same six-line bank-and-restore around it, which is the shape
that says the kernel owes the feature.

So an instance owns a directory: `inst_fdrv`/`inst_fcwd`, seeded at
`inst_alloc` from wherever the app was launched, and **`inst_vol_enter`
stands the machine in the calling instance's folder before the file API
resolves anything**. The globals still mean what they always meant — one
floppy, one drive, one mount snapshot — they are now *where the hardware
is*, not *where anybody thinks they are*. Six things hold it up. The switch
is at the API cell and nowhere else (`FILE_WRITE`/`READ`/`DELETE`/`RENAME`/
`DFREE`; the kernel's own `dskw_*` callers act for the machine and are
untouched, and **`FDLG_OPEN` uses the same name-staging stub and deliberately
does NOT get it**, because `fdlg_home_go` is what decides where a *dialog*
opens and a switch underneath would pre-empt it). **The compare is what
makes it free** — banked == current is six compares, and one app doing file
work never moves, so a mount is paid only when something really did move the
volume underneath it. It is a **quiet** mount (§18.9) whose `[dsk_lstale]`
debt is deliberately never collected, on `drv_vol_back`'s and `assoc_back`'s
terms. A failure needs no channel: `dsk_chdir_q`'s failure path shuts
`[dsk_mntok]`, so the operation refuses with `FERR_NODISK` — which is why
`inst_vol_enter` returns nothing at all, **not even CF**, and preserves every
register and the flags. `inst_caller` answers "who is asking" and is
SPEC.md §34.3's rule generalised rather than a second copy — `snd_req_inst`
is a jump to it. And **`OSAPI_FILE_HERE`/`GOTO` answer for the instance
now**, which turns them from a requirement into a convenience: all four
existing callers keep working, their `goto` back becoming a compare that
finds the volume already there. Redundant, not wrong — the only way to
retire a duty a published slot handed to applications.

### One read, one write, and no 64KB ceiling on either (SPEC.md §18.4.1)

There were three routines and there are two. `dskw_read` and `dskw_write` are
the **whole** read/write surface — for the kernel and for packages, for a
32-byte settings file and for a 116KB module — and the destination or source
is `ES:BX` with a **32-bit** count in `DX:CX` (the read answers in `DX:AX`).

The mechanism is one small routine, `dskw_norm`, called once at the top of
each pipeline: it folds the paragraph part of BX into ES, leaving an offset of
0..15, and the transfer loop then holds that offset and advances the
**segment** by 32 paragraphs per sector. An offset under 16 plus 512 cannot
carry, so the 16-bit horizon is unreachable by construction — and it costs
nothing, because `dsk_xfer` was already looping one int 13h per sector and
walking BX itself. The whole 16-bit read path and both `readbig` bodies went;
the change is net smaller than what it replaced.

Four things about it are easy to undo:

- **The destination stays `ES:BX`, deliberately, and that is not laziness.**
  A base-segment-only contract would force every caller with a small fixed
  buffer to find a segment run for it — and the kernel has one it must not
  lose: `drv_cfg_load` reads `SYSTEM.CFG` into 64 bytes of `.bss` **at boot**,
  where a heap claim is a thing that can be refused. The superset is ~10 bytes
  of code and serves both.
- **DX is an argument to both calls and an output of the read.** Every call
  site needs `xor dx, dx` for a 16-bit count, and none may assume DX survives
  a read. Three kernel routines had to start pushing DX for this
  (`drv_cfg_load`, `drv_cfg_save`, and `drv_load` sets it inline).
- **Slot 0x01E8 is retired, not reused** (SPEC.md §20.8 rule 4): the cell
  answers CF=1 with `FERR_NAME`, so nothing above it renumbered, and
  `apps/os88api.inc` publishes **no** `OSAPI_FILE_READBIG` — a source that
  still names it fails to assemble. `tools/checkdocs.py` carries `0x01e8` in
  `HELD` so prose may keep naming the number.
- **Slots 0x0120/0x0128 changed CONTRACT at the same number**, which §20.8
  rule 4 otherwise forbids. It is a recorded, one-time exception taken while
  every caller is still inside this tree; it invalidates every `.o88`, and
  `make` is what makes that survivable. The next contract change is a new
  number.

**Both data walkers coalesce runs** (SPEC.md §18.4.2), and only one of them
used to. `dskw_wdata` appended whole sectors to a pending run and spent it
with `dskw_wflush`; `dskw_rdata` — the body behind `OSAPI_FILE_READ` — issued
**one `disk_read` per sector**, and its own header comment claimed it was
"stepped the same way" as the write side it had stopped mirroring. §18.91's
batching could not help, because `dsk_xfer` only batches what one call hands
it, so the largest file operation in the system still paid a revolution per
sector: PERFORMANCE.md's "a 116KB Tracker module is 57 seconds" was this and
nothing else. Measured after, on the same load: **295 sectors in 34 int 13h
calls against 244**, with the sector count *identical* — which is the shape
that says the splitter is not dropping work. The two now share one flush body
picked by `[dskw_fop]`, for the reason `disk_read` and `disk_write` share
`dsk_xfer`: the run arithmetic must not be able to differ between reading and
writing, which is exactly how this drifted. What is deliberately still one
sector at a time: **directory walks** (they read into the single 512-byte
`dsk_secbuf`, and a walk stops early — at the match, or at the first `0x00`
name — so reading ahead can cost work rather than save it, the opposite trade
from a file read whose length is known up front) and **`dskw_mkdir`'s
zero-fill**, which writes the same buffer per sector and is bounded at 3 extra
writes because §18.7's 65,535-sector partition cap keeps cluster size at 4.

What this did *not* remove: `dskw_append` and the file manager's chunked copy
(SPEC.md §22.5) stay, because the copy **buffer** is a heap claim of whatever
the machine could spare and a file can still be bigger than it. And Note Pad's
CR/LF staging buffer became a 1KB claim held only across one save or load —
an I/O buffer has no reason to live in a region that caps at one segment.

### The Standard File dialog is modal, and that is what makes it cheap (SPEC.md §38)

`kernel/fdlg.inc` is the kernel's Open/Save chooser — the other half of the
file API, which until it existed gave packages five whole-file operations and
no way to *name* a file (which is why Note Pad wrote a hard-coded `NOTES.TXT`).
Two things about it are load-bearing and easy to undo by accident:

- **It is not an instance.** No `KIND_*`, no `inst_tab` record — a bare
  `wm_create`d window this module owns, the same species as a `menu_track`
  pull-down rather than an application. So it has no dock tile, no Task
  Manager row and no callback billing (`inst_win_owner` answers 0 for an
  unowned window), and its close **and minimize** boxes reduce to `wm_hide`,
  which `fdlg_gate` reads as *cancelled*. That is why this module has no
  close-path code at all.
- **`[fdlg_win]` is the modal gate**, enforced at three call sites and nowhere
  else: `fdlg_grab` (every button press, swallowed unless it lands in the
  dialog's rect), `fdlg_top` (the keyboard poll) and `fdlg_reap` (the UI
  task's idle pass, which only affects latency). Because nothing else is
  clickable while it is up, no other window can navigate the volume — which
  is precisely why the dialog reads the global mount snapshot directly and
  needs no view cache of its own, the exact opposite of the Disk
  window's rule. The gate lives in `.text` as a `dw 0`, not in `.bss`: `-f
  bin` zeroes nothing and `fdlg_grab` reads it on the machine's very first
  mouse press.

`fdlg_open` (API slot 0x0150) is called from a window callback that already
holds the gfx lock, so it creates and shows the window inline and returns; the
answer comes back later through a completion callback, run after the dialog is
destroyed so the app repaints onto clean screen.

**Where it OPENS is per application, not global (SPEC.md §38.10).** It used
to end its setup with an unconditional "re-list where the volume already is",
and `[disk_drive]`/`[dsk_cwd]` are one pair for the whole machine — so
anything that moved them moved every app's idea of where its documents are:
a Control Panel close writes `SYSTEM.CFG` to the system disk (§51.5.1), a
package writing a file leaves the volume where it wrote, a Disk window coming
to the front re-lists into *its* folder (§22.8). The next Save As in an
unrelated app then opened somewhere the user had never taken it.
`inst_fdrv`/`inst_fcwd`/`inst_fpick`/`inst_fname` are a side table in
`instance.inc`, one row per record — and the first two are **the instance's
own current directory** (SPEC.md §19.2.1), the pair its FILE OPERATIONS
resolve in, not a memory the dialog keeps on the side. **An app that has
chosen nothing opens on `MEDIA`**, the folder both shipped disks carry
(empty on the system disk) for exactly this. Seven
things hold it up. It is a side table because `I_RECSZ`
is 32 and full and `index<<5` is what makes a record cheap to reach
everywhere else; `inst_alloc` SEEDS it from wherever the app was launched
(a reused record must not inherit a dead instance's folder, and seeding
rather than blanking is what leaves no "no folder" case for a file
operation to have a policy about); `[inst_fpick]` — a question about the
USER, not the data — is what selects the default, and neither of the first
two answers reads the globals at all, so it resolves `MEDIA` in the ROOT of
THIS INSTANCE's drive and falls back to that root, which costs two mounts
where it used to cost one and only on the opens before the user has chosen
anywhere. The **folder** is written by the
user NAVIGATING, at `fdlg_go` — the one choke point all three ways of moving
inside the dialog go through, and one `fdlg_home_go` deliberately does not
use, so *arriving at a default* is not a choice and a background read cannot
become one; backing out of a folder is a navigation too, and cancelling does
not undo it. The **name** is still written on a COMMIT only, after
`fdlg_commit`'s staleness triple, since a cancelled dialog is not a
statement about which document an app is on; the caller's `SI`
default still wins, the remembered name filling the box only when `SI` is 0;
and `fdlg_home_go` banks the current pair **before** trying anything,
because `dsk_chdir`'s failure path moves the globals it would otherwise
be recovered from. What it does *not* change is where a file operation
resolves — names still resolve in the current directory (§19.2), and the
dialog leaves the volume at the folder it committed to.

### Where the memory went (SPEC.md §2, `docs/KERNEL-MEMORY.md`)

**The kernel is one span, and it fits the budget.** `KERNEL_SEG` = 0x0060 — the first paragraph above the BIOS data area — through the top of task 0's stack: image, `.bss`, the cold segment, the FAT window (which doubles as the boot overlay's landing zone), the disk caches, the sector buffer and every task stack. 72,704 bytes of the 74,240-byte `KERN_BUDGET` (the footprint guard), with `KERN_CODE_MAX` — `.text` + `.bss` inside one 64KB segment — at 55,456 of 65,536. `KERN_BUDGET` is the tighter of the two by 5x and is meant to be; `docs/KERNEL-MEMORY.md` is the byte-exact account of both. Above it: the claim heap, and nothing else. The 60KB package pool is retired — a package's region is an ordinary heap claim (SPEC.md §20.1), which returned those 60KB to every machine and dropped the RAM floor from 256KB to **128KB**.

Five things got it there, and `docs/KERNEL-MEMORY.md` is the maintained account:

- **Task stacks and disk buffers are in `LOW_SEG`** — `.lowbss`, 9KB, addressed through SS or ES and never DS, which is why SS ≠ DS everywhere. It sits *above* the image now; there is no low memory under the kernel any more, because the kernel starts as low as the BIOS lets it. The disk buffers are read only through `dsk_get_dir`/`dsk_get_icon`, which stage one entry back into the kernel segment so every consumer keeps a plain DS:SI pointer.
- **A package's region is a heap claim, taken from the TOP down** (SPEC.md §20.1/§50.3) while data claims grow up from the bottom. Not tidiness: a data claim can move within its lifetime by being freed and re-claimed, and **a region can never move at all** — its base is its CS. From one end they interleave and a long-lived data claim mid-heap permanently splits the space a package can load into. The region's owner word is the instance **slot** and its data claims' is the **segment**, so `mem_free_rec` releases both and the Task Manager does not count the region twice. `APP_MAX_SIZE` is mirrored in `kernel/kernel.asm`, `apps/os88api.inc` and `tools/os88pkg.py` — change them together and rebuild every `.o88`.
- **The file manager's listings are heap claims** (SPEC.md §2.3/§22.1): `VIEW_KB` (3KB) per open Disk window, a byte-for-byte copy of `disk_dir` + `disk_icons` reached through the `FS_VSEG` segment its state block carries. Paints read the window's cache, actions re-sync the global snapshot first (`fmv_sync`), so a repaint, a drag or a `wm_paint_all` costs zero floppy I/O — and a machine with no Disk window open pays nothing.
- **Nothing has growth room.** Every rung is the measured size of what it holds, so the pool and the heap move with the kernel. `KERN_MAX` — a fixed ceiling with slack under it — is retired: that slack was memory nothing could ever use. The same mistake in miniature was `STK0_TOP`, which used to be "whatever is left below the kernel segment", so task 0's stack silently ate every byte saved anywhere beneath it; `STK0_SIZE` is a named constant now, and that is what turned two rounds of buffer-shrinking into actual memory.
- **`.fartext` is retired (SPEC.md §33) — and its two successors are not it.** The old mechanism copied cold modules down to a second segment at boot and needed a 10,752-byte low-memory reservation to hold a 5,455-byte blob, so once the whole *footprint* became the number being steered by, it cost more than it saved. The **boot overlay** (`.ovl`, SPEC.md §2.5) and the **cold segment** (`.cold`, SPEC.md §2.6) buy the same `KERN_CODE_MAX` relief with no reservation at all: both are sections of the one kernel image with a `vstart` of their own, landed by the boot sector's single contiguous read — the overlay in the FAT window, where it runs once from `kmain` and is overwritten by the first mount (it costs no RAM, no budget, no guard), the cold segment resident at `COLD_SEG`, where its bytes still count against `KERN_BUDGET` but not against the 64KB window. The contract for both: CS elsewhere, DS still `KERNEL_SEG`; calls cross through 4-byte `call`/`retf` shims, and `tools/os88ovlchk.py` (run by `make`) refuses any near call or branch crossing a boundary — the failure it guards against assembles cleanly and runs wrong.

Two invariants that are easy to break, both asserted:

- **Every disk-visible base is 512-byte aligned.** int 13h moves one sector per call, which bounds a transfer to 512 bytes but does **not** stop one straddling a 64KB physical boundary — only starting 512-aligned does, and the DMA controller answers a straddle with error 09h. The FAT snapshot, the disk buffers, a package image and a package's file buffer out of the heap are all int 13h targets, so `KIMG_PARA` rounds the image to a whole 512 bytes and the rest of the ladder follows. It held by accident while every base was a round constant; the symptom when it broke was a **"Disk error" on any save big enough to reach the next 64KB boundary** — Paint's 63KB BMP immediately, a Note Pad file never.
- **The boot sector relocates itself, to the TOP OF CONVENTIONAL RAM** (SPEC.md §2.7). It runs at 0000:7C00 and is *still running* while the kernel's sectors land — it far-calls the splash between runs — and the kernel covers 0x7C00 long before the last one. So it copies itself to `int 12h`'s top less its own 512 bytes and far-jumps there (`push`/`push`/`retf`: the segment is computed, and the 8086 has no `push imm`), **keeping the same offset** so every `org 0x7C00` label still resolves. It used to be a fixed `BOOT_RELOC`, and that address — not any property of the kernel — was what capped the footprint; at the ceiling it can only meet the kernel on a machine too small to run the OS, so it **refuses** instead, comparing its computed base against where the kernel's read plus its own 2,048-byte stack would end, printing `RAM` and halting. Three failures land on that one compare: a machine genuinely too small, a BIOS under-reporting (an XT counts RAM from its DIP switches), and a `KB*64` that overflowed. Trusting `int 12h` is not a new dependency — DOS sizes itself from the same call, and `mem_init` reads it for the top of the heap, so the sector and the allocator agree by construction. The heap hands regions out downward, so **the first package loaded sits exactly where the sector was**. QEMU always answers 639KB, so the low-memory paths need `make test RAMKB=<n>` (measured: 105 boots, 104 refuses). **Two files carry `KERNEL_SEG`** — `kernel/kernel.asm` and `boot/boot.asm` — plus `apps/os88api.inc`, because it is baked into every package's far-call targets, so a kernel move means rebuilding every `.o88` and both apps floppies.

### Layout

- `boot/boot.asm` — 512-byte boot sector; geometry comes from `-DSPT`/`-DHEADS`, sector count from the measured kernel size (both injected by the Makefile).
- `kernel/kernel.asm` — constants, the derived memory ladder and its guards, boot sequence, the os8088 API jump table at 0060:0010, `%include`s of all modules, final .bss and size assertions. Module ownership is the table in SPEC.md §4; each `.inc` owns one subsystem (viddet, vga12, font, mouse, sched, events, wm, instance, menu, ui, apps, disk, diskw, loader, files, fdlg, icons, desk, dock, ctrl).
- **A driver may own a Control Panel page** (SPEC.md 31.9). The item list is
  the five static rows plus one per loaded driver that publishes `DSV_CPNAME`,
  so a page exists exactly while its driver is attached - which falls out of
  the publication slot being cleared at detach rather than being arranged. Two
  traps: the page's list NAME is staged into the kernel, because `font_str`
  reads through DS and a pointer into the driver's segment renders the
  driver's own image; and `[cp_sel]` is clamped at detach, or the panel
  dispatches through a freed segment on its next paint - and the panel need
  not be open for that to be owed.
- **Mounting a volume costs the zones that MOVED, not the screen and not the
  grid** (SPEC.md 26.3). `osapi_vol_add`/`del` cannot repaint (a driver calls
  them with the gfx lock held), so they post `[desk_zdirty]` and `ui_task`
  spends it — and that is a DIFFERENT flag from `[cp_dirty]` on purpose:
  `cp_dirty` means the scheduler mode changed, which every window quoting it
  must be told about, so it is honestly a `wm_paint_all`. A drive zone is one
  icon. `desk_zones_paint` is `wm_paint_dmg` over that rect, and no
  `wm_paint_all` runs at all. **The rect was the whole GRID and that was the
  bug** — `DVOL_MAX` zones whatever is mounted, which sounds like harmless
  slack and is not, because zones fill a column downwards and wrap LEFT, so
  the unused ones are whole COLUMNS of empty desktop reaching back across the
  screen. On CGA (`desk_rows` = 2) three volumes occupy x 526..633 and the
  grid claimed x 470..633; those 56 phantom pixels reached into the Control
  Panel window the Mount button is IN, so 11.91's marking pass redrew that
  window whole, `W_PAINT` and all, on every mount. 295 glyphs → 148,
  framebuffer byte-identical. `[desk_zhw]` is the fix: a high-water mark of
  how many zones the screen may be showing, raised by `desk_zmark` and spent
  by `desk_zones_paint`. **The changing zone is COUNTED either way** — an add
  marks after the row goes live, a delete marks BEFORE the flag is cleared,
  because what a delete leaves behind is stale pixels at the old last
  ordinal — and taking the max is what survives coalescing, which "the live
  count plus one" does not: unmounting a three-partition disk drops three
  volumes into one drain. The other trap is unchanged: the bottom-left corner
  is `(cols-1)*desk_rows + (rows_used-1)`, NOT the last volume's ordinal —
  `[desk_rows]` is 4 on Hercules, where the last ordinal sits two whole zones
  above the bottom of the first column.
- **A per-window test that keys off another routine's drawing must ask whether
  it DREW** (SPEC.md 11.91). `wm_paint_dmg` marks every window reaching
  `[vid_dock_y0]`, because the dock strip is drawn under windows — but
  `dock_paint` is incremental and a quiet desktop costs it no pixels, so the
  test's own comment ("the dock is repainted unconditionally above") had
  outlived the code it described. A window dragged onto the strip was erased
  and re-`W_PAINT`ed on every mount for a strip that had not changed, and the
  marking is transitive so everything overlapping it went too: 313 glyphs
  against 148. `[wm_dmg_dk]` is a one-shot argument set by whichever caller
  got CF = 1 out of `dock_paint`, and **cleared by `wm_dmg_wins` rather than
  by the caller**, because the two errors are not symmetric — a wrong 1 is a
  wasted repaint, a wrong 0 is the dock drawn through a window — so the value
  a forgotten call site inherits has to be the safe one.
- **Desktop zones ARE the volume table** (SPEC.md 26.1), and they wrap into a
  new column to the LEFT when one will not fit above the dock. `[desk_rows]`
  is computed at boot from the live geometry: 7 on VGA, 4 on Hercules, **2 on
  CGA**, where a third zone would otherwise land on the dock at row 176. The
  §1 rule about `[vid_*]` in its natural habitat - invisible on VGA, so a
  drive-zone change is not done until it has been looked at on CGA.
- `kernel/font.inc` — the 8x8 text renderers. `font_char` is transparent-background and clips per whole cell; **`font_run` (API 0x0258, SPEC.md §6.1) is the erase-and-letter pair as ONE operation** — both colours, each cell painted complete. It is a SPEED optimisation for the mono adapters and is measured, not asserted: `tests/fontbench` (in **`tests/`**, built only by `make bench` — the testing apps are tooling and never ship) says **1.30x** for a ten-character run, and that figure is from a REAL 4.77MHz XT with a Hercules card, where a cell costs about 1ms; `tests/gfxbench`, a separate harness, says **1.24x** on that machine's Hercules AND its CGA card. Under QEMU it also says 1.26-1.30x in instructions but 2.85x in framebuffer traffic — and the hardware came in at the INSTRUCTION figure, so the per-cell overhead dominates the writes it guards and traffic is the explanation, not the predictor. **What caps the win is the ~756us fixed cost every drawing call pays** (PERFORMANCE.md Part 2): `font_run` collapses a fill and ten `font_char`s into one call, so what it saves is mostly *floors*, and nothing done inside the renderer can beat a number paid outside it. **The bigger win is that it does not FLICKER**: the erase-and-letter pair leaves the run blank between the fill and the last glyph, which on an XT is tens of milliseconds and plainly visible, and `font_run` writes each cell from old to final in one store (SPEC.md §6.1). On a 1bpp adapter at a byte-aligned x the cell owns its whole framebuffer byte, so a cell row is a single store — no shift, no read, no second byte, no separate fill pass. **Two things about it are commonly got wrong.** The fast path needs `[bb_on]` **and** `x & 7 == 0` **and** `[gfx_dis]` clear, and anything else falls back to `gfx_fill` + `font_str` — which costs **2.5% MORE** than writing that pair by hand (the far call, the gates, `font_width_x`), so it is not free on VGA and the tracker calls it on mono only. The "cannot produce §11.3's granularity failure" property is now the CALL's, not just the fast path's — the fallback used to be literally the fill-then-letter pair and blanked a cut line, and it picks per-cell drawing when a clip edge actually crosses the run (SPEC.md §6.1.2), so a caller may draw under an armed region without gating. Tracker's pattern columns were moved onto 8-pixel boundaries to earn it; **`WF_SNAP`/`OSAPI_WM_SNAP` (SPEC.md §11.94) is how an ordinary window earns it** — an opt-in, mono-only flag that keeps the window's CONTENT origin on a multiple of 8 through every `W_X` write, at the price of 8px drag steps. The Task Manager and Note Pad use it; the Disk window was measured and left alone, because `fm_repaint`'s one big fill plus ~40 `font_str`s is already cheaper than 40 self-erasing runs. **`wm_snap` preserves FLAGS** — a package entry proc returns CF to the loader, and the mono-only `cmp` inside it left CF set, so asking to be snapped aborted the launch on Hercules while loading fine on VGA.
- `kernel/viddet.inc` — adapter detection, runtime geometry, `gfx_rowbase`/`gfx_nextrow`/`gfx_ink`. Included **before** `splash.inc`: the splash probes and sets the mode on its first tick, so this must be resident in the first `SPL_RESIDENT` sectors and all its data lives in `.text`, never `.bss`. **`vid_init` no longer re-runs `vid_setmode` unconditionally** (SPEC.md §15.3/§39.6): the mode set clears the framebuffer, and while the splash is still up that clear IS the loading screen — so it is skipped while `[spl_live]` is set. The probe and the publish always re-run and must, because everything between there and the first paint reads `[vid_w]`/`[vid_h]`/`[vid_stride]`.
- `kernel/splash.inc` — **the boot bar does not stop at the last sector of the kernel** (SPEC.md §15.3). It used to hit 100% when the boot sector's read finished and then get wiped, with the whole of kmain still to run — on the field machine that is ~3.1 s of floppy for `drv_boot`'s mount alone (PERFORMANCE.md: **~65 ms per sector** in a run, 238 before SPEC.md 18.91's `AL` fix), and nearly ten seconds when a driver was being loaded. The bar is priced in SECTORS and so is the tail, which is why this was cheap: `SPL_POST` (16) notches are added to the *denominator* by `spl_tick`, `spl_step` spends one and repaints, and `spl_finish` forces the last and hands the screen over immediately before `wm_paint_all` — which needs no erase, because that paint covers every pixel. The hot caller is `dsk_xfer`, **once per sector transferred**, so `spl_step` preserves every register AND the flags, and is a compare plus a `ret` for the life of the machine after boot (~13us against a sector's 65 ms). Three traps: `[spl_live]` ("the splash owns the screen and the mode") is NOT `[spl_on]` ("the chrome has been drawn"); `bb_set` had to move to the END of `drv_boot`, because a back buffer seeded from VRAM ahead of the reads swallows every notch after it; and the allowance is a **clamp**, not a scale — `spl_step` stops at `total-1`, and raising the total mid-run is not the fix for an overrun, because the fill is `done x 288 / total` and the bar would go *backwards*.
- `kernel/video.inc`, `keyboard.inc`, `string.inc`, `gfx.inc` are dead — left in the tree but **no longer included** (relics of the pre-GUI text shell, as is `kernel-shell.asm.bak`).
- `apps/` — loadable packages. `os88api.inc` is the SDK: `OS88_HEADER` emits the 32-byte package header (including the dispatcher bytes at +12), `OSAPI_*` `%define`s name the far-call table cells, `OS88_IMAGE_END` seals size + bss. `mines/` (embedded icon), `hello/` (proves the generic-icon fallback — the only thing in the tree that still ships without an icon, deliberately), `notepad/` (the former built-in Note Pad kind, moved out to reclaim ~1.4KB of kernel budget — its per-instance bss replaced the fixed 2-instance pool, so the cap is gone. **The note itself is a heap claim** (SPEC.md §27.6), not bss: 1KB at launch, +1KB whenever a keystroke would fill it, sized to the file on a load — where the file lands in the buffer and the CR/LF fold runs in place — and shrunk back on New. **The view scrolls** (SPEC.md §27.7), and one word does it: `[np_top]` is the note row at the top of the content, and `np_walk`'s `np_row` starts at MINUS it — so a row above the view has a negative index, and every array here is already indexed by an unsigned test against a limit, which a negative word read as unsigned is past. The one place that could not see it is `np_rflush`, because a row just above the view has an ordinary small y rather than an implausible one. Moving the view DROPS the signatures, the checkpoint, `np_rows` and any loaded seed rather than adjusting them — adjusting would be a second place that has to agree what a row is — so a scroll is a full repaint, never a band. Two traps it sprang: `np_measure` does not clear `[np_resume]`, so `np_scrollto` has to, or the walk after a scroll resumes at a row that has been renamed and every number it produces is out by the scroll; and the caret-follow is its own flag `[np_follow]`, NOT `[np_ekind]` — that one says which cheap redraw path a keystroke earned, and Enter, Up, Down, Home and End are all 0 there while all five move the caret. The bar is the Disk window's (SPEC.md §22), reserved always because whether one is needed depends on the row count, which depends on the wrap width, which would depend on the bar. **Every walk now STOPS at the bottom of the view** (SPEC.md §27.7.1): scrolling made it worth asking why a keystroke lays out the part of the note nobody can see, and the counter said 72% of the work was below the window — a keystroke near the top of a 2,000-character note was 6 walks and 10,079 iterations and is 2 walks and 1,015. The traps, all of which broke first: the `[np_lastrow]` comparison has to be **signed** (a row above the view is negative, which unsigned reads as past every limit, so the walk stopped before drawing anything) with `0x7FFF` as the no-limit sentinel; the bound is `[np_vrows]`, one row PAST the last visible, because a character typed at the end of the bottom row wraps the caret onto the row below; `[np_curseen]` exists because 0 is a real pen y so `[np_cury]` cannot say "not found", and the safety net it arms must walk **unbounded and unseeded** or it misses the caret exactly as the first walk did; and `.pad` must not fall into `.stop`, which would claim `np_rows` entries `np_rstart` never wrote. `[np_drows]` is the one thing a bounded walk cannot know — exact at a natural end, a **monotone lower bound** at a bounded stop (never lowered, so the error is always in the direction that keeps the caret reachable), and recounted by `np_height`: the worker half a second after typing stops, and `np_onclick` synchronously before a bar click, which is the one place the height must be exact rather than generous. **A scroll moves the pixels it already has** (SPEC.md §27.7.2): moving the view by `d` rows changes only `d` rows of what is on screen, so `OSAPI_GFX_SCROLL` moves the rest and an arrow click letters 4 rows instead of 20 (`NP_SB_STEP` is 4 — the Disk window steps one, but its rows are 16px list entries and these are 8px lines of prose). What licenses it is that the blit shifts the PIXELS and `np_shiftrows` shifts their DESCRIPTION — `np_sig` and `np_rows` — by the same `d`, in one operation, so §27.7's "drop rather than adjust" no longer applies: there is no second place that has to agree. `[np_ptop]`, the `[np_top]` the screen was drawn for, is what says the two have parted, and `np_redraw` reconciles it BEFORE anything reads an array indexed by a visible row — a bar click scrolls and *then* redraws. Two things in the band arithmetic bit: the x span rounds OUTWARD to byte columns (which is what makes it work on VGA and not only where `WF_SNAP` aligns the content, at the price of blanking a 7px strip that `np_sbar` and the grow box own), and the y span must stop at the last WHOLE row rather than `[np_bot]` — the sliver below it is a row `np_rflush` refuses to draw, so nothing would ever erase what the blit pushed into it, which showed as a permanent 1px band of descenders on Hercules and not at all on VGA. A page scroll and a live toast are both refused and repaint in full, on purpose. **Selection, Cut/Copy/Paste, Undo and Find/Replace are SPEC.md §27.8–§27.10**, and four things in them are worth knowing before touching this file. **The selection is an XOR fill folded into the row signatures** — a selected cell hashes with bit 15 set, so moving it dirties exactly the rows it left and arrived at — and `np_selxor` intersects the row's span with the cells the run *actually wrote*, because XOR is its own inverse and a second one on an untouched cell takes the highlight back off. **A drag pass costs one ROW of layout, not one note** (SPEC.md §27.8.1): `np_hitpt` asks what `np_onclick` asks and now answers it the same §27.5 way - seed at the pointer's row, stop after it - where it used to call `np_measure` unseeded and unbounded eighteen times a second; and a pointer whose (x,y) is unchanged skips the pass entirely, except when it is parked outside the view where every tick owes a row of scroll. 154,112 walk iterations to 36,608 on an identical drag, **4.2x**. What is NOT the cost, though it looks like it, is the `[np_ckok]` clear: pass 1 only consults the checkpoint when `[np_ekind]` names an edit at the caret, and a drag sets no kind. **Moving the inversion draws NO glyphs** (SPEC.md §27.8.2): np_rflush folded the UNION of a row's old and new selected spans into the span it drew, so changing which cells are inverted dragged the glyphs with it - ~30 cells a row, ~1ms each. A drag changes no character, so the walk records a second span per row (`[np_xs0]`/`[np_xs1]`, the cells whose selected-ness differs from what the SCREEN shows) and one XOR flips exactly those. 4,049 glyph cells in 141 font_run calls became 929 in 33. The gate is a selection at BOTH ends, which is what keeps the caret out of it - erasing a caret means lettering the cell under it, and this path letters nothing. **No new primitive was needed and the obvious one would not have helped**: `gfx_xor_fill` is already an inversion and `font_run` already takes an ink AND a background, so "draw this run inverted" was always `AL=CWHITE, AH=CBLACK`; the cost was never the inversion, it was re-lettering cells whose characters had not moved. **A drag inside the selection MOVES the text, in three in-place reversals and no buffer**: reversing `[s,e)`, then the run beside it, then both, rotates the block to the far end of the span, and a staging claim that can be refused would have made a drag fail for reasons the user cannot see. **An undo group is a burst of keystrokes with no half-second gap**, measured by the worker that was already there for the visual break — and `np_ubegin` is what hires it, because until that it was hired only by the break and by a note that outgrew its window, so a fast machine with a short note never closed a group and the whole session was one undo. **The regex matcher is iterative with an explicit backtrack stack in bss**, because the match count is recomputed by the *worker* and a worker's stack is 256 bytes (§8); a pattern needing more than `NP_RXST` frames is refused whole by `np_fchk` before anything runs. The find panel is a strip of the CONTENT rather than a second window, so the whole of what the rest of the module knows about it is the height `np_bounds` adds to `[np_ty]` - and **opening or closing it is a BLIT, not a repaint** (SPEC.md §27.10.2): the panel changes that one number and nothing about the wrap, so every row holds the same characters and only moves. 570 glyph cells became one `OSAPI_GFX_SCROLL` plus the panel, and closing costs the four rows the text moving up EXPOSES. It is §27.7.2 with the renumbering removed, its three safety tests are refusals rather than corrections, and the shift is **not a multiple of 8** - 29px and 41px against np_scrollpaint's whole rows - which is why it was verified on CGA and not only on VGA (0 differing pixels against a forced full repaint, both adapters, both directions), and searching has a MENU of its own rather than a tail on Edit - it is the one thing here that reads the note without changing it. There is no Clear: Backspace and Delete already delete a selection, so it was a third door onto `np_selkill` whose only distinction was being slower than the key everybody presses anyway. Two traps it sprang: `[np_ffield]` = 0 is a real focus (the find box) and bss arrives zeroed, so a fresh Note Pad sent every keystroke to a panel that was not on screen; and ONE caret word serves both boxes, so tabbing from a long pattern into a short replacement gave the insert loop a negative count and wrote 64KB across the package's own bss. `NP_MAXKB` is 16 now and is an ARITHMETIC limit, not a memory or a display one: a save expands newlines to CR LF, and twice a 16KB note is 32,768, which fits the 16-bit DI, BX and `2 × [np_len]` that walk it — twice a 32KB one does not. A dirty row is **one opaque `font_run`** and there is no band fill (SPEC.md §27.2): the row is accumulated into a buffer space-padded to the band, and a space paints background on the fast path, so the padding IS the erase and the line is never momentarily blank. That was done for the FLICKER, not the 10.7% — on a real XT a keystroke costs 33ms and the old fill-then-letter pair spent most of it with the line empty. The trap it sprang: rows the walk no longer reaches — a backspace pulling a wrapped line up — must be blanked explicitly, because the band fill used to cover them for free. Two things then landed on top of it, because once the drawing was two cells the LAYOUT was the cost: **the walk resumes NEAR the start of the caret's row** (SPEC.md §27.4 — it used to be AT it, licensed by "wrapping has no lookahead"; §27.11's word wrap took that away, because the break in FRONT of a row is decided by the length of the word BEHIND it, so `np_seedck` seeds one row earlier and walks back further through a long word. The ordinary case is one extra row and the fallback when `np_rows` runs out is a full walk, which is the only right answer for row 0 of a scrolled view; 404 walk iterations a keystroke became 35 at 200 characters, and stopped growing), **`np_rows` — the index each row starts at** (SPEC.md §27.5 — every walk exists to answer a question about ONE row, and recording where rows begin turns the caret keys from four full walks into four bounded ones: Up 1,608 iterations → 184, Home 1,608 → 90, Left 804 → 60), and **typing in FRONT of text scrolls instead of reflowing** (SPEC.md §27.3 — `OSAPI_GFX_SCROLL` pushes the rows below the caret down a row and the screen shows a line break the note does not contain, settled half a second later by a worker task. Gated on `OSAPI_CPU_INFO` = `CPU_8086` and on `np_tx` being 8-aligned, which `WF_SNAP` guarantees on the two mono adapters and nowhere else. **Every edit enters it — insert, Backspace and Delete — because the key handler REPORTS the caret's column rather than leaving the break to derive it**: the row below duplicates a prefix of C cells either way, but the caret ends at C+1 after an insert and C−1 after a backspace, so a derivation runs the opposite way for each and left two stale characters behind a backspace. Entering and *continuing* are different permissions, though, because the tail is not redrawn while the break is up: Right would draw a character twice, Left would lose one, and Delete eats the tail's first character, so all three settle), plus the sound packages `recorder/` and `piano/` (SPEC.md §35/§36), `fractal/` (SPEC.md §40 — the reference worker task, and the reason both halves of the redraw work exist), `paint/` (SPEC.md §42: a bitmap editor whose canvas, undo image and clipboard are a **heap claim** sized from `OSAPI_MEM_AVAIL`, giving up features tier by tier and finally putting up a notice window on a machine too small. Its BMP **and GIF** codecs borrow those same buffers for their work areas, which is the only reason a 16KB LZW dictionary fits at all; **Opening a big picture is SPEC.md §42.6**: the staging buffer is a transient claim sized from `OSAPI_MEM_AVAIL` — it used to be the borrowed undo image, or twelve kilobytes of flood-fill stack when there was no undo image, which was the real ceiling — and `pt_srowset` carries the BMP decoder's source row as a (segment, offset) pair, `dskw_norm`'s arithmetic inside an app. 69,718 bytes refused before, 124,918 opened after. The GIF codec stays 16-bit on purpose: a GIF is compressed, so 64KB of it is a picture past `PT_GDIM_MAX`, and it now refuses a larger one rather than truncating it. `docs/PAINT-NOTES.md` records which of the capabilities it asked for have since landed and which have not. **Its pencil is SPEC.md §42.8, and the lesson is that a leading-edge sweep stops paying at width 1**: `pt_seg` draws the brush's leading edge per step, which is why the 32px eraser is usable, but a one-pixel brush's edge is one pixel — one `gfx_fill` a step and a second whenever the minor axis moves, so a 300-pixel chord is ~400 calls and 373 ms, and the pencil had a **maximum drawable speed of ~1,000 px/s**. A hand drawing a circle passes that and the lag then compounds, which reached the field as circles coming out as long straight chords with whole arcs missing (docs/FIELD-NOTES.md 11). A width-1 segment is now one `OSAPI_GFX_LINE` (576 drawing calls → 66 over one scripted stroke). **The walk had to become the KERNEL's** — the canvas is what a repaint draws from, and `gfx_line`'s two-error form walking always downward puts almost every pixel of a chord somewhere the DDA form did not (3,015 differing bytes, against 0 with the fast path off) — and **no kernel file changed**, so Missile Command's trails are untouched. It is gated on 1bpp because `gfx_line_raw` sends mono to `gfx_line_mono` and VGA to `gfx_line_runs` and **those two do not agree pixel for pixel** (0 bytes on Hercules and CGA, 663 on VGA). **Its Full Screen is SPEC.md §42.7, and it is §53's SAME-MODE consumer** — the one that shows what a bracket setting no mode is for: no `fsx_mode` call, so no caps bit, so it runs on VGA, Hercules and CGA out of one body and the menu item never greys (§47 rule 3: the mask answers a question about modes and this sets none). **It is also the one that deliberately does NOT sit on the §11.2 surface, and that is a measured decision, not a preference.** §11.2-then-bracket is the obvious build (Missile Command's), and it is tidier — `wm_fullscreen` puts the screen's geometry in the WINDOW RECORD, so `pt_org` needs no branch at all. What kills it is coming home: §53.6 step 4 owes one `wm_paint_all`, the window is still `WF_FULL` when it runs, so that repaint draws the FULL-SCREEN Paint and the `OSAPI_FULLSCREEN` AL=0 after it throws that away and repaints as a window. Counted at `gfx_blit4`/`gfx_fill`/`font_char` over one round trip **with the §53.6.1 XMS stash forced off** — mandatory at the time, because the target is tier 0 and a stock QEMU box's stash REPLACED the spare repaint and hid the whole problem (the stash is gone now, §53.6.1, so every machine repaints and the figures are simply what every machine does) — leaving cost **3 canvas blits and 1,496 fills against 1 and 770**, and the round trip 5 full-canvas draws against 3. So the geometry lives in `pt_org` instead: one branch, `[pt_fsx]` set → origin (0,0) and the content box is the screen. Two things follow from the kernel not knowing: `pt_repaint` skips `OSAPI_WM_GROW` (`wm_grow_paint` would draw a box at the record's own corner, a window nobody can see) and the first paint clears its own bed (`pt_fsbed`, two rects — there is no `wm_draw_win` white fill in front of it, and filling the whole screen would be PERFORMANCE.md's double-draw with the picture as the second layer). What it takes the machine FOR is the lock, not a raster: `pt_wait` runs once per mouse sample of every stroke and windowed it is an unlock/yield/lock round trip — PERFORMANCE.md Set 4's 21.8% pair — so inside the bracket it becomes `OSAPI_FSX_WAIT`, which is also the only present a double-buffered tracking loop gets. Three things it owes: its own crosshair (the held lock keeps the system arrow off all session), whose position is **banked and replayed** because XOR is its own inverse and an erase at a position the draw never used smears permanently, and which comes OFF around every dispatch because a click or key may draw anywhere — **and SPEC.md §42.7.1 is why it is a 5x5 SQUARE and not the crosshair it started as**. Two defects, both reported off the field machine as a flickery pointer on Hercules. It was written TWICE (erase-then-draw wrote every pixel the two positions share dark and then lit again — §7.1.2's defect in an app's overlay), so `pt_ptr_sub` emits the part of one rect the other does not cover and the NEW square's pixels go first (a move bigger than the pointer leaves the two disjoint, and in that order the pointer is never absent, only briefly doubled — an absence reads as a blink, a double reads as movement). And it COST too much to be atomic: as two bars a move was 4 calls and 32 scan lines = 8.7 ms against a 20 ms Hercules frame, and **the fixed 756us per call is the lever, not the size** — a shorter crosshair is still four calls. Measured over one 33-move walk: **136 calls and 872 scan lines → 76 and 354, 2.1x**. Its verification took two corrections to the APPARATUS, both of which made a broken build pass: `mouse.py`'s `to` re-pins against the edge clamp so no two samples land on one row and the overlapping path is never reached (use relative `move`), and an out-and-back PAIR cancels its own error exactly, so the return leg needs different step sizes than the outward one; its own input, which is polling int 16h and `OSAPI_MOUSE` and calling **its own `pt_click`/`pt_onkey` with the arguments the kernel would have handed them**, so every tool, control and toast works in there unchanged; and Escape offered only when it has nothing else to cancel, Ctrl+F always. **The canvas maxima deliberately do NOT rise**, which is the decision to read before "improving" the 67 rows of white it leaves on every adapter: the canvas IS the content, a window has no viewport, and a shrink that would crop artwork is refused — so a picture grown into that band could never come back to a window, and the band is the price of an exit that always works), `solitaire/` (SPEC.md §43 — Klondike, and the one package that drags the way the *window manager* does: `sol_drag` is `ui_drag`'s erase-before-unlock loop written against the API, so a hand of cards costs a few XOR strips a tick and nothing repaints until the button comes up. Faces are drawn but **backs are blitted** — the lattice is rendered once into a packed 4bpp image, so each later draw is one `OSAPI_GFX_BLIT4` instead of hundreds of far calls — and on 1bpp its red pips go *hollow* rather than red, because index 12 reduces to white and would vanish into the card face), `arkanoid/` (SPEC.md §44 — a brick-breaker whose **game loop is the worker task**, because a ball has to keep moving between keystrokes: one frame per `OSAPI_TASK_SLEEP 1`, and everything the UI task does is set a word the worker reads. Three things it discovered are worth knowing before writing another real-time app. int 16h has **no key-up**, so a held arrow is inferred from typematic repeat — each press refills a deadline in ticks that must outlast the ~9-tick typematic *delay*, or a hold stalls for half a second and reads as a dropped keyboard. And **`OSAPI_SND_TONE` is worker-safe**, which the SDK's list did not say until this: `snd_req_inst` stamps a grant with the running task's own `T_INST` when no callback is being dispatched, so a worker's tone is attributed to its instance and released at teardown — only the *blocking* `OSAPI_SND_PLAY` is UI-task-only, and for the different reason that it freezes the desktop. And a package is told when it **gains** the front and never when it loses it — `W_ONCLICK`/`W_PAINT` are the arrival, `W_FLAGS` bit 1 only says *visible*, and a covered window still is — so a real-time app that must stop when the player walks away has to **ask**, every frame, and it takes **two** questions (SPEC.md §12.6/§44.8). `OSAPI_WM_TOP` answers who is frontmost; `OSAPI_MENU_OWNER` (slot 0x02B8) answers who the *active application* is, and only the second catches a click on the **bare desktop**, which hands the menu bar to Locator and moves nothing in the z-order — so `wm_top` alone reads it as nothing having happened while the player is off in Locator's menus with a live ball on screen. Both take no lock and touch no VRAM, so the worker may call them every frame. Two consequences. Arkanoid's pause is **sticky**, because a ball that starts moving the instant a window is raised is a ball nobody was watching yet. And **a resume has to re-activate** (`ark_refocus`), which is the trap: a key is routed to `wm_top`'s window while the bar follows `[menu_win]`, so after a desktop click Space still arrives, sets `M_PLAY`, and the very next worker frame reads the same unchanged owner and pauses again — a pause no key can undo. `OSAPI_WM_FRONT` fixes it because it *activates* before it raises, and it is gated on already being frontmost: on any other window `wm_front` repaints, which from inside a callback re-enters the package's own dispatcher through `W_PAINT`), `tracker/` (SPEC.md §45 — an FT2-style MOD player: worker-fed ring streams, one `OSAPI_FILE_READ` for a 116KB module, scroll blits. **Its fullscreen is the fsx exclusive surface now (SPEC.md §53), and it is the `FSXF_KEEPWORKER` reference consumer**: the bracket owns the machine and draws the FT2 screen while the audio worker keeps feeding the ring across the freeze, so a Sound Blaster stream never underruns. **Which surface it is depends on XT mode (SPEC.md §45.13).** XT off: same video mode (§53.7), so the drawing slots stay legal and the graphics FT2 screen is drawn as before. XT ON — which a tier-0 machine boots with pre-armed — the bracket sets `FSXM_TEXT80` and draws the same screen out of character cells, and **the full scrolling pattern grid comes back**. That is measured, not asserted: the grid in pixels is **2,567 glyph cells a second** on Hercules, about 2.6 seconds of drawing per second of music at PERFORMANCE.md's ~1ms/cell, which is what §45.9.1 turned the view off to escape; in text it is **zero** cells and zero fills, because a cell is one word store and the card's ROM font does the rest — 1,121 `rep movsw` words a row change, ~4% of the machine. Three things about that path are easy to undo. **A sequential boundary MOVES the window rather than reformatting it** (SPEC.md §45.13.5): the cursor started on the visible rows, so a pattern boundary rewrote nineteen rows under the reader one per frame - reported from the field as "a whole line of replacements going down the first column". No second buffer is needed, because the shadow already holds them: this pattern's tail IS the next shadow's pad above, and the pad below IS its first nine content rows, so one `rep movsw` of shadow rows 64..81 down to 0..17 puts the whole window in place and the cursor starts at row 18, below the screen. Taken only when the build FOLLOWS the one in the shadow (complete, and the patterns either side match), so a position jump or a `Bxx` still formats from scratch. **It shipped once as DEAD CODE** - two `jmp short`s in a row, the old `jmp short .out` above the new `jmp short .carry` - which assembles under `-w+error`, boots, and puts the field's original complaint back in the field's original words. The verification missed it because it compared *rows in common across a boundary* (13 rows, 0 differences), and that is equally true of a correct rebuild: it measured the destination rather than the path. What replaces it measures the property the feature actually claims - **the window scrolls and never re-letters**: sample the 19-row window repeatedly and require each consecutive pair to line up under SOME row shift (69 pairs across two boundaries, 0 misaligned), plus an `FL` bit set inside `.carry` itself so a capture says which path a boundary took. **The shadow is why a row change is a blit**: a player never edits, so the pattern is formatted ONCE into `ttx_shadow` as char/attr words, with `TTX_HALF` rows at each end so the window into it is `shadow + view*rowbytes` with no clamp and no branch — the ends of a pattern are rows being blitted like any other. **Those pad rows are the NEIGHBOURING ORDERS** (§45.13.4), and that is what makes the scroll contiguous: they were blank, so every pattern boundary was 18 rows of nothing crossing the screen — ~2.2 s of the grid emptying and refilling — while the music carried straight on through it. Above row 0 is the previous order's pattern (rows 55..63), below row 63 the next order's (rows 0..8), resolved once per rebuild into `[ttx_shprev]`/`[ttx_shnext]`; the row numbers running `..3E 3F 00 01..` are the only boundary marker needed. Three traps: the claim is the order POSITION (`[ttx_shpos]`, plus `[ttx_shloop]`) and not the pattern number, because two orders naming the same pattern move both pads without moving `[mp_pattern]`; a pending `Bxx` parks `[mp_songpos]` at `target-1` for the rest of its row, so the position test is skipped while `[mp_posjmp]` is set; and past the last order the neighbour is `[mp_restart]`, while *before* position 0 there is nothing at all unless the song restarts there — `TTX_NOPAT`, the one case where a blank pad is the truth. The cost is 18 more rows per rebuild (82, ~21 chunk frames instead of 16) and **the frame is unchanged**, which is the only figure the spread was ever defending. **The screen shows what is being HEARD** (SPEC.md §45.15), which it did not: `mp_row` is the row being MIXED and `trk_feed` keeps the ring topped up to `TRK_RING - TRK_HALF`, so at the XT rate the whole display ran **2.2-3.0 seconds ahead of the card** — measured, not modelled (press Enter, screendump 0.15 s later, the grid was on row 21; it now reads row 00). `mp_stamp` records where each row's first sample lands in the stream, `mp_at_pos` answers with the stamp the card is inside, and `tui_sync` publishes it into `tui_a*` for EVERY drawer, graphics and text alike. Four traps: the position is caught between `mp_nextrow` and `mp_readrow`, because a `Bxx` parks songpos at `target-1` for the rest of its row; both ring cursors are free-running bytes masked at use, because the writer is `trk_feed`'s lock-free worker and the reader is the UI task, so an advance must be one indivisible `inc`; the card's position is the worker's OWN status poll republished (`[trk_consumed]`), not a second driver call per frame; and `trk_play_stop` parks the replayer at the audible row, or stopping jumps the display three seconds forward. **The stamp comparison is `js`, not `jg`** — `pos - consumed` is modular arithmetic between two free-running 16-bit counters, so the SIGN OF THE RESULT decides (`sch_isr`'s wake scan is the same idiom); `jg` honours the overflow flag and froze the display for six seconds out of every twelve, then jumped it 44 rows. `tests/trklog.inc` caught it on its first capture, which is what its two new columns are for (`SD` pinned at 63, `AR` standing still while `CONS` advanced). **And the card's report is COARSE** (SPEC.md §45.15.1): the driver advances `consumed` one whole DMA half per block IRQ — 2,048 bytes, 372 ms, about three rows at the XT rate — so following it raw stood the grid still for a third of a second and then jumped three rows. `tui_playpos` interpolates between blocks at `mixrate/18.2065` bytes a tick (the high word of `mixrate × 3600`, so no division), and the two properties that make that safe rather than merely smoother are that it is BOUNDED BY WHAT WAS STAGED (`[trk_total]` - a physical bound, where "one block past the last REPORT" froze the scroll for a fifth of a second every few rows, because `[trk_consumed]` is whatever the WORKER last saw and its passes can be 10-20 ticks apart) and MONOTONE (`[tui_play]` never falls, because a scroll that jumps BACK a row reads far worse than one that steps). **The model is an ACCUMULATOR, not a stopwatch** — `[tui_lcons]` is its own running position, advanced by 0-2 ticks a call, because holding an anchor and multiplying a growing elapsed against it overflows a word at 217 ticks and the overflow branch then slammed the estimate to `[trk_total]`, the MIXER's position: the field measured `PLAY-CONS` at +15,242 against a 16,384 ring, and QEMU could not, because the free-run only lasts that long on a machine whose byte rate matches the model. **And the tick is a staircase too** (SPEC.md §45.15.2): the accumulator only moves when `[ticks]` does, so every single-tick `PLAY` step in a field capture is 0, 302 or 604 and nothing between - a row is 2.549 ticks, so crossings quantize to a repeating 3,2,3,3,2 and the scroll limps 165/110/165/165/110 ms. That is what was left after the music went smooth and §45.13.5 was fixed, reported as "the text is what is microstuttering". The frames are the only finer clock available (§34.1 reserves the ch0 latch and no slot publishes it), so the PREVIOUS tick's frame count divides the current one - 1.81 frames a tick in the field, so half the quantization. Four things hold it up: the sub-term is an OFFSET added on the way out and zeroed at each edge, never added into the accumulator, so §45.15.1's whole argument is untouched; it is capped one byte below a whole tick, so the edge that zeroes it also adds `[tui_bpt]`; both re-anchor paths spend it; and **counting and capping must be separate** - clamp the frame counter at the divisor and the measured count can never exceed its own previous value, so the divider ratchets to 1 and the feature silently reverts to the staircase it was written to remove. **A report re-anchors the model only when it OVERTAKES it**: the driver's counter is quantized DOWN to a block boundary, so it is up to 2,048 bytes behind where the card really is, and restarting the model on it threw the interpolation away and froze the display until it climbed back - 26% of field row-gaps at 4+ ticks, now 0%. Measured: the longest run with the screen row unchanged went 7 ticks to 2, and every advance is exactly one row - and the FIELD (PCem, two captures) says 203 single-row steps with 0 backwards. **Two things those captures changed** (SPEC.md §45.16.1): a rebuild step forced a blit whether or not the row it formatted was ON SCREEN, and 19 of 82 shadow rows are visible, so 77% of them redrew nothing - 410 blits, 12.6% of the machine, in a 29 s capture; `ttx_shstep` sets `[ttx_vdirty]` itself now, for a visible row only. And `[ttx_fdiv]` is NOT a frame rate (the probe said 1 and 2 against measured 29.1 and 25.4 fps, because it runs at bracket entry where the machine is busiest), so the VU needles are stepped once per system TICK rather than every fdiv'th frame. **And the band is landed inverse by the blit's own pass** (SPEC.md §45.13.2): relighting 59 attribute bytes afterwards left the row drawn NORMAL for the ~9 ms of the copy, which on 1bpp text is the white bar going BLACK - reported from the field as "the highlighted line is flicking black", and made visible by the interpolation taking row changes from 2.4 a second to 7. **The frame clock is measured** (SPEC.md §45.16): `fsx_wait` also takes a vertical retrace — 50 Hz Hercules, 60 CGA, 70 VGA text — and the text screen can afford 3x the frames because the blit is change-driven, which takes the scroll's 110/110/110/**165** ms cadence to ±18 ms and stops a fast module's rows being dropped. `ttx_clkprobe` times 8 waits against `OSAPI_GET_TICKS` and takes retrace only for 1..7 ticks: the path is a POLL with a 3-tick timeout, so a dead status port would be 6 fps, and **zero is refused too** — which is what QEMU's dumb 3DAh toggle answers, so QEMU tests the refusal and the field machine tests the acceptance. Two things are per-FRAME and are re-tuned by the measured ratio: the rebuild chunk (4 rows is 25 ms and will not fit an 18 ms slot) and the VU decay, which `tui_vu_step` does per CALL. **The band is an attribute**, relit after the blit, so nothing has to remember which row used to be the band. And **every kernel drawing slot is off-limits after `fsx_mode`** (§53.1) — `[trk_tx]` says so, and `tui_msg_draw` returning early on it is what lets every status-message path in the app keep working unchanged: the message lands in `[tui_msgp]` and the text frame's own status line spends it. X and S are refused on that surface the §47 say-why-not way, and the entry path parks the user's back buffer (`fsx_mode` refuses while one is armed) and re-arms it AFTER `fsx_run` returns, not inside the bracket the way Smooth does. Two orderings are load-bearing and both are the §53 contract: `[trk_fs]` is set BEFORE `osapi_fsx_run` so the worker skips its lock-held render and does not park-and-starve the ring (§53.2), and cleared INSIDE the bracket main before it returns so `fsx_restore`'s desktop repaint paints Tracker windowed rather than redrawing the FT2 frame over the desktop — the one bug this found, and the same discipline `[mc_fsx]` already followed. `osapi_fsx_wait` is the §32 back-buffer present (§45.11) as well as the frame clock. Load is windowed-only — the §38 dialog is unreachable in a bracket), `modplug/` (SPEC.md §56 — a port of **ModPlug Player V2**'s look and feel: the skinned player window with its green LCD, LED transport row, scrubber, volume slider, option-button grid and spectrum/oscilloscope/VU pane, plus a Setup window with ModPlugPlayer's page list and a PlayList editor. The line it draws is the one worth knowing before touching it: ModPlugPlayer's own playback is **libopenmpt**, so the INTERFACE is ported and the replayer is this tree's own — an **independent copy** of `trkplay.inc` under `mpm_` prefixes, which means a replayer bug fixed in one player is *not* fixed in the other, recorded in §56.1 rather than left to be discovered. Four things it settled. **A package's second window cannot host a file dialog**: `fdlg_open` calls `inst_win_owner` and refuses when the answer is "nobody", and Setup/PlayList are deliberately UNBOUND so their close boxes reduce to `wm_hide` — so PlayList ▸ Add… passes the *player's* window. **A callback that repaints is not a `W_PAINT`**: the kernel orders one, a menu command or a click in another of your windows does not, so `mppu_refresh` arms `wm_clip_set` on the player and clears it explicitly — without it, opening Setup drew the player straight over the window it had just opened. **`OSAPI_FONT_RUN` is what makes that safe**: every text element on all three windows is an opaque run, so §11.3's granularity trap has no fill-and-glyph pair left to break and no per-cell gate is needed anywhere. And **two palette words are adapter questions, both learned on CGA**: the LCD's green is in the dither class and a glyph rounds to black, so the panel came out black-on-black (Missile Command's trap again), and a `CLGRAY` chassis reduces to a 50% checkerboard that every black glyph on it has to compete with — `[mppu_ink]` and `[mppu_body]` go white at 1bpp, which is the Macintosh look this OS is modelled on anyway. Its `rep stosb` padding wrote into KERNEL_SEG until it was caught, because **ES is KERNEL_SEG on entry to every callback** and `stos` writes through ES. **The face is drawn from an invalidation mask** (SPEC.md §56.12) — the period's update-region idea (`InvalRect`/`BeginUpdate`, `InvalidateRect`/`WM_PAINT`, Intuition's damage list) applied at the widget rather than the pixel, because an 18 Hz worker redrawing a panel that had not changed cost **158 glyph cells a frame** where §6.1.1 prices a cell at a millisecond on the machine this package exists for. `[mppu_dirty]` is a word of `MPPI_*` bits filled three ways: `mppu_poll` folds the cheap pure predicates (nine lamps, twelve option lamps, two slider handles, one status pointer) into signature words and compares them through `mppu_chk`, which is what lets ~14 event handlers keep one unconditional `call mppu_refresh` and still cost one lamp; `mppu_inval` covers what polling cannot see — the static face, `MPPI_BODY`, exactly three sites; and the LCD is neither, its four lines composed every frame and each line's **hash** deciding whether it becomes pixels, in `mppu_lcd_emit`, which all four builders already emit through. **28,365 glyph cells over ten seconds became 2,468 — 11.5x — measured with counters in `font_char`/`gfx_fill`, not asserted.** Four rules hold it up and all four are somebody else's rule first: a frame that is not drawn must not clear the bits it did not spend (`tm_rowok`); `mppu_draw_all` calls `mppu_ckclr` FIRST, the single "pixels died" site; `mppu_chk` never stores a zero key (`tm_elchk` — a blank LCD line hashes to zero, and would then never be drawn at all); and `mppu_layout` invalidates the whole face when the content origin moved. It draws **synchronously** rather than deferring to the worker, the one place it departs from the Windows model it copies: the worker is *hired*, `mpp_hire`'s refusal is transient, and a handler that only marked would draw nothing on a machine where the hire has not yet succeeded. **It arrived from `main` and SPEC.md §56.13 is what the port had to change**, which is worth reading because nothing failed to assemble — this branch's SDK is a superset, so every difference was silent. Two corrections. **Its greying was rule 1's own failure**: `OSAPI_GFX_PEN` does not exist on `main`, so it set `CDGRAY` and not `[gfx_dis]`, which is a real grey on VGA and **solid black** on Hercules and CGA — every greyed caption in the three windows pixel-identical to a live one while the frames around them dithered. `mppu_dpen`/`mppu_dink`/`mppu_dpen_off` are the fix, and they are **two entry points because `AX` is sometimes live** (`mpps_draw_box` holds the control's x across the branch that greys it, so a pen routine returning an ink in `AL` corrupts the rect it is about to draw — that was the first version's bug). And **the file dialog says how big the file is here** (§38.6's `DX:CX`), which `mpp_trim`'s own comment had named as the thing it did not have: the claim is the file's exact size now and an oversized pick is refused **before** the read, where 116KB of module is seconds of motor on the field machine. The banked size is **read-and-clear**, because `mpp_load_name` is also reached from a playlist advance that had no dialog and must not inherit another file's figure. Two decisions to leave things alone, both recorded rather than silent: **`MOD` stays pointed at Tracker** (§54 bakes it in, and two MOD players claiming one extension makes a double-click depend on load order), and **`WF_SNAP` was considered and not taken** — snapping the window aligns the content origin, but every run inside it also has to start on a multiple of 8, and this face's positions are ModPlug Player's own layout; Tracker had to move its columns to earn it, and here that means moving the skin. Its worker measures **98 bytes** of stack against this branch's 256-byte slice (§20.6 rule 6, half of `main`'s), which is Tracker's 92 plus six), `artful/` (SPEC.md §46 — a port of ActionRetro's ArtfulType, the distraction-free Markdown writer for classic Macs, onto the §11.2 fullscreen surface: the app draws its **own** Macintosh menu bar — black in Writer mode — and pull-down menus in the `sol_drag` press-drag-release idiom, styles markdown live from its own ROM-font glyph renderer (bold overstrike / italic shear / 2x-3x headings / underlined links / dithered code cells) with the caret's paragraph shown raw, wraps by raw widths so caret motion never reflows, and repaints one line as one `OSAPI_GFX_BLIT4` — the whole 4.77MHz performance story. **Its document is a heap claim too** (SPEC.md §46.9) — the 20KB gap buffer in bss was a holdover from a kernel with no memory API, and "works with an empty heap" was never true when the package's own region is a claim. `at_dresize` grows it, and the gap is what makes that more than a resize: the high run always ends at the ceiling, so it slides up AFTER a grow and down BEFORE a shrink. `AT_MAXKB` is 60 because every offset in the buffer and the line table is a word. The cost is `at_getb` doing `push es`/`mov es,[at_dseg]`/fetch/`pop es` per character. Its snapshot undo and big clipboard live in a second claim and degrade gracefully without one; its caret blink is the worker), `missile/` (SPEC.md §48 — Atari's 1980 Missile Command, ported from the 6502 sources in the sibling `missile-command` repo. The wave table, the smart-bomb schedule, the scoring multipliers, the explosion radius ramp and the city/base coordinates are the arcade's own numbers read out of `W3MAIN`/`W3COMN`; the trackball becomes the mouse and three fire buttons become "the nearest live base"; it runs windowed or on the §11.2 fullscreen surface and claims no heap at all. Three things it discovered are worth knowing before writing another package that draws long-lived vector-ish art. **A trail drawn in per-frame segments and erased as one whole line does not erase** — the two Bresenham rasterizations differ by up to a pixel in the minor axis, which left 104 of a measured 217-pixel trail on screen and turned every dead missile into a permanent dashed line; the erase dilates each flushed run by a pixel, which costs nothing because a run is a rect either way. **A refusal that is PERMANENT must not be coded like one that is transient** — "no free slot, try next tick" and "there is no city left to aim at" look alike at the call site, and treating the second like the first hung the game with a still screen on a perfectly live machine (twice: the other time via an on-screen counter that drifted past the launch gate, which is why that count is now derived every frame rather than maintained). And **text must come from SPEC.md §39.4's WHITE class, never its dither class**: the wave counter was `CLGREEN` and on CGA it was not faint but *absent*, because a dithered 8x8 glyph loses the half of each stroke the pattern masks out and a 1px stroke has nothing left. **A fourth landed later and is the one to read before optimising anything that draws here: SPEC.md §48.8.** On the two 1bpp adapters this game was unplayable, and the explosion was 65-81% of every `gfx_fill` it issued - but **not because the dithered frame was slow**, which was the obvious hypothesis and is measurably false (3.114 against 3.106 counts per fill, 0.26%: on mono `bb_ink`'s dither branch only arms `[bb_altm]` and the fill loop runs the identical instructions). It was slow because the dither belonged to a **colour cycle that changed every frame while the RADIUS held still for two to six**, so 27 full discs were drawn where three would have done. On 1bpp or CPU tier 0 the cycle goes, `mc_step3` gives the same 27-frame life three drawn states (radius and colour both hanging off that one table, so they cannot disagree), and `mc_blob` draws a disc as the five or six *nested rects* it exactly is instead of one fill per row. 750 fills a burst became 24. **The honest coda is that this did not buy full speed on its own** - `mc_line` coalesced horizontal runs only, so a near-vertical trail was one fill per row and one dead missile's whole flight path was erased in a single frame: the worst measured `mc_wipe_trails` call was 267 fills, about 310 ms, a five-tick stall. **SPEC.md §5.6's `gfx_line` is what finished it** (§48.8.3): a busy frame went 190 ms to 43.5 ms of a 55 ms tick, so the game now holds 18 fps on the machine it was written for. **SPEC.md §48.9 is the pair after that**, and both were reported as one complaint: a periodic hitch and an intermittent flicker, *windowed and fullscreen alike*, which is what said they were in `mc_rbody` and not in the window manager. `[mc_gdirty]` was one byte meaning "something touched the terrain" and bought the whole band plus six cities and three bases - **143 ms, two and a half ticks, five times in 86 frames** - so it is a damage SPAN now (16.5 ms, and verified byte-for-byte identical to a full repaint); `[mc_bdirty]` was the same mistake in miniature, three launchers redrawn on every shot, and is a bit per base; and `mc_draw_status` blanked the WHOLE strip and re-lettered it on every kill, which is PERFORMANCE.md Part 1's erase-and-letter pair in its classic form - three space-padded `font_run` fields now, with `OSAPI_WM_SNAP` so windowed mono gets the single-store path too. The trap they sprang is §48.9.1: **an optimisation that stops something being redrawn every frame inherits every place that used to rely on that redraw** - terrain is drawn after the bursts, so a repair holed a live one that no longer redraws itself each frame. §48.9.3 is the third and worst: `mc_draw_msg` runs EVERY frame and erased a full-width band and re-lettered it every frame the banner was up, so `DEFEND YOUR CITIES` and `BONUS POINTS` strobed at 18 Hz for their whole duration - and its own comment called that "affordable", which it was in WORK and never was on the glass. It is `[mc_msgp]` (the banner actually on screen, so an unchanged one costs a compare) plus one opaque centred `font_run` now. **The two questions "how often does this draw" and "does it blank while it draws" are independent, and this one routine had them wrong in opposite directions.** §48.9.4 is the last of them: the end of a wave was FOUR full repaints (258 ms each, about a second to change a banner and a score) and is one. Two changed nothing on screen, the third is terrain damage now, and the survivor is `mc_startwave`'s - which is not arbitrary, because `mc_clear_objects` clears object STATE and erases nothing, so only a sweep AFTER it wipes an ABM frozen in flight when the wave ended. **§48.10 is the correction a field log forced, and it is the second lost optimisation in this tree**: `mc_blob` traded 39 one-row fills for 6 NESTED rects, and nested rects overlap - 184 scan lines to cover a 39-row disc, 37.1 ms against `mc_disc`'s 36.4. A `gfx_fill` is 756us of arriving plus **177us per scan line**, so a call count is the right currency only while the calls are small. Bands instead of nested rects: same pixels, each row once, 2.4x. The second proposed fix - an erase cursor bounding the wipe - was *measured and dropped*: it existed to cap a five-tick stall and the stall is now one 59 ms frame, which `MC_LAGMAX` already absorbs, so it is not worth the shape of change that produced both of §48.5's wave-never-ends hangs. **§48.11 and §48.12 are what a STAGE-level field log found that a call count could not**, and the instrument is the point: one PIT-read span per phase of `mc_render`, which is honest even on an emulated CPU because the PIT is a real clock there too. It put a 55 ms frame at 29.2 ms idle and 65.8 ms busy and split it — `upd` 2.0/10.9, `lok` 5.3/5.9, `crs` 8.6/8.9, `trl` 6.3/**36.6**, `rst` 0.4/5.8, `unl` 5.1/5.1 — which says two *different* things. `trl` is the explosion and its cost is structural: modelling `mc_blob` against the coarse ramp gives 39 fills a burst, predicting 15.9 fills a frame at eleven concurrent bursts against the log's measured **15.8**, so the model IS the game and the only thing left to cut is how many times a burst is drawn. And an idle frame costs 29 ms of which 19 draws nothing — 8.6 ms of it the **crosshair**, four one-pixel arms XOR'd off and back on every frame whether the mouse moved or not. There is nothing to win inside those calls (an arm is a rect and 756us is what a rect costs to ask for), so §48.11 stops making them: the overlay stays on screen across frames and comes off only when the mouse moved (`mc_cross_moved`) or when a rect about to be drawn reaches it (`mc_cross_need`, hooked into `mc_fillc`/`framec`/`runc`/`line`). **The ordering is the whole correctness argument** — the erase runs BEFORE the overdraw, while the crosshair is still whole, so the XOR undoes what the XOR drew; and `mc_xorc` must never be hooked or the crosshair's own erase recurses. §48.12 is the explosion half: the **collapse** was 42% of a burst (16 of 39 calls) for one visible state, so the ramp grows and HOLDS and the life is cut 27→21 frames to keep Σr — all of a burst's lethality, and one table drives both the drawn and the lethal radius so they cannot disagree — within 3.3%. 18.3 ms a frame → 12.4. Both were verified the only way this can be: fire a cluster, pause, capture the framebuffer, force a full repaint, diff — **0 differing pixels of 262,144** each time. **The field run after those two moved the problem rather than ending it**, and that is worth knowing as a shape: quiet frames went 29.2 → 15.5 ms, and `wip` — the whole-trail erase — became 37.8 ms of a 73.5 ms frame *with only 5.2 line calls in it*, which is what said it was a per-PIXEL cost and not a per-call one. The dilated erase (§5.6.5) was three full Bresenham walks, so a 300-pixel trail is 900 mono read-modify-writes. **SPEC.md §5.6.6 is the fix and it is a kernel one**: the three passes share dx, dy and err and differ only by a constant on the minor axis, so on a STEEP line they are three columns of the SAME row — usually the same framebuffer byte — and one walk with a three-bit mask is the identical pixel set. The pixels are verified (0 differing, four runs, Hercules and CGA), and the speedup is **1.91x** - measured in guest INSTRUCTIONS by `gfxbench`'s transposed pair, after `tests/linetest`'s host-time 1.3-1.9x settled nothing, which is PERFORMANCE.md Part 4's rule the hard way. Missile Command sees less of it than that ratio suggests: only **38%** of its trail erases are steep on the field machine (a QEMU count said 53% - the real play says 38%). Solid ink only (the dither class alternates with x, so a single mask cannot carry the three), steep only (shallow means three *rows*, which on a banked adapter is three `gfx_rowbase` walks) and mono only. `tests/linetest` is the gate. **§48.13 is the one a single question found**: this is §53's reference consumer and it was never running a bracket on the machines the work was about. The exclusive path was reached only through Mode X, `fsx_caps` is **0x011 on Hercules** (TEXT80 | HERC, no Mode X), so `F` gave the §11.2 fullscreen WINDOW - an ordinary window that happens to cover the screen, still in the z-order, still pre-empted, still taking the gfx lock once a frame. Set 4 priced that at `lok` 6.2 ms + `unl` 5.7 ms EVERY frame, drawn content or not: **21.8% of a 77-second session**. The exclusive path had been tied to a mode CHANGE rather than to exclusivity, and §53.7 always said those are separable - "exclusive but same mode: a game loop that wants zero jitter, and that costs nothing to allow". `[mc_fsxm]` carries the FSXM_* to set or **0FFh for "do not switch"**; measured, `lok` and `unl` are **0**, not smaller. The trap is that **`[mc_fsx]` means the mode is FOREIGN and not "in a bracket"** - all ten readers switch to the Mode X twins or answer 320x240, and a same-mode bracket wants none of it - and the cheapest confirmation the bracket is live is that the DOUBLE CURSOR goes: the kernel keeps the system arrow over a §11.2 fullscreen window, and a held lock keeps it off. **§48.14 is what that same log said next**: `wip`, the whole-trail erase, was 37.8 ms of a 73.5 ms frame *with 5.2 line calls in it* - a per-PIXEL cost, and §5.6.6 could only reach the 38% of erases that are steep. A trail is ONE Bresenham now, laid at launch from the launch point to the point the missile is AIMED at, advanced a few pixels a frame by SPEC.md §5.6.7's resumable walk, and erased by re-laying the identical line and replaying exactly the pixels the draw emitted. That removes the reason §5.6.5's dilation existed rather than paying for it: the draw and the erase are the same walk, so "differ by no more than a pixel" stops being a question that can be asked, and the erase is one read-modify-write a pixel instead of three. Two things that look like trades are corrections - a dodging smart bomb's trail is a straight line now, where the polyline it used to draw was one the straight erase could never have removed; and the progress measure is the MAJOR AXIS clamped to the line's length, `[mc_ilen]` signed (+n = x-major, -n = y-major) because the SDK does not publish the walk block's layout and the clamp is what stops a dodged bomb asking for pixels past the end. The two that break silently: **the block holds SCREEN coordinates**, so `mc_track` raises `[mc_full]` when the origin moves and `mc_redraw_trails` re-lays every walk; and **`[mc_iarm]` = 2, "no walk", is not a failure state** - the Mode X surface (§53.1) and a trail with an endpoint outside the content box both keep the old segment-and-dilated-line path, which is why §5.6.5 is still live and still right. Verified the §48.12 way: 0 differing pixels against a forced full repaint on all three adapters, with trails in flight and again after they had died. **§48.15 is the shape of the cost rather than its size**: a trail arrives over sixty frames and still left in one, and one burst can kill five missiles at once, so a dead trail is QUEUED and spent a few pixels a frame instead - at eight times the rate it was drawn at, clearing from the launch point forward, which is the direction the walk can replay exactly and also the way smoke goes. Two caps answering different questions: `MC_DRNRATE` is per trail and jittered so a cluster does not finish together, and `MC_DRNBUD` is per FRAME across the whole queue, which is the one that actually bounds the cost - a jitter alone still lets eight entries ask for eight rates at once, which is the batch again with extra steps. Overflow erases inline, which is what every erase did before. Two things hold it up: `mc_drn_clear` at every full repaint, or the queue spends the next few frames erasing what the repaint drew; and **the damage marks moved from the push to the drain, onto different frames** - an ABM's launcher is under the FIRST pixels and an ICBM's ground bite under the LAST, so marking both at the push repairs terrain the erase has not reached and then holes it with nothing left to repair it, which is §48.9.1 in its exact form. **§48.16 is what the log after those two said, and it is the cleanest instance of PERFORMANCE.md Part 2's rule in the tree**: a stuttering second's MEAN frame is 46.2 ms against a 54.9 ms tick, so it is the tail that crosses and not the mean - and the two trail stages disagreed by 3.5x about what the SAME operation costs. `drn` moved 46.6 pixels a frame for 9.11 ms (~160us a pixel, which is just what a 1bpp line pixel costs - `gfx_lstep_mono` and `gfx_line_mono` are the same loop, so §48.14 made a pixel no dearer, it stopped drawing each one three times); `mov` drew about NINETEEN pixels for 10.20 ms, ~570us each, because it was one far call per live missile per frame and the drain already happened to hand several trails over per call. `mc_dsc_add`/`mc_dsc_run` collect the frame's steps into SPEC.md §5.6.8's `OSAPI_GFX_LSTEPV` - ten calls a frame become three, one per pen plus one for the drain. **The DIAGNOSIS there was wrong and the change is kept anyway** (Set 11, the 5150 itself): a walk step's arrival is **128.7us**, not the ~756 borrowed from §5.7's rect floor, so the batch is worth ~0.9 ms a frame and not ~6. What separated `mov` from `drn` was the **per-block setup** - `gfxbench`'s own "655us pixel" is a one-pixel BLOCK, within 2% of that machine's `GFX_PIXEL`, while the marginal pixel inside a multi-pixel step is ~175us. Three quantities, all measured, all different. Two traps: a batch is spent BEFORE `mc_setcol` changes the pen, because a descriptor carries no colour; and the drain's `mc_cross_need` calls still all run before any of its pixels, which is §48.11's ordering surviving on where the flush sits. **§48.17 is what the first working MAX line found** - the worst SINGLE frame of each second, which three previous logs' per-second means had been averaging away: 33 of 60 seconds hold a frame over one tick, the biggest stage in them is `exp` in 20 and `rst` in 8, and the worst three frames are 126/111/108 ms with `exp` at 76/66/74 of them. **The stutter is a SPIKE, not a load**, and both spikes are "everything at once". `rst` was `mc_draw_status` re-lettering all THREE fields - 29 glyph cells, ~29 ms on Hercules - every time `[mc_sdirty]` was set, which is on every kill, when only the score's last digit had moved; `mc_srun` keeps the text each field was last DRAWN with and emits the span from the first differing cell to the last, which subsumes a per-field dirty bit rather than needing one. `exp` was a salvo: the coarse ramp redraws a burst only when its drawn STATE changes and §48.12 left it three states in 21 frames, so bursts that detonate together transition together - `mc_add_exp` seeds `[mc_et]` with `rand mod MC_EXPJIT` instead of 0, which spreads them and costs ~1% of Σr because what a burst skips is the FRONT of the ramp and never the peak, with one table still driving both the drawn and the lethal radius. **The calibration earned its keep here too**: Sets 6-8 were PCem and Set 9 MartyPC, which nobody knew at the time, and the CAL line said so - cpu agreeing to 0.7% while the per-call floor differed by 16% and the per-scan-line by 21%. **§48.18 is what the first VALID before/after said** (Set 10, same emulator, per-call floor 0.15% apart): §48.17's two fixes did NOTHING at the frame level, and the second was instructive - `rst`'s 20-49 ms was inferred to be text because the worst frames held "only eleven fills", and **the fill counter cannot see a glyph**, so that was never evidence. The strip fix is kept (strictly less work, byte-identical output); the ramp jitter is DROPPED, because a salvo turns out not to be synchronised at all - an ICBM is intercepted as a blast radius GROWS, so a cluster's detonations were already spread. What the numbers do say, priced against their own calibration: a worst frame is 34.8 fills and 172.6 scan lines = **53.8 ms of `gfx_fill` in a 63.4 ms frame, 36.4 ms of it the ARRIVING alone**. So the only lever is the fill COUNT: `mc_blob`'s quantum R/8+1 → R/4+1 (9 fills → 7), two drawn burst states instead of three (`MC_EXPFR3` 21 → 15 to hold Σr at −3.4%), and the crosshair as two XOR bars instead of four arms (the centre hole falls out of the double XOR; 6.8 ms of every frame the mouse moves). Each is its own commit because each is a visible trade - **and the first was REVERTED on looks after being measured** (Set 12): it was worth ~4.3 ms on a worst frame, which is roughly the margin that took the median worst frame under a tick for the first time (52.4 ms against 54.9), and the burst lost its round edge for it. The measured middle if it is ever wanted is **R/6+1, which is ALSO 7 fills** at the only radius two states ever draw. The other two stand: `crs` 6.9 → 4.4 ms and `exp`'s MAX 73.8 → 47.1. **§48.18.1 is the negative result and it is the one to read before reaching for §5.6.8's trick again**: a vector `gfx_fill` was COSTED before being built and recovers ~4% - what a batch removes is the API cell, the clip test and the dispatch, ~170 of ~4,381 clocks, and what it cannot remove is everything that makes a rect a rect. A walk step's fixed cost was per CALL; a fill's is per RECT), `tamegram/` (SPEC.md §49 — a four-direction, dual-faction containment matrix contributed by Jason Page, credited in its About panel. Three things it settled are worth knowing. **A worker whose UPDATE shares scratch words with the DRAWING path must hold the gfx lock for both** — every UI callback already holds it, so a lock-free update lets `W_PAINT` land in the middle of `tg_fits` and the trial position gets half-evaluated against a different origin, which ends in a grid write past the end of the claim. It is affordable here only because the expensive half of a frame was always under the lock and the expensive half of an update runs once per piece lock, not once per tick. **The bounds test belongs at the index, not at the call sites**: `tg_gidx` answers CF=1 for an off-board cell, and every reader plus the single writer already went through it. And **a package's cell size must come from the LIVE content box, not from the screen** — 32 8px cells plus a HUD want 284 rows and CGA's desktop band has 136, so the matrix hung through the bottom of its own window and the HUD ran off the side; `tg_fillc`/`tg_framec`/`tg_str` clamp to the content box because the gfx primitives clip to the *screen* and `W_PAINT` runs unclipped). `taskmgr/` (SPEC.md §28 — **the former built-in Task Manager**, and the one built-in that could not be lifted out until SPEC.md §20.9's four cells existed: it read `sch_cycles`, `inst_tab`, `mem_tab` and seven constants of the memory ladder directly, because it was kernel code and could. It is a *viewer* — nothing in it can kill a task — so six kilobytes of kernel on every machine forever bought a window most sessions never open; as a package it costs ~7.3KB of heap while open and nothing when closed, and it came off **both** guards where a cold segment would have relieved one. It ships in **`SYSTEM/` on both floppies** — read-only on the system disk, an ordinary file on the apps disk (SPEC.md §28.3) — because the chip menu loads it by name off whatever disk is in A:, and on a single-drive machine that is the apps disk the moment the user swaps: `ui_tm_open` banks the current volume, mounts A:, steps into `SYSTEM` (`ui_tm_sysdir` — a second mount, since a folder's cluster is not known until the root has been listed), finds `TASKMGR.O88`, runs the loader and puts the volume back — the `drv_boot` dance. The menu item stays **live** rather than greying (SPEC.md §47 rule 3: the only honest test is the load), is a **singleton** from that menu, and reports the reason in a notice window when it fails). **Everything in `apps/` ships**; the gate packages that used to sit here (`fmtest/`, `sbtest/`, `filetest/`) are in `tests/` now, with the benchmarks.

**The fractal's restore cache (SPEC.md §40.1)** is the other half of the redraw work and the thing most easily broken by a well-meaning edit. There is no frame buffer — 320x170 at 4bpp is 27KB — so what is kept is a run-length copy, one word per row-run (colour in bits 15..12, last column in 11..0), and it holds **every pass**. It held pass 0 alone until this branch, which meant the cache **stopped growing at 25%** and every later repaint threw away three quarters of a computed frame and dropped the percentage back to 25 — *the* symptom, and the reason to read this paragraph before making the same trade again. Its stated reason had expired: image + bss had to fit a 7,168-byte slice of the 19,968-byte package pool, and that pool is retired (SPEC.md §20.1). The runs are a **heap claim that REGROWS** now — 4KB at the first kick, doubling on a row that will not fit, ceiling 32KB because `fr_cn`/`fr_cpos`/`fr_cmax` are words — because there is no right fixed size: measured with a C model of `frac_iter`, a whole frame is **340 bytes** for a deep zoom that is all interior, **15,366** worst case over the five types, five zooms and four palettes at their defaults, and **69,690** for a recentred deep zoom on the Burning Ship. A grow is taken only when the heap's largest free run is at least twice the increment (this is an optimisation, not the app), and it **always takes the base back from `DX`** — a grow that moved leaves the old segment pointing at memory that is not ours. Seven rules hold the whole thing up. `fr_kick` is the single invalidation point, because every view change already funnelled through it. `fr_cache_row` runs under the gfx lock, after the restart check and *before* the visibility check — under the lock so it is atomic against `fr_kick`, before the visibility check because a row nobody can see is exactly the row worth caching. `fr_restart` carries three values (0 idle / 1 restart / 2 resume), read with a read-and-clear `xchg`: a separate test and store could see the resume, have `fr_kick` overwrite it with a restart, and clear the restart away. **`fr_advance`, the `fr_prog` increment and the replay cursor's step all live inside `fr_emit_body`, behind that same restart check** — everything meaning "this row was consumed" belongs in one lock hold. Out in the worker's loop a stale step walks past the row `fr_redraw` just published to resume from; no pass ever paints it (pass 1 is rows 2 mod 4, pass 2 the odd ones) and the frontier can never match again, so the cache freezes too. **The row is implied by the state machine**, so that arithmetic is `fr_stepv`, in registers, with three walkers over it — the render, the cache frontier, and the resume point — because a second copy of it is a second opinion about what the cache contains. **A computed row's cursor follows the frontier** (`fr_cpos` = `fr_cn` after an append), or the append pushes `fr_cn` past the cursor and the next `fr_take` replays the row just stored onto the row after it — the one bug this rewrite shipped and caught. And **the replay is SPLIT, which is the performance argument rather than a tidiness**: `fr_redraw` puts the pass-0 prefix back inline because that is what "the picture is back" means, and the worker replays the rest a row per lock hold through `fr_take`. A row is ~40 runs at PERFORMANCE.md's ~756us a drawing call, so a finished 170-row frame replayed in ONE lock hold is seconds of frozen desktop; paced the way it was rendered it freezes nothing, and it is still an order of magnitude cheaper than recomputing rows at half a second each. `fr_prog` counts rows **computed**, never rows replayed, which is what makes a repaint free of it.

- `tests/` — **every package that is not shipped software.** Two kinds, and the distinction is what they assert. The **gates** answer pass/fail against a capability: `fmtest/` the AdLib FM surface (SPEC.md §34.2/§51.4), `sbtest/` the Sound Blaster streams (§34.5/§34.6), `filetest/` the write path (§18.4), `linetest/` SPEC.md §5.6.6's three-column walk (a deterministic fan of dilated steep lines and nothing else, so two kernels can be compared BYTE FOR BYTE over a framebuffer dump — which is the only thing "identical pixel set" can mean; it doubles as the benchmark for that path), `stackprobe/` the 256-byte task-stack margin (§8 — the one gate whose QEMU answer is NOT the answer: SeaBIOS hides a real BIOS's interrupt stack use, so its 360KB image exists to be run on real iron; docs/TESTING.md has the recipe). One is neither: **`trklog.inc`** is `apps/tracker` assembled a second time with `-DTRKLOG` (`make trklog`, SPEC.md §45.14) — a *recorder*, one line per system tick written to `TRKLOG.TXT`, for a field bug whose remaining question is a correlation between three clocks rather than a pass/fail. It is the same source as the shipped app, not a copy: every hook is inside `%ifdef TRKLOG` and `TRACKER.O88` carries none of it. The **benchmarks** answer *how fast*: `fontbench/` prices the primitive (§6.1.1), `typebench/` the keystroke (§11.94), `gfxbench/` the whole drawing surface on whichever adapter it booted on, windowed AND on the SPEC.md 11.2 fullscreen surface (the primitives should measure identically in both, and `FULLSCREEN in+out` is the only way a package can reach a `wm_paint_all` to time it — every other composition call forbids the gfx lock a callback holds) **plus the gfx lock itself, measured BACKWARDS** (`GFX_UNLOCK+LOCK pair`: `OSAPI_GFX_LOCK` from inside a callback is a deadlock, unlock-then-lock is the same two routines in the other order and is an idiom the kernel already uses there — §7.1; it is also the one row whose span cannot be interrupt-free, `gfx_lock` ending with `sti` by contract), `sysbench/` the machine underneath it. The last two share `tests/benchlib.inc` — the timing loop, the 48-bit arithmetic, the report arena and the file writer — and they **write their report to a text file** (`GFXHERC.TXT` / `GFXCGA.TXT` / `GFXVGA.TXT`, `SYSBENCH.TXT`) on the current volume, because forty rows do not fit a 640x200 screen and the output is meant to be carried off the machine into PERFORMANCE.md Part 9. Two consequences: **the bench floppy must not be write-protected**, and `gfxbench` is deliberately ONE package for Hercules AND CGA — both are the same 1bpp renderer over four numbers it reads from `OSAPI_VIDEO` at run time, so the two columns are the same measurement rather than two sources that can drift. Their `cli` window is one ITERATION rather than one row (fontbench's is a row), so the tick, the mouse and any sound refill land in no measurement at all. None of them is on a shipped disk — each rides its own scratch image, mounted with `make test TESTAPPS=build/<x>.img`, which builds that image on demand. `all` builds none of them and nothing under `tests/` is tracked; `make bench` and `make stackprobe` are the explicit targets, for building test disks without booting. (No artifact of theirs is tracked — but neither is anything else in `build/` now, so that is no longer the thing keeping them out of `all`; the `all` rule is.) The **`testing` branch** is where these are *developed*, so their iteration (two of three benchmark corrections so far were to the apparatus, not the thing measured) stays out of this history; a finished harness lands here. docs/TESTING.md.
- `tools/` — host-side Python: `os88pkg.py` (validates/stamps `.bin` → `.o88`), `os88disk.py` (builds FAT12 data-floppy images; `--verify` is a structural fsck, `--scramble` builds a legally fragmented test image), `qmp.py` + `mouse.py` (test drivers).

### Software package pipeline

```
apps/mines/mines.asm --nasm--> build/mines.bin (org 0)
                    --os88pkg.py--> build/mines.o88   (v3: validated, not relocated)
build/*.o88        --os88disk.py--> build/apps.img / apps360.img   (FAT12 floppy, drive B:)
```

The data disk is a standard **FAT12** volume (SPEC.md §19) — DOS, Windows, macOS and Linux all mount and write it, and since SPEC.md §18.4 so does os8088; every byte read off it is still treated as hostile. `disk_mount` validates the BPB against the 17-rule table in SPEC.md §18.2 before trusting any derived number, snapshots the FAT into `FAT_SEG` (ES-only, `dsk_next_clus` its single reader), re-shapes the root directory into synthesized 32-byte entries (volume label, LFN, subdirectory, hidden/system and deleted entries filtered; 8.3 display names like `MINES.O88`; 32-entry cap), and harvests icons by peeking each type-1 entry's first sector — a v3 `.o88` with the embedded-icon flag donates bytes 32..95, everything else gets the all-zero generic-icon sentinel. Loads go through `dsk_read_chain`, a size-driven cluster-chain walk with run coalescing: files a host OS wrote back fragmented load fine, a corrupt chain fails bounded as "Bad package", and FAT16 (reachable only on 2.88M test geometry — cluster count decides, per the Microsoft spec) differs only in `dsk_next_clus`'s entry decode.

**A volume switch is not a mount** (SPEC.md §18.9). `dsk_chdir` rebuilds the
LISTING because navigation needs it; a FILE OPERATION does not — `dskw_find`
and `fcp_scan` walk directory sectors themselves — so `dsk_chdir_q` stops
after the BPB, the FAT window and the cwd, skipping the scan, the sort and
**one icon-harvest read per file in the directory**. The harvest is why this
exists: a copy alternates between two volumes, so the destination's icons were
re-harvested on every switch and copying a folder got slower as it filled. The
trap is that a quiet mount leaves the global snapshot EMPTY and owed —
`disk_nfiles` goes to 0 (a wrong listing is worse than no listing) and
`[dsk_lstale]` is the debt, which `dsk_relist` pays by tail-calling
**`dskw_remount`, never `dskw_sync`** — `dskw_sync` defers when nothing on
screen is drawn from the listing (§18.4), so that pair would clear the debt
and then decline to pay it, which is a listing that is never rebuilt.

**Coming back from a document open is a file operation too** (SPEC.md §54.9):
`assoc_back` restores the volume so the app can READ ITS DOCUMENT, and a read
resolves by name through `dskw_find`'s directory walk, so it is
`dsk_chdir_q` — measured, opening `BEVERLY.MOD` from `APPS` went 295 → **284
sectors** (~2.6 s on the 5150), the mount COUNT unchanged because a quiet
mount is still a mount and what changed is the ~12 sectors one costs becoming
~1. Its debt is deliberately never collected, which is safe only because
`files_refresh`/`files_poster` paint from the WINDOW's cache, `fdlg_home_go`
ends in a full `dsk_chdir` on both branches, and `fmv_sync` has the test.
**The other two mounts of a document open are gone as well** (SPEC.md §21.4):
`ld_run_name` resolves through `dskw_stat` rather than a directory index, so
`assoc_try` needs no listing and is quiet too, and `assoc_run` reads the
poster's own `FS_DRV`/`FS_CWD` off its state block instead of calling
`fmv_sync` at all — a full mount that only ever existed to produce two words
`fmv_sync` itself takes from exactly there. Measured on the same document
open: 295 → 284 → **274 sectors**. **By name is also SAFER than by index** —
an index is resolved against a snapshot that can have shifted (§19.4), which
is docs/FIELD-NOTES.md 4; a name cannot be shifted into somebody else's
entry. The trap it sprang is worth reading before touching either entry:
`.peek` takes the cluster **in AX** as well as in `[ld_clus]`, undocumented,
so the first version handed `dsk_clus2lba` an attribute byte — it assembled,
every gate passed, and a document double-click silently did nothing.

**The
copy engine's paths back to the event loop must pay**, and it has two:
`fcp_stop` and the replace question's pause. The pause is the
sharp one — it is not an end, so nothing else would reconcile it. **The
other three debtors owe nothing at all**: `drv_mounted` and `drv_vol_back`
(both quiet now — going to A: to write `SYSTEM.CFG` and coming back are file
operations, not navigations, and no Disk window paints from the global
listing) and §18.4's deferred write. A debt nobody ever collects is the
right outcome there rather than a leak: it costs one word of state and means
the sectors were never spent.

**EVERY reader of the global listing must consult `[dsk_lstale]`, and a
`(drive, cwd)` compare is NOT that test** — the binding rule, learned by
breaking it twice in one commit. The debt leaves the globals naming the
*right folder* with **no listing in it**, so "are we already where we want to
be?" answers yes about an empty snapshot; before the deferral that state
could only follow a volume switch, so the compare was accidentally
sufficient. `fm_focus` had the test; `fmv_sync` and `fmv_bcast` did not, and
both failed **silently** — a double-click after a Control Panel close
launched *nothing at all* (and cost no I/O doing it), and deleting one file
redrew its Disk window as `Drive B: 0 files`. Both are
docs/FIELD-NOTES.md 4's family. The two fix shapes: a reader that can re-list
where it stands falls through to its own load path (`fmv_sync`); one that
adopts the snapshot wholesale pays `dsk_relist` first (`fmv_bcast` —
idempotent, free when nothing is owed).

**A copy costs two volume switches per file** (SPEC.md §22.5), down from five.
The destination is created WITH its first chunk instead of empty (so the first
chunk does not cost a switch back to a source we just left, and an empty
file's directory write and FAT flush stop existing), and the loop ends on
`[fcp_rsz]` instead of switching to the source to be told there is nothing
left. `fcp_rdnext` is **one `dsk_read_chain` call per chunk**, not one
`disk_read` per cluster — that walker already had the run coalescer and only
needed making resumable (`[dsk_chain_end]` = the last cluster it touched).
Two traps. **The chunk must be a multiple of BOTH volumes' cluster sizes**
(`fcp_clspan`): the destination's for `dskw_append`'s precondition, the
source's because a take ending mid-cluster skips the rest of it and loses
those bytes — it was `dskw_clbytes` of whichever volume was current, which was
only ever right because every partition had 512-byte clusters. And a chain
that ends before the size says it should is an ERROR now, where the old loop
returned short and the caller read that as "finished" and truncated in
silence.

**Writing** is `kernel/diskw.inc` (prefix `dskw_`, the only caller of `disk_write`): seven operations — write (create or replace), read, delete, rename, dfree, plus `dskw_mkdir` and `dskw_rmdir` for folders (SPEC.md §18.5/§18.6) — the first five reached by the OS directly and by packages through API slots 0x0120..0x0140, UI-task context only. Names resolve in the volume's **current directory** (`[dsk_cwd]`, SPEC.md §19.2), not the root. Three rules are binding and easy to break by accident. (1) **Commit order**: allocate + write the data, flush the FAT, *then* write the directory entry (one sector — the commit), *then* free the replaced chain and flush again; a crash leaks lost clusters, never a cross-link. (2) **Rollback**: any failure before the commit re-reads the FAT off the disk (`dskw_refat`), so a half-built chain cannot survive in RAM to be flushed later. (3) **Coherence by mark, then remount only if somebody is looking**: a successful metadata change always marks the views of the folder that changed (`fmv_mark`, SPEC.md §22.8) and re-runs `disk_mount` **only when `fmv_gneed` says something on screen is drawn from the global snapshot** — in practice only the Standard File dialog, whose New Folder button is a write made while it is up. Otherwise it publishes `disk_nfiles` = 0 with `[dsk_lstale]` raised, which is the identical state a quiet mount leaves (§18.9), and `dsk_relist` pays. `disk_dir`/`disk_icons`/`disk_nfiles` are therefore still *always* either exactly a mount snapshot or nothing at all, never a patched one, and no new staleness rule enters the kernel. **The old rule was "every write remounts" and it was too strict** — it was written when the only writer was the file manager acting on the window the user was looking at. Measured: a Control Panel close while the user is on B: was 3 mounts / 47 sectors and is **2 / 28** (~4.5 s of floppy on the 5150), and an app saving into a folder no window shows was 1 mount and is **0**. Writes are gated on `[dsk_mntok]`, set only by a fully successful mount — which is why the boot floppy (no valid BPB) can never be written. Verify write changes with the `tests/filetest` gate package (`make test-snd TESTAPPS=build/filetest.img`, plus the `-frag` image) **and** `python3 tools/os88disk.py --verify <img>` from the host afterwards — the in-kernel free-space check and the host fsck catch different bugs.

 Packages are format v3 (SPEC.md §20.2) and **own a segment**: assembled at org 0, loaded on a paragraph boundary claimed off the top of the heap (`mem_claim_hi`), bss zeroed, entry far-called with DS = CS = the package's own segment. There is no relocation of any kind — no dual assembly, no reloc table, no author rule about whole-word addresses — and `tools/os88pkg.py` is a validator rather than a generator.

Three things carry the boundary, and each is solved once rather than per call site:

- **Calling out.** Every API slot is an 8-byte cell that switches DS and `retf`s, and the SDK makes `OSAPI_X` a `%define KERNEL_SEG:offset`, so `call OSAPI_X` is a far call and **no package call site changed** when this landed. Three cells in ten defer to a longer stub: **X stubs** put the caller's DS in ES so the kernel can reach package data (`wm_create`'s template, `font_str`'s string, the spawn fence, the claim owner); **N stubs** stage a file name into the kernel's own buffer first.
- **Calling in.** The window record carries **one** far pointer, `W_DISP`/`W_SEG`, aimed at a three-byte **dispatcher** in the package's header (`call bp` / `retf`, at `PKG_DISP` = 12). Every callback stays an ordinary near proc with a near `ret` — a package author never writes `retf`, so a missing one cannot exist — and dispatch is re-entrant across packages because the pointer comes out of the record, not a global. `wm_pkgcall` is the single site.
- **Reading what you were handed.** **ES = KERNEL_SEG on entry to every callback**, because the window record and the file dialog's name buffer live there. `[es:bx+W_W]`, not `[bx+W_W]` — without the override a package reads its own image at that offset, which assembles cleanly and runs wrong.

Each instance may own one worker task, spawned from a callback and torn down through `OSAPI_TASK_ALIVE`. **Multiple packages — or multiple instances of one — run at once**; closing one frees its region *and every heap claim it held*. **The apps disk is **foldered** (SPEC.md §19.2): the root holds `APPS`, `GAMES`, `MEDIA` (the shipped `BEVERLY.MOD`, and where a File Open or Save starts — SPEC.md §38.10) and `SYSTEM` (`TASKMGR.O88`, the chip menu's, for a single-floppy machine — SPEC.md §28.3), so a package is two double-clicks away; the system disk carries `SYSTEM` and an empty `MEDIA` too. **The listing is sorted by name in `disk_mount` (SPEC.md §19.4)**, so the Makefile's order does not reach the screen and nothing may be built on it — it used to, which is why packages had to be appended at the end of their folder and the scripted tests clicked by that index. Sort where the snapshot is built and the Disk window, the file dialog and every view cache get it for free; the sort runs **before the icon harvest**, which is the only reason `disk_icons` never has to be permuted alongside `disk_dir`. **The `..` row is synthesized in the same place** (SPEC.md §19.5): slot 0 of a subdirectory's listing, ahead of the sort, carrying the parent's first cluster — so it is first in both views, dives like any folder, and the file dialog stopped synthesizing its own (a display row IS a directory index there now). It is **type 3**, not 2: everything that navigates tests `type >= 2`, and `fm_arm_sel` refuses 3 so Rename and Delete cannot be armed on it. A name comparison against `'..'` would have been the wrong test — the species filter drops the on-disk dot entries, so a volume may legitimately hold something else that displays as `..`.** A package's file name is an 8.3 stem, so it is not always the app's name: Solitaire ships as `SOLITAIR.O88` and carries `SOLITAIRE` in its 16-byte header field, which is what the dock and the Task Manager show.

### The mouse is on COM1 or COM2, and nobody is asked which (SPEC.md §9.5)

Both ports are probed for a UART, every one that answers is programmed,
hooked and listened to at once, and **the first to deliver a run of clean
packets wins**; `mou_lockon` then retires the others (IER 0 + the IRQ masked)
on the UI task's next pass. No Control Panel row, no `SYSTEM.CFG` key, no
build knob — §9.4's reasoning, that the machine answers faster than the user
and a stored answer is one more thing to be wrong after the card moves.
`mou_pall` is the idiom the whole module is written in: a row the probe
rejected has its base **zeroed**, so every UART write is a presence test
nobody had to write, and a one-card machine runs the sequence it always did.
The **base decides the IRQ** (3F8→IRQ4, 2F8→IRQ3) — a jumper that disagrees
is out of scope, as before — and a port is probed *before* its IRQ is
unmasked, because arming IRQ3 on a machine with no COM2 card arms a line
nothing drives. The probe is the **divisor latch** (two values written and
read back with DLAB set), not the +7 scratch register, which an original 8250
does not have.

**The hard part is that the other port usually has a modem on it, and a Hayes
result code is a well-formed mouse packet.** The protocol asks only for a
bit-6 byte then two without, and `'O','K',CR,LF` is exactly that — while bit
5 of the header is the LEFT BUTTON and bit 5 is set in every lowercase
letter, so a modem saying `no carrier` is a modem clicking. Three rules, each
put there by a case that beat the one before it, and the middle one is the
lesson: **a count that never decays cannot tell a mouse from a modem**, since
four result codes *seconds apart* accumulate a run of four exactly as four
mouse packets do. So the run must be of CLEAN packets — the ISR breaks it on
either protocol violation (a bit-6 byte mid-packet, a bit-6-clear byte
between packets), which kills `RING` on its second byte — `mou_newround`
clears every part-built run at each reset edge, and **each port decodes
independently** (`mou_phase`/`mou_b0`/`mou_b1`/`mou_run` are per-row arrays,
which is why BX stays the port row into `.pkt`). That last one is the trap
worth stating twice: shared, a modem's byte resynced the MOUSE's packet and a
modem's violation reset the MOUSE's run, so a chattering modem starved a
mouse on the other port out of ever being found while winning nothing itself.
**A device that cannot win must not be able to stop the one that should.** A
packet on an unsettled port is counted and *discarded* — no cursor motion, no
event — and `[mou_need]` is what keeps that free: **one port is not a
contest**, so a single-UART machine uses a threshold of 1 and behaves exactly
as it did before COM2 existed; only two live ports pay `MOU_LOCKN` = 8, which
costs a fifth of a second of the first movement of the session and nothing
after. Test it with `make test MOUSEPORT=com2` (a live but silent UART at
3F8 and the mouse at 2F8 — the hard case; an unpopulated 3F8 tests only the
easy half), and **`MOUSEPORT=com2irq4`** for §9.5.2's cross-wired card. Cost:
302 bytes of `.text` and **nothing** against `KERN_BUDGET`, the growth landing
in the image's existing padding to `OVL_START`.

**A real 5150 BIOS under QEMU's `-bios` was asked about and does not work** —
QEMU has no XT-class machine, and the 5150 POST reads its DIP switches through
an **8255 PPI at 60h–63h** where every QEMU machine has an **8042 at 60h/64h**,
so it fails before video. `-bios` maps a file; it does not change the hardware
under it. 86Box with the real ROM sets is what that question wants and
`make xt` and friends already are it. SeaBIOS *does* misrepresent a period
machine — int 1Eh, short int 13h reads, interrupt stack use — but none of that
is reachable this way (docs/TESTING.md).

### With no mouse, the arrows ARE the mouse (SPEC.md §9.6)

Gated on `[mou_seen]` - the byte §9.4/§9.5 already keep - so a machine whose
mouse has spoken never enters any of it and pays one compare per keystroke
(verified: arrows still reach apps untouched). **An 83/84-key keyboard has no
separate cursor pad**, so the arrows ARE the keypad and NumLock decides which
you get: NumLock OFF gives `AL = 0` + a scancode, and those are the keys this
takes; NumLock ON gives ASCII digits, always untouched. An app cannot have
arrow keys on a mouseless machine while this is on, which is the right default
- without a pointer it could not have been launched - and **ScrollLock is the
escape hatch**, READ as a level out of `KB_FLAG` bit 4 (`0040:0017`) rather
than watched for as a key: it is a shift STATE, int 09h swallows it and int
16h never reports one, so the first version's `cmp ah, 0x46` waited for a byte
that never arrives. **The button is ONE action on three keys - keypad 0 (Ins),
keypad 5 and Space - and it is neither a click nor a hold** (SPEC.md 9.6.1).
There used to be two, and they were the wrong pair: a *click* (MDOWN+MUP
together, `mouse_btn` untouched) **can never open a menu**, because
`menu_track` draws the pull-down and its next instruction polls a level that
is already up - which reached the field as *a menu that flashes and will not
stay open* on a Compaq Portable III - while a *hold* opened menus and read as
broken on every icon and close box. Now a press latches the level and posts
MDOWN, and **`kbm_ui` releases it once the ui_task pass that dispatched that
mouse-down is over**: a press is a CLICK for anything that dispatches and
returns, and a HOLD for anything that does not return until the button comes
up. The release is gated on that pass having popped *that* MDOWN, or a press
arriving behind an older event is released before it is dispatched - the
flashing menu from the other end. **A repeat is not a press** (`KBM_GAP`
again): typematic is ~1.8 ticks and a deliberate second press is 4+, and
without the guard a press held a moment too long opened a menu and closed it
on its own repeat. **Five loops spin on that level and the list used to name
three**: `menu_track`, `ui_drag`, `ui_grow` call `kbm_pollm` beside their
`task_yield`; **`osapi_mouse` calls `kbm_poll`**, gated on `[mou_seen]`, which is the same loop by another name
and is what makes a PACKAGE's drag reachable - Solitaire's `sol_drag`, Paint's
`pt_wait`, Note Pad's selection - where this used to say they were not; and
`fm_drag` is served by **not spinning at all** with no mouse, because it waits
with the button down to tell a click from a drag and the button is latched for
the whole dispatch, so a row click would cost two presses and a double-click
four. File drag and drop wants a mouse, as it always did. **What happens to a key
the poll does NOT claim is the whole difference between `kbm_pollm` and
`kbm_poll`, and getting it wrong is a machine you cannot get out of**: int 16h
AH=01h reports the HEAD of the BIOS buffer, so an unclaimed key left there
makes every later key queue behind it, invisible - the press that would close
the menu included. The modal loops EAT it (they read no keys of their own, and
a key typed into a menu is discarded, which is what modal means); `osapi_mouse`
LEAVES it, because a package's loop polls int 16h itself and eating its
keystrokes is worse than the bug. It reached the field as a Compaq Portable III
stuck in a menu with a live machine underneath it, wedged by keypad 5 - which
was unclaimed there because it is the one keypad key with NO cursor function,
so what a BIOS puts in `AL` for it is that BIOS's business. It is matched on
`AH = 4C` alone now; turn NumLock ON under QEMU to get the field's byte. **ScrollLock is the
ONE latch, and the rule it encodes is that the way back to typing can never be
a key you would want to type** (SPEC.md 9.6.2). A '.' was added as a second,
friendlier hatch and was wrong within a day: '.' is wanted the moment anybody
opens Note Pad, reserving the keypad's gives up Del, and letting a typed one
latch but not un-latch leaves a full stop that works in one direction only.
ScrollLock costs no keystroke at all - int 09h swallows it - and on a keyboard
with the lamp the machine SAYS which mode it is in, a state indicator nobody
had to draw. Space is the deliberate counter-example: printable and taken,
because a hand on the arrows wants a button under its thumb, and it is only
taken while the pointer is live. **It raises the window under the pointer on
the way in** - a level has no keystroke to hang that off, so `kbm_ui` banks it
in `[kbm_slast]` and finds the EDGE once per pass; a falling edge raises
nothing. **The step accelerates
and the ramp RESETS on a change of direction** - not a refinement: without it
the correction travels at full speed too and a menu item sits unreachable
between two 24px strides, which is exactly how the first version failed to
pick one. **It costs 520 bytes and two 512-byte steps of `KERN_BUDGET` (spare
4,096 → 3,584 → 3,072)**, the first decided on when that step was half the
slack rather than an eighth of it, the second bought by 9.6.1/9.6.2 - **114
bytes of code for a whole step, because the image rung had ONE byte of slack**
(`.text` + `.bss` = 49,151 against a 49,152 ceiling), so any addition at all
cost the same 512 and trimming the feature could not have avoided it. That is
the guard working rather than the feature being dear, and it means the next
byte added to `.text` anywhere pays 512 too. It is
written down in docs/KERNEL-MEMORY.md and in the module as the FIRST candidate
to remove (or move to test/bench-only kernels) if footprint ever outranks it.
Recommend that; do not do it unasked.

### The clock is a ladder, not a BIOS call (SPEC.md §37.90)

`int 1Ah` AH=02h..05h is the **last** rung. An XT BIOS implements AH=00h/01h and
nothing else, so on a 5150 with an AST SixPakPlus the BIOS knows nothing about a
clock that is sitting right there; and a BIOS that implements the two *read*
functions may still `iret` out of the two *write* ones — a clock you can read and
never set. `clk_probe` walks four rungs (MC146818 at 70h/71h, then RP5C01/TC8521
at 2C0h, then MM58167 at 2C0h, then the BIOS) and `clk_rtc_write` dispatches on
`[clk_tier]`.

Three things about it are load-bearing:

- **Probe order exists so that no rung writes to a chip a later rung would have
  identified differently.** Two different parts live at 2C0h. The RP5C01 rung is
  claimed **only** when its digits decode to the same hour, day, month and year
  `int 1Ah` just reported — one test that confirms the chip, the base, the
  addressing mode and the MODE page with **no writes at all** — so it runs first
  and a machine whose BIOS cannot read the clock can never reach it. The MM58167
  rung, which does write (a scratch nibble, restored on the single path out of
  `clk_ns_half`), runs after.
- **Every loop is bounded.** The one way to hang is to wait forever for a bit that
  never changes on a machine where every read is 0FFh — the exact bug Linux
  shipped until v5.11. The UIP poll takes its `pushf`/`cli` **per access**, not
  around the loop: 2.3 ms with interrupts off is forty tick periods.
- **The chip's own settings are obeyed, never rewritten.** Register B's DM
  (0 = BCD) and 24/12 (1 = 24-hour) polarities are both counterintuitive and both
  belong to the machine's BIOS; flipping them behind its back makes the clock read
  wrong from DOS afterwards. 12-hour mode's PM bit is stripped *before* BCD
  decoding and re-applied *after* BCD encoding — the other order feeds 8Ch to
  `clk_tobcd`.

`RTC=` in the Makefile forces one rung so the other three are testable at all
(see Commands above); the Control Panel's Date/Time page names the rung that
answered, because on a machine whose clock will not hold a setting that is the
whole diagnosis.

### Extended memory, window geometry and About (SPEC.md §41, §12.2, §11)

- **The API slot numbers.** Everything above 0x01B0 moved **down 88 bytes**
  once, when five cells that had been **held empty** and ten more RESERVED
  were closed up (SPEC.md §20.3); that is the third and last time that block
  has moved. Two of the five held cells were *filled* rather than dropped
  (`OSAPI_SND_FM`/`OSAPI_SND_STREAM` at 0x00F8/0x0100, now the loadable
  sound driver's).

  The rule that governs the table is **a RELEASED slot keeps its contract**,
  and "we no longer implement this" is a refusing stub, not a reuse. A slot
  introduced since the last release is NOT shipped and may still be changed
  freely - renumbered, re-contracted or withdrawn - because nothing outside
  this tree can have been built against it; the obligation is to get the
  contract right before it goes out, not to freeze it the moment it is typed.
  `gfx_line` is the worked example, its thin-or-dilated `SI` contract having
  been replaced outright rather than kept alongside a successor. Reusing
  0x01C8 for a KB-counting `mem_avail` where a paragraph-counting one had
  been published would fail silently and by a factor of 64 — which is why
  that block was *moved* rather than overlaid. SPEC.md §20.8 rule 4 is the
  written form; **renumbering invalidates every `.o88` at once** and is only
  survivable because every package is in this tree and `make` rebuilds them.

  **No app reads the window record through ES any more.** `wm_geom` answers
  content size and visibility, so Fractal and Note Pad ask the kernel instead
  of dereferencing a pointer whose segment they only held by convention. The
  record is still readable and the SDK still publishes the offsets; it is no
  longer the idiom, and the worker-task case (Fractal) was the one where the
  convention was thinnest.
- **`OSAPI_WM_GEOM` (0x01B0).** Content width/height and visibility in one
  call. Reading `[es:bx+W_W]` still works here and most apps still do it, but
  those are FRAME dimensions and every caller repeated the same
  `-2` / `-TITLE_H-1`; this is that subtraction in one place.
- **`OSAPI_ABOUT_SET` (0x01E0).** The app's name in the bar becomes a
  one-item pull-down, `About <Name>`. The cell is **appended last** in
  `menu_bar` so the app's own menus keep bar index == set index + 1 and
  `ui_dispatch`'s `dec ah` needs no adjustment; `[menu_abcell]` names it.
  Both its strings live in one kernel buffer — the item is `'About Paint'`
  and the title is `menu_abstr+MENU_ABPFX_LEN`, the same bytes from the name
  onward — so its `MB_SEG` is 0 even under a package. Solitaire and Arkanoid
  ship credit panels behind it; Arkanoid's also holds its **worker** off the
  content while the panel is up, or the game would draw underneath it.
- **`dskw_readbig` (0x01E8) — arrived, and has since been folded away.** It
  was the one file op with no 64KB ceiling; `dskw_read` has none either now
  (see "One read, one write" below), so the slot is a refusing stub and the
  SDK publishes no name for it.
- **`cpudet.inc` + `xmem.inc` (§41).** CPU tiers, the A20 line and the store
  above 1MB, across five slots. On tier 0 — the target machine — all of
  it is zero KB and every entry point returns having touched no port. The
  claim heap is unaffected: §50 is still the answer for *conventional* memory
  a package cannot fit in its own segment, and §41 is the answer for bulk
  data that does not fit conventional memory at all. The Task Manager shows
  it as one `XMS used/sizedK` line **below** the package-pool map, with no map
  of its own — real mode has no address for it, so it is in neither of the
  two maps above (SPEC.md §41.6).

**That store is what first pushed `KERN_BUDGET` past 64KB** — the first of
nine granted moves; the constant's comment in `kernel/kernel.asm` carries the
full history. It stands at 86,528 today, the fifth move having been *downward*
once the optimisation passes had left over 9KB of unexamined slack under it.
The 64KB *segment* limit (`KERN_CODE_MAX`) is untouched and unraisable: 16-bit
offsets. `BOOT_RELOC` used to move whenever the budget did, dragging a
constant across two separately-assembled files and raising the minimum machine
each time; it is gone (SPEC.md §2.7) and the sector computes its own address.

The apps disk is **foldered** and nothing of ours is loose in its root:
`APPS/`, `GAMES/`, `MEDIA/` (`BEVERLY.MOD`, and where a File Open starts —
SPEC.md §38.10) and `SYSTEM/` (`TASKMGR.O88`, which is there for
`ui_tm_open` and not to be double-clicked — SPEC.md §28.3), via a `DIR:`
prefix per package in the Makefile, so a package is two double-clicks away
rather than one. **The system disk carries the same two kernel folders**,
its `MEDIA` empty (`os88disk.py --folder`), because the dialog's default has
to exist on whatever volume the user is on and a boot floppy has no media.
**Nothing may be built on a root index**; the listing is sorted by name
(SPEC.md §19.4).

### Hard disks are a driver, and a volume is an index (SPEC.md §18.7/§52)

`[disk_drive]` is a **volume index** — 0 = A:, 1 = B:, 2 = C: — and never an
int 13h drive number, which is what keeps `'A'+drive`, `FS_DRV`, the desktop
zones and `osapi_file_here` all working untouched. The int 13h drive, or a
`DRVC_DISK` driver's own handle, lives in the `dsk_vtab` row; `dsk_xfer`
branches once at the top and a driver-backed volume goes out to `DSV_BLK` with
**the same volume-relative 16-bit LBA**. The driver adds its own 32-bit
partition base, and that is the whole of what "partitions" means to the
kernel — which is why the FAT layer, the directory walker and the write path
are the floppy's code, unchanged. A volume caps at 65,535 sectors (31.99MB),
which is both BPB rule 8's existing refusal and the DOS 3.3 limit these
machines ran; more capacity is more partitions.

**Every volume can own its FAT window, floppies included** (SPEC.md
§18.8.1/§18.8.2) — `DSK_FAT_SECS` sectors of heap, claimed at
`osapi_vol_add` for a driver volume and at mount time for a floppy
(`dsk_fatw_want`), with `[dsk_fatseg]` naming the live one. **The floppy half
is measured rather than assumed**: a floppy mount is ~12 sectors *regardless
of what is in the directory* (B: root with 1 package = 11, `APPS` with 9 =
12, `GAMES` with 5 = 12), so it is BPB(1) + **FAT(9)** + dir(1) +
`ASSOC.DAT`(1) — the icon harvest, which looks like the expensive part, is
already answered from §54.7's per-volume icon cache. **The gate is 128KB of
FREE heap**, not the 4.5KB the claim needs, retried per mount rather than
latched, and a refusal just shares as before. Reuse needs a QUIET mount
**and** a matching `dsk_bpb_sig` — a position-sensitive 16-bit signature of
the boot sector the mount has already read, so revalidation costs no I/O. It
**cannot** tell two os8088 disks of the same geometry apart, because
`os88disk.py` pins `BS_VolID` for reproducible images; that residual swap
case is accepted deliberately, since a full mount (every navigation) always
re-validates. The three per-volume arrays are in `.text` with real
initialisers, which is a **latent-bug fix**: `dsk_fatwc` was `.bss`, `-f bin`
zeroes none, and `dsk_fatw_pick` reads it as a SEGMENT. Measured on the
Control Panel case: 47 → 28 → **10 sectors**, and **9** once §18.4.3's
`dskw_find` arrived from `elendilon` — different sectors (the FAT re-read at
the switch against the directory re-read at the open), so the two compose
rather than overlapping. That is what stops a copy reloading nine
sectors on every switch: 45 mounts, 3 loads. A floppy gets none (its window
is the whole FAT and never moves) and a refused claim just shares, as
everything did before. Four traps: only a QUIET mount may reuse a banked
window; a dirty one is flushed at the switch rather than carried, because
`dskw_flush` later would write volume-relative LBAs to the wrong disk;
`[dsk_fatw0]` and `[dsk_fatd0]` are in `.text` with real initialisers,
because `dsk_fatw_park` runs at the first mount and a zeroed word reads as
"resident and dirty"; and freeing a live window must put `[dsk_fatseg]` back.

**The FAT is a WINDOW now** (SPEC.md §18.8). `FAT_SEG` stopped meaning "the
FAT" and started meaning "these nine FAT sectors" — a 32MB FAT16 volume's FAT
is 254 sectors and there was never going to be room. A floppy gets the
degenerate case byte for byte: the window covers the whole FAT and never
moves. Five routines are the entire blast radius, because `FAT_SEG` always had
exactly one reader (`dsk_next_clus`) and one writer (`dskw_setfat`). Three
traps: `dsk_fat_ofs` splits FAT12 from FAT16 because a FAT16 entry's absolute
byte offset is `clus*2` and **overflows a word** at 65,524 clusters (the
sector and the in-sector offset each fit; their product does not); rule 16
compares SECTORS for the same reason; and `dskw_refat` is an invalidate with
no I/O, whose reach is now shorter — an eviction may already have flushed part
of a half-built chain, which costs LOST CLUSTERS and can never cross-link.

**Mount is per PARTITION** (SPEC.md §52.4): it walks all four slots and mounts
every FAT one, so a disk partitioned in three comes up as HDD C, HDD D and
HDD E with three icons. Two tests decide and both are needed — the type byte
(01/04/06/0E, never extended or FAT32) and whether the volume actually mounts,
which is the half a type byte cannot answer. **The KERNEL names them**: the
driver passes no label, because the drive letter is the kernel's to assign, so
`HDD C`..`HDD F` derive from the volume index the way `Disk A` does.

**The mount survives a reboot** (SPEC.md §52.6), and it does so **inside
`SYSTEM.CFG`** rather than a file of the driver's own (SPEC.md §51.9). A
separate `HDD.CFG` was the first answer and the boot cost killed it: a second
directory search, a second read, and — because every file slot resolves in the
*current* volume and directory — two full **remounts** around them to get back
to A: and then back to where the driver was. `OSAPI_DRV_CFG` (slot 0x0290) is
a `rep movsb` into a file the boot already reads. The kernel carries
`DRV_BLOB_SZ` = 34 opaque bytes, knows the key's name and length and nothing
about its contents, and **round-trips them untouched on a machine whose driver
never loads** — deliberately unlike §51.5 rule 1, because this key *is* known
and only its meaning is not. The price is those 34 bytes of `.bss` reserved on
every machine, hard disk or not.

The driver reads the blob at **`DRVV_READY`** — a verb the kernel sends right
after publishing a driver's services, and the earliest point at which any
fence keyed on the publication slot will answer, because attach deliberately
runs before it is armed. Three traps: a device is matched by kind+unit+base
and never by its row index (the probe re-runs and a machine can gain or lose a
drive between boots); the automount still banks the current volume with
`OSAPI_FILE_HERE` and puts it back, so the rest of `drv_boot` finds the system
disk current; and **there is one save path, `hd_cfg_mark`, and it never touches
a disk** — the geometry editor on every `+`, Mount, Unmount and detach all
stage the blob into the kernel and the panel's close writes it, like every
other setting in the machine (SPEC.md §31.8). There were three, and the two
that wrote immediately are gone with §51.9's verb 2; **the testing consequence
is that a persistence run must close the panel before it quits**, mount
included, where a mount used to survive a hard quit.

**Everything the user sees is in `drivers/hdd/`**: the probe, the Control
Panel page, the disk tool that partitions and formats, and Mount. The kernel's half is
five API slots (0x0270..0x0290), a 6-row volume table and a branch. Two things
about the driver are worth knowing before touching it. Its **rung 1 (the IDE
task file) is gated on `CPU_286`**, and that is arithmetic: an 8088's
`in ax, dx` is two 8-bit bus cycles at the same port, so the drive's high byte
is lost — an 8088 with a hard disk has a controller with a ROM, and rung 0 is
that ROM, which is also the whole of MFM support. And its formatter computes
the FAT-size ceiling in **32 bits**, because `TmpVal1 + TmpVal2 - 1` wraps a
word for every cluster size there is and reads as "no layout fits".

**Both transports batch a run into one command** (SPEC.md §52.1), and the
bounds differ because they disagree about who walks the geometry: rung 1 caps
only at ATA-1's 255, because the DRIVE steps sector/head/cylinder itself, so
it is one command and then one DRQ handshake per sector; rung 0 stops at the
end of the TRACK (a CHS int 13h must not cross one) **and** at the 64KB DMA
page, because the kernel bounds only the runs it issues and this rung splits
them again. **The formatter picks its cluster size from a capacity table**
(SPEC.md §52.3) rather than smallest-legal — a 32MB partition was getting
512-byte clusters, a 254-sector FAT the nine-sector window covers 3% of, and
one FAT entry to walk per 512 bytes of every file. The consequence to know is
that two partitions on one disk can now have DIFFERENT cluster sizes, which is
what `fcp_clspan` (SPEC.md §22.5) exists to survive.

**Partitioning and formatting are ONE window and one button** (SPEC.md
§52.2). They were two windows — New/Delete/Write, then pick-a-slot-and-Format —
which made the user learn that a table entry is not a volume and that Write is
the commit. Now each of the four slots reports its own state (`FAT16` /
`Not Formatted` / `Unmountable`) and one Format button does both halves. Four
things about it are worth knowing. **A slot is not a 32MB region of the disk**
(§52.2.1): `hd_slot_extent` takes the extent out of the TABLE with
`mem_claim`'s bump scan, so a partition sits one cylinder past a foreign 50MB
one rather than at the next multiple of anything, a hole between two
partitions is usable, and an entry can never be laid inside somebody else's —
which is the failure that takes a whole 80MB partition with it. **A hole is
often an ALIGNMENT GAP**: this tool's floor is LBA = spt and a modern tool
1MB-aligns at LBA 2048, so a disk repartitioned elsewhere has 1,985 real
unowned sectors at the front, and formatting them gives a working 970KB FAT12
volume — which read `0M` until the size column learned KB, and looked like a
broken tool rather than a small disk. The scan walks every hole and
takes the LARGEST, because the slots are scarce: first-fit spent one of four
primaries on that 1MB gap while 30MB sat free in the middle of the disk. **A slot over
the ceiling or of a foreign type is `Unmountable` and its Format button stays
LIVE**, because reclaiming the first 32MB of it is the only useful thing left
to do there. **No ROW in that window is greyed** (§52.2.2), and the row that
briefly was is the §47 case worth reading: greying is a claim about a
*control*, the row is selectable and Format acts on it (rule 4's "looks
unavailable and works", arriving where rule 4 does not look because no
predicate refuses anything) — **and it did not show anyway**, because it was
greyed with a bare `CDGRAY`, which rounds to black on 1bpp with no `[gfx_dis]`
to dither it, so on CGA it was pixel-identical to the live row beneath it. The
window's **buttons** do grey, Delete on an empty slot, and they take the pen
through `OSAPI_GFX_PEN` so the frame *and* the label dither together — a
button is a control and a row is not, which is the whole distinction.
**`Not Formatted` means unpartitioned OR
partitioned-and-empty** on purpose: both mean Format makes a volume, and the
second is what an interrupted format leaves. And **the table entry is written
BEFORE the volume**, for the same reason the boot sector goes last inside the
format — every interruption has to land on a state the tool can re-offer.

**A driver's window is the kernel's to measure.** The tool window erases
its content through `OSAPI_WM_GEOM` and never the template's constants: the
content is `W_W-2` by `W_H-TITLE_H-1`, **TITLE_H is 18**, and `wm_fit` clamps
a template that does not fit the live screen (SPEC.md §39.7) — so a repaint
that open-codes a size draws through the border and into whatever is behind
it, and the gfx primitives clip to the SCREEN and will not stop it. The same
applies to strings: every one in that driver fits its box, because `font_str`
does not stop at a window edge either.

### Loadable drivers (SPEC.md §51, `kernel/driver.inc`)

**A driver is a package that is not an application.** Same 32-byte header,
same `org 0`, same paragraph-aligned heap claim, same three-byte dispatcher
at `PKG_DISP` — so `drv_call` is `wm_pkgcall` with the far pointer taken out
of a driver row instead of a window record, and a driver author writes near
procs with near `ret`s. Four differences, each load-bearing: it is a **.DRV
file** (the mount types only `*.O88` as an application, so it can never be
double-clicked into the loader); its **header version is 4** (so
`ld_check_hdr` refuses it too — two independent gates); it has **no instance
record** (its memory is `MEM_K_DRV`, counted under System); and **its bss
ships inside its image**, which is what lets `drv_load` make exactly ONE
claim, at the size the directory entry already reported, before a byte is
read.

`DRVV_ATTACH` must be all-or-nothing — the kernel frees the image the moment
a driver says no, so anything it hooked outlives it — and `DRVV_DETACH`
cannot fail. Detach happens BEFORE the free, always: freeing the claim under
a live interrupt vector points it at whatever claims that memory next.

**Publication is per CLASS** (SPEC.md 51.2.1), and that is a fix rather than a
generalisation: `[drv_owner]`, the far pointer and the copied table used to be
one of each, so on a machine with a sound card *and* a hard disk the second
attach silently disconnected the first - and "sound stops when the disk driver
is enabled" is a symptom nowhere near its cause. Class 1 is index 0, so not one
line of `snd.inc` changed. `drv_blk_call` is a second BODY rather than a class
argument, because the disk ABI spends every register the sound ABI does;
`drv_cp_call` *can* take the class in AL, because the Control Panel page ABI
leaves AX alone.

A driver publishes a **service table** the kernel copies into `.bss` at
attach (the `dsk_get_dir` staging idiom), so every later dispatch is a near
read plus one far call and `snd_tick` — inside IRQ0 — needs no segment
register to find out whether it has work. `DSV_TONE` is the interesting cell:
publishing it **moves the tone tier off the PC speaker**, which is what an
OPL2 wants (an FM note is two register writes and then no CPU) and a Sound
Blaster does not.

**`drv_svc_call` takes no register but DI, and that is a contract**: every
other general register is an argument to something in the sound ABI — AL the
verb, BX the FM frequency, CL the channel, DH the requesting instance, SI and
ES a staged buffer — so the dispatcher is a far pointer in memory
(`drv_fptr`/`drv_fseg`) rather than something passed in. It went through BX
once and quietly ate the frequency: every FM call came back refused while
*tones*, which pass AX, worked perfectly.

**The kernel reaches a callback through the package's own dispatcher**, so
**every proc — the entry included — is a near proc with a near `ret`**, and
there is no `push cs / call x` trick around a retf-ending helper because
there are no retf-ending helpers. A `retf` returns into the
loader's stack frame and hangs the machine at the first paint. `tests/fmtest`
is the FM gate: `make test-snd ADLIB=1
TESTAPPS=build/fmtest.img`, click twice, and the wav must show 880 Hz
dominant from a keyed 440 — which is only true if the CALLER'S patch bytes
reached the operator registers.

**Nothing here can stop the boot.** No disk, no file, no card and no memory
are all recorded in the row and reported afterwards — `drv_boot` runs before
the first paint so a loaded driver is live from frame one, and `drv_notice`
runs after it and opens the **Control Panel on its Drivers page**, which
already names every driver and says what its last attempt answered.

**No row is wanted by default, and that is the rule rather than a tuning**
(SPEC.md §51.3). Both `drv_tab` rows ship `DRVR_WANT` = 0, so **nothing is
loaded at boot that `SYSTEM.CFG` did not ask for** and nothing probes
hardware that was not asked about. The sound row shipped with a 1 for a
while, and a freshly built image carries no `SYSTEM.CFG` at all, so every
first boot read the whole 5.5KB driver off the floppy to be told there was no
card — measured at **27 sectors**, ~6.4 s at the pre-fix 238 ms and ~1.8 s at
today's 65 ms per
sector — and then `drv_notice` opened the Control Panel on a failure nobody
had requested. The whole boot is 5 transfers / 13 sectors now, against 24 /
40. Two consequences to hold on to: a fresh image has **no FM and no digital
sound** until the Drivers page is ticked once (the tone tier stays on the PC
speaker, which is where it lives with no `DSV_TONE` published, and the tick
loads on the spot), and the "no hardware found" report is still exactly
right when the settings file *did* ask — that path is untouched and gated on
the same `DRVR_WANT`.

**…but the first boot ASKS, and that is not the same as guessing (SPEC.md
§51.3.1).** What §51.3 refused is in its own paragraph: 5.5KB of driver read
off a floppy to be told there was no card. `drv_snd_sniff` is §34.2's OPL2
timer-flag dance and costs **~2 ms of port I/O** — a hundredth of one floppy
sector — so on a machine with no `SYSTEM.CFG` yet, the sound row's `DRVR_WANT`
starts at 1 if there is an FM chip at 388h and 0 if there is not. Four things
about it. **It is a DEFAULT and nothing enforces that** — `drv_cfg_unpack`
calls `drv_want_set`, which rewrites all three `WANT` bytes from the file's
bitmap, so a settings file always wins in both directions and there is no "did
the file exist" test anywhere to get wrong. **It is in the BOOT OVERLAY**
(§2.5), called from `kmain` and not from `drv_boot`, where it belongs by
subject and cannot live: `drv_boot`'s own mount is what writes over the
overlay, so by the time `drv_cfg_load` has answered the probe's code is FAT —
which is also why it **costs nothing at all against `KERN_BUDGET`** (measured:
`.text` +5 bytes for the far call, into 354 bytes of image-rung slack;
`.ovl` +113, and the overlay is free). `clk_init`'s four-rung RTC ladder is
the precedent, same shape and same home. **One probe covers both cards, as a
fact about the hardware**: every Sound Blaster carries an OPL2 at 388h, which
is why the driver's own attach probes FM first (§34.2). And **the DSP reset
scan is a knob, `make SNDSNIFF=sb`, gated on the FM probe having missed**,
because it costs the thing §51.3 refuses to spend on the hard-disk driver —
*writing* to six unknown port ranges on every boot. Two cases want it: a card
whose FM half is jumpered off, and QEMU's `-device sb16`, whose OPL does **not**
answer the timer probe where a real one does. The field
machine has no sound card at all (docs/FIELD-MACHINES.md), so it is the one
that pays a probe and gains nothing — which is what decided the default.
**MartyPC is the instrument and had to be**: the probe reads a timer that has
to overflow in real guest time, so an emulator running the guest at host speed
through a status stub can neither confirm nor refute it — QEMU says the probe
works with `-device adlib` and says an `-device sb16` box is cardless, and only
the first is a fact about hardware. On a cycle-accurate 5150 the FM probe hits
on `os8088_5150_sb`, misses on `_cga` and on **`os8088_5150_sbonly`** — a
machine added for this, a DSP with nothing at 388h, which no real card is but
which is exactly the case the knob exists for — and `SNDSNIFF=sb` finds that
one against a real DSP 2.01. End to end: the Sound page comes up on **Sound
Blaster** with nothing ticked, and its Test tone is **660.0 Hz in the OPL2
capture** while the speaker's holds only the 5150's POST beep.

**`bb_set` is the LAST thing `drv_boot` does**, after the load loop, and that
is SPEC.md §15.3's requirement rather than tidiness: it seeds the back buffer
from VRAM, and until the first `wm_paint_all` VRAM holds the loading screen
whose bar is still being ticked. A buffer armed ahead of the reads swallows
every notch after it. The heap does not care — data claims grow up and a
driver's region down.

**The system disk is a FAT12 volume, and the kernel is a FILE on it** (SPEC.md
§19.3) — an ordinary one in the data area, allocated first and contiguously so
that 512 bytes of boot sector can read it as a flat run without walking a
cluster chain. Drive A: mounts, browses and **writes**, in os8088 and in DOS
alike. It used to live in the reserved area with `BPB_RsvdSecCnt` covering it,
which is legal and which **DOS does not honour on a floppy**: DOS builds a
floppy's BPB from its own table of standard formats, so it read the kernel as
FAT and root directory and reported garbled entries and 52,224 bytes free.
Both geometries are now byte-exact standard formats. The boot sector derives
the kernel's LBA from the four BPB fields it needs rather than having it
injected — and **the one trap it sprang is that `[lba]` stopped being a count
of sectors done**: it starts at 33 (or 12 on 360KB), both past
`SPL_RESIDENT`, so the splash tick fired before a byte of the splash had
landed and the machine hung black inside `KERNEL_SEG`. Sectors done is counted
on its own now.

**The system files are hidden** (SPEC.md §19.6): `KERNEL.SYS` and every
`*.DRV` are read-only + hidden + system, `SYSTEM.CFG` is hidden + system (not
read-only — the kernel rewrites it), and `TASKMGR.O88` is visible + read-only
because it is an application the chip menu loads by name. `disk_mount`'s
species filter already dropped hidden and system entries, so the Disk window,
the icon grid, the file dialog and DOS all follow for free. Two things did
not: **`drv_find` had to stop using `dsk_find_name`** (that walks the filtered
display listing, so every driver read as "Not on the system disk" the moment
it became hidden) and uses `dskw_stat`, which walks the directory sectors;
and the kernel needs `dskw_write_sys` to rewrite `SYSTEM.CFG`, because
`DSKW_PROT` treats hidden|system as untouchable and the *first* save would
otherwise lock out every save after it. That entry point is kernel-only and
**must never get an API slot** — the point of the section is that a package
cannot make a file the user can neither see nor delete.

`SYSTEM.CFG` in the root is 32 bytes carrying the *whole* Control Panel (the
driver list, the sound route, the clock options, the scheduler mode, the back
buffer), written by `cp_flush_close` when the panel closes and restored by
`drv_boot`. A
missing or malformed file means the defaults, never an error.

**It is written to the SYSTEM disk, not to whatever is in A:** (SPEC.md
§51.5.1), and that took a fix: `drv_cfg_save` mounted A: and wrote, on the
unexamined assumption that the disk you booted from is still in the drive. A
single-floppy machine swaps to the apps disk to launch anything, so a Control
Panel change afterwards wrote the settings *there* — the setting never reached
the system disk, AND the user's data disk gained a hidden+system file that
§19.6's own protection makes impossible to list or delete from inside os8088.
`KERNEL.SYS` in the root is now the marker, checked with **`dskw_stat` and not
`drv_find`** — the latter answers a size and refuses one over 64KB, and the
kernel is 63,944 bytes today, so it would have started calling the system disk
foreign the moment the kernel grew. The refusal keeps `[cp_wdirty]`, so
putting the right disk back and closing again saves.

**…and it puts the user's volume back** (SPEC.md §51.5.2), which is the other
half and took a second fix. §51.5.1 settled *which disk gets written*; going
to A: is a **navigation**, and nothing undid it — so `drv_cfg_save` on the
panel's close, and `drv_load` on a Drivers-row tick, both returned with A:
current. A user working on B: was silently moved to A: by a Control Panel
click, and **nothing said so**: the file API resolves every name in the
current directory (§19.2), so the next Standard File dialog just opened on the
wrong disk. It was found by accident — a scripted test ticked the sound
driver, closed the panel, then drove a package's Open dialog by row position;
the dialog was on A:, the row hit nothing, and the app said `Disk error`,
which points nowhere near the Control Panel. `drv_vol_bank`/`drv_vol_back` are
`ui_tm_open`'s bank-and-restore (§28.3) made a routine, and three things hold
them up: **both preserve the FLAGS**, because each caller's result is in CF
and AX and a failed remount must not read as a failed load or a failed save
(so a failed restore is swallowed — root, write gate shut, `ui_tm_back`'s
answer); **the compare is what makes it free**, since a remount is a floppy's
worth of sectors and `drv_boot` calls `drv_load` per wanted driver, so banked
== current means nothing to undo; and **`drv_boot` needs no exception**, which
falls out of that compare rather than being arranged — it mounts A: itself
before the loop, so every load inside it banks A: and finds A:. **What it costs
is one floppy remount, and only when the volume actually moved** — measured
with §18.94's counters rather than argued: ticking a driver on B: was 1 mount
/ 28 sectors / 12 `int 13h` calls before the fix and is 2 / 43 / 17 after, so
the restore itself is 1 / 15 / 5; on A: it stays 1 / 26 / 10; and a boot with
a driver wanted is 2 mounts where an unconditional restore would be 3. **That
cost is the fix rather than an overhead on it** — putting the volume back *is*
a remount — and it is
affordable because of what it is attached to: both callers already touch the
floppy, and a `SYSTEM.CFG` write alone is 2+ seconds of frozen UI on the floor
machine (§31.8). **Those figures predate the quiet mounts and are kept as the
record of what the restore cost when it was introduced**: going to A: and
coming back are `dsk_chdir_q` now (SPEC.md §18.9), because both are file
operations by NAME — `drv_find` is `dskw_stat`, `drv_cfg_*` are
`dskw_read`/`dskw_write_sys` — and none of them looks at a listing, so the
scan, the sort and one icon-harvest read per file in A:'s root were bought
for nothing. Navigation is still a remount by design (§19.2); this was never
navigation. Measured on the boot path: a plain boot is **14 sectors → 11**.
`ui_tm_open` is the one caller that genuinely needs a listing (it hands the
loader a directory index) and it mounts A: itself rather than coming through
`drv_mounted`, so it is untouched.

Two traps. **`build/os8088.img` is now writable and the OS writes to it** —
any test that touches a Control Panel setting is remembered by that image and
survives the next boot, exactly like `build/apps.img`; `rm -f build/os8088.img
build/os8088-720.img build/os8088-360.img && make` is the reset when a run's
starting state matters. And **`make test ADLIB=1`**
(or `SB16=1`) is the only way to exercise the driver at all: without a card
QEMU's probe correctly finds nothing, which is the right answer and not the
one you want to be testing against.

### The system clipboard (SPEC.md §55, `kernel/clip.inc`)

**One TEXT buffer for the whole machine**, three routines and three words.
Copy a paragraph in Note Pad, close Note Pad, paste it into another program:
that is the feature, and every decision falls out of it. Slots 0x0320
(`clip_put`, ES:SI + CX, CX = 0 empties it), 0x0328 (`clip_get`, ES:DI + CX,
answering the whole length in AX and the bytes copied in CX) and 0x0330
(`clip_size`, what a paste asks BEFORE it makes room) - drafted at
0x0310..0x0320 and moved when it met `gfx_pen_cf` and `gfx_lstepv`, which is
SPEC.md 20.8 rule 4 working as written: neither had shipped, so neither was
frozen, and the one that had not yet reached the integration branch moved.

Four things are load-bearing. **It is text only, and that is a decision** —
a typed clipboard needs a registry every app agrees on, a preference order and
a rule for "neither side understands the other", and the answer to the last is
almost always "nothing is pasted". Paint's picture clipboard stays Paint's own
(§42). **The buffer is a heap claim sized to what is in it** (§50.3), so a
machine that never copies pays nothing and emptying gives every byte back; a
put whose rounded KB differs **frees then claims** rather than regrowing,
because every byte is about to be overwritten and preserving them would need
both blocks free at once — the price, stated in the contract, is that a
refused claim leaves the clipboard EMPTY rather than stale. **The owner is
`MEM_K_CLIP`, a kernel tag**, so `mem_free_rec` does not take it when the app
that copied closes (that IS the feature) and `mem_sum_kb` already counts it
under System with no change in the Task Manager. And **`clip_put` takes
`ES:SI` rather than being an X stub**: the obvious shape would have handed the
kernel the package's own segment, and the two apps most likely to call it keep
their document in a heap claim of its own (§27.6, §46.9), not in their image.

### The claim heap (SPEC.md §50, `kernel/memory.inc`)

Everything above the kernel is handed out on demand. `mem_claim` takes KB and an owner word and answers a segment; `mem_free`/`mem_free_owner`/`mem_free_rec` give it back, and `mem_avail` reports the largest free run and the total. Five things about it are load-bearing:

- **A package's owner word is the segment it runs in**, put in ES by the slot's X stub — so there is nothing to pass and nothing to forge, and `OSAPI_MEM_CLAIM` works from the entry proc where the app has no window yet. That is where an app sizes itself.
- **`mem_claim_dma` puts the 64KB page rule inside the scan** (SPEC.md §50.3). An ISA bus master cannot carry into the page port, so a DMA buffer must not straddle a 64KB *physical* boundary — and the sound driver used to discover that by claiming, testing the address it got, and claiming again while **holding** the block that failed, so finding 32KB could hold 128KB and refuse on a machine that had the room all along. `CX` is the KB of the block's **head** that the chip sees, not the whole block (the SB's staging pool is copied with `rep movsb` and may straddle freely), and a candidate that would straddle bumps to the next page floor — the same monotonic shape as the bump past an overlapping claim, so termination is unchanged. Two things follow: a head over 64KB is refused up front, and **`mem_regrow` does not preserve the constraint** — no record carries it, so a claim that moves can land straddling.
- **Teardown frees claims.** `mem_free_rec` runs at all three instance teardown sites, which is why `OSAPI_MEM_CLAIM` needs no close hook.
- **The kernel is a client too**, with its own owner tags: the menu save-under (`MEM_K_SAVE`, claimed by `menu_drop` for exactly as long as a menu is on screen and released *before* the chosen item runs) and the back buffer (`MEM_K_BB`). `bb_set` claims 150KB when double buffering is armed and frees it when it is switched off, and the Control Panel's Display row **greys out with "Not Enough Ram"** when `bb_canfit` says the heap cannot fund it right now. That is live state — open Paint and the row greys, close it and it comes back.
- **Refusal is a normal path, not a panic.** Every claim in the tree has a fallback: the menu save-under repaints the menu's own rect instead of restoring it, a Disk window with no listing cache reads the global mount snapshot, Paint gives up features tier by tier and finally puts up a notice window.

### Three geometries of everything

Every image is built three times: 1.44MB (18 spt, for QEMU), **720KB** (9 spt, 80 cylinders — a 3.5" DD drive, and what every USB floppy drive and Gotek reads) and 360KB (9 spt, 40 cylinders, for 86Box / a real XT). If you change the boot path or the FAT driver / disk layout, check all three. **720KB and 360KB share `boot360.bin`, and that is not a shortcut**: the boot sector's whole knowledge of a geometry is `-DSPT`/`-DHEADS`, both 9/2 here, and it derives the cylinder from the LBA rather than holding a count of them — what actually differs is the BPB (media byte F9h vs FDh, `TotSec16` 1440 vs 720, `FATSz16` 3 vs 2), which `tools/os88disk.py` writes over the first 62 bytes. A `boot720.bin` would be a byte-identical second artifact that can only ever say what `boot360.bin` already says. `make run-720` boots the pair in QEMU, which picks a floppy's geometry from the image SIZE — 737,280 bytes is a standard one, so nothing else was needed.
