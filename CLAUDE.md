# CLAUDE.md

Guidance for Claude Code (claude.ai/code) working in this repository.

## What this is

os8088: a Macintosh System 1-style GUI OS for the Intel 8086, written entirely
in real-mode NASM assembly, booted from floppy. Pre-emptive multitasking,
overlapping windows, serial mouse, a bottom dock, and loadable software
packages that run as closable, multi-instance apps. One binary drives VGA,
Hercules or CGA, picked at boot.

This file is a map. Each document below is the authority on its subject and
nothing here duplicates it — a second copy is a copy that goes stale.

| document | read it before |
|---|---|
| **[SPEC.md](SPEC.md)** — the binding contract | touching any interface. Every symbol name, register contract, constant and layout is pinned there. **Update it *before* the change, not after.** A bare `§` anywhere in this repo means SPEC.md; §4 is the module-ownership table |
| **[PERFORMANCE.md](PERFORMANCE.md)** | anything that draws, lays out or loops — but the load-bearing ~200 lines are condensed below, and over half the file is a field-measurement log. Open it for the reasons listed at the end of that section, not by default |
| **[docs/TESTING.md](docs/TESTING.md)** | concluding something is untestable — it is the matrix of what each emulator can and cannot do, with a recipe per capability |
| **[docs/KERNEL-MEMORY.md](docs/KERNEL-MEMORY.md)** | spending any memory |
| **[docs/HERCULES-TESTING.md](docs/HERCULES-TESTING.md)** | testing on Hercules — it *is* automatable, and all three ways of getting it wrong give you a black image rather than an error |
| **[docs/C-TOOLCHAIN.md](docs/C-TOOLCHAIN.md)** | writing or building a package in C (§73) — how to install the compiler, the four C rules and what each refusal means, and what the language does not have here |
| **[docs/NET-STACK-PLAN.md](docs/NET-STACK-PLAN.md)** | anything on the wire (§72) — the stack's stages, what each layer refuses, and why TLS is not on this machine |
| **[docs/BROWSER-PLAN.md](docs/BROWSER-PLAN.md)** | the browser (§71) — the renderer's steps, and `tools/htmsim.py` is its reference implementation |
| **[docs/PROXY-PLAN.md](docs/PROXY-PLAN.md)** | the host-side proxy — it exists because an RSA-2048 private operation is *minutes* on a 4.77 MHz 8088, so TLS terminates off the machine |

## Commands

Needs `nasm`, `qemu-system-i386`, `python3`; `tools/setup-macos.sh` installs
them on a Mac. No linker — everything is `nasm -f bin` flat binaries,
deliberately, to keep Apple's Mach-O-only toolchain out of it.

```
make          # build every floppy image into build/ (also runs tools/checkdocs.py)
make run      # boot in QEMU with an emulated serial mouse. RUNAPPS=<img>
              # swaps the B: floppy, so a disk built on demand can be LOOKED
              # at (`make bench && make run RUNAPPS=build/bench.img`)
make test     # boot headless, QMP socket at build/qmp.sock — this is how you drive it
make test-snd # ...plus PC speaker capture to build/snd.wav (verify: tools/sndcheck.py)
make debug    # boot halted, waiting for gdb on :1234
make bench    # build the tests/ apps — ON DEMAND ONLY; nothing under tests/ ships
make zcheck   # play every Z-machine story to a script and diff it against
              # dfrotz (§61.13). `make zh` builds the harness interpreter;
              # `tools/zharness.py <story> --repl` types at one by hand. This
              # is how a Frotz change is checked — a story is the only thing
              # that exercises an interpreter, and it is minutes by hand
make zgfx     # ...and what the reader can SEE (§61.14): every row the
              # interpreter claims against the pixels under it, the same
              # across a repaint, and each story's opening screen against the
              # real Frotz's. `make zpic` builds the v6 picture fixture it
              # ends with; `make zscreens` re-takes the golden screens, and
              # is the only part that needs `frotz` and `pyte` installed.
              # zcheck cannot see a graphics defect — a story that draws a
              # quote box and loses it prints the same characters as one that
              # keeps it
make cword      # the C toolchain (§73). `tools/setup-cc.sh` fetches and builds
make cworddisk  #   SmallerC into build/cc/ first — nothing in `all` depends on
make cc-smoke   #   it, and a tree without it builds every shipping floppy and
make chello     #   prints one note. cc-smoke/chello are the two examples and
make covl       #   covl is the OVERLAY gate (§73.14); cword is the
                #   application — Word 1.1a again, in C, in two segments
                #   (§73.12). `make clean` SPARES build/cc
                #   (clean-cc removes it) — it is a pinned upstream instrument
make cpmsw      # the CP/M games and applications the RUNCPM floppies carry
                #   beside RunCPM's master disk (§74.6) - LADDER, CATCHUM,
                #   Nemesis, GAINA, WordStar, Turbo Pascal - fetched by
                #   tools/getcpmsw.py from the public RunCPM software
                #   collection, every file pinned, nothing committed;
                #   CPMSW='A/5:FILE' adds your own. The 1.44MB disk carries
                #   the lot, the 720KB one the arcade area, the 360KB one
                #   none (GAMES.TXT on each says which and why)
make runcpm     # RUNCPM (§74), the second C application: RunCPM 6.9 as a
make runcpmdisk #   windowed CP/M 2.2 emulator — the host checks, then the
                #   package; then the three floppies, from RunCPM's CCP and
                #   master disk that `tools/getruncpm.py` fetches at a pinned
                #   commit (`make runcpm-src`; never committed). `make rczex`
                #   / `make rcz80test` are the Z80 core's ZEXDOC gates (in the
                #   OS / in raw QEMU), `make rcmemtest` the movers',
                #   `make rcbandbench` the row composer's bench
                #   (PERFORMANCE.md Set 65)
make ethertest  # THE ETHERNET GATE'S DISK (§72.9): a SYSTEM.CFG that already
                #   asks for ETHER.DRV, so the card is up and DHCP has run
                #   before the first paint and the test reads state instead of
                #   clicking. `make ethertest && make browsertest && python3
                #   tests/ethernet.py`. It boots QEMU and must: MartyPC has no
                #   NIC of any kind, so `ETHER.DRV` cannot be hosted on it at
                #   all, and tests/ethernet.py asserts behaviour and never
                #   speed because the machine under it is not an 8088
make browsertest # ...and the browser's page disk, for tests/br*.py
make allapps  # build/apps-all.img (§19.10): ONE 1.44MB floppy with every app
              #   on it, Frotz, both Words and RunCPM (with its drive A)
              #   included, for a release page. Needs the C toolchain and
              #   the RunCPM fetch, so it is on demand like cworddisk —
              #   it is the only target outside §73/§74 that does
make clean
```

`make test` knobs, each documented at its definition in the Makefile:

| knob | effect |
|---|---|
| `VIDEO=cga\|herc` | force an adapter instead of probing |
| `RTC=at\|ns\|rp\|bios\|none` | force one rung of the clock ladder |
| `ADLIB=1` / `SB16=1` | give the sound driver a card to attach to |
| `HDD=<MB>` | give the hard-disk driver a disk |
| `TESTAPPS=build/<x>.img` | swap the B: floppy for a scratch image |

All are stamp-tracked, so changing one rebuilds the kernel. Without that, make
sees an up-to-date `kernel.bin`, boots the previous configuration, and it reads
exactly like the feature being broken.

86Box targets for period hardware, one per `vm/` directory: `xt`, `xt-640`,
`xt-cga`, `xt-hercules`, `xt-multimon`, `xt-sound`, `xt-sound-1.44`, `286`,
`286-sound`,
`386sx`, `386`, `386-sound`, `486`, `pentium`, `xt-z`, `386-z`, `xt-word`,
`386-word`, `386-c-word`, `xt-runcpm`, `286-runcpm`, `386-runcpm`; plus
`marty` (MartyPC). `xt-multimon` is the
**two-card** XT — a CGA and a Hercules, a monitor window each — and the only
86Box machine that can show §39.12–§39.19's extended desktop; it boots Single,
and Control Panel → Display → Desktop is what extends it (§39.19.1). `xt-z`
and `386-z` are the Frotz machines (§61.9), `xt-word`/`386-word` are the Word
machines (§68.5), `386-c-word` is the C word processor's (§73.12) and
`xt-runcpm`/`286-runcpm`/`386-runcpm` the CP/M emulator's, one per floppy
geometry because the three disks carry different software and the machines
run at different speeds — which for a CP/M game IS the play speed (§74.5,
§74.6) — the eight that put a dedicated
floppy in B: instead of the apps disk. `make zdisk` builds the story disk
(`tools/getstories.py` fetches the stories, which are never committed), `make
worddisk` the Word disk, `make cworddisk` the CWORD disk — which carries
`WELCOME.RTF`, the same welcome document the Word disk carries as a `.DOC`,
adapted to what cword's RTF can actually say (§73.12.3) — and `make
runcpmdisk` the RUNCPM disks (`tools/getruncpm.py` fetches RunCPM's CCP and
master disk at a pinned commit and `tools/getcpmsw.py` the CP/M games and
applications that ride beside it, §74.6 — never committed, either of them;
`make rczex` and `make rcz80test` are the Z80 core's ZEXDOC gates, in the OS
and in raw QEMU).
`make allapps` collapses all of them onto one 1.44MB floppy (§19.10).

**`RESET=` clears a machine's non-volatile state on the way in**, and it
reaches every one of those targets because they all launch through the same
`$(BOX)`: `RESET=1` (or `cmos`) for the CMOS, `RESET=flash`, `RESET=both`.
86Box's own `-X` does it, and `-X` clears and then goes on to boot, which is
why this is a knob and not a target. On an AT-class machine a cleared CMOS
means the next boot stops in BIOS setup — pick EXIT FOR BOOT once and
`vm/<machine>/nvr/` is written again. It does **not** reach an *orphaned*
`.nvr`: that file is named for the `machine =` key, so editing that key in a
`86box.cfg` strands the old one; `rm -rf vm/<name>/nvr` is the bigger hammer.
Every `nvr/` is gitignored, so neither can reach the repo.

**Nothing in `build/` is tracked — never commit a binary.** The toolchain is
deterministic on purpose (`tools/os88disk.py` pins the volume serial and every
FAT timestamp), so a released image can be rebuilt byte-for-byte. Releases are
cut by `.claude/skills/release-os8088`; a port of an existing program to a
C package, the way `apps/cword` was made, is driven by
`.claude/skills/port-to-os8088` (`/port-to-os8088`), whose `LESSONS.md` is
what that port learned; and an incoming pull request **from a contributor's
fork** — fetch it, merge `main` into it, review it, fix it, push the fixes
back to their branch, comment — is `.claude/skills/review-fork-pr`
(`/review-fork-pr <PR#>`), whose `LESSONS.md` is what seven of those reviews
learned. `docs/UPSTREAM.md` is the same cycle seen from the fork's side and
binds both.

## Hard rules (§1 — these break silently if violated)

- **8086 only.** `cpu 8086` plus `-w+error`: no `pusha`, no `push imm`, no
  `shl reg, imm` other than 1, no `movzx`, no 32-bit registers.
- **Near model.** CS = DS = `KERNEL_SEG` for kernel code and every task;
  **SS = `LOW_SEG`**, because task stacks live outside the kernel's segment.
  Every inter-module kernel call is near — there is no far code (§33).
  **SS ≠ DS, so `[bp+disp]` addresses SS**: kernel code holding a pointer in BP
  needs `[ds:bp+…]`.
- **Register discipline.** Public routines preserve every register but their
  documented outputs. ISRs push DS/ES, load DS = KERNEL_SEG, `cld` before
  string ops. Critical sections are `pushf`/`cli` … `popf`, never `cli`/`sti`.
- **Section discipline.** A module switches sections with a bare
  `section <name>` and **must switch back to `.text` before the file ends**, or
  the next include lands in the wrong one.
- **Label hygiene.** One flat namespace: every module-internal label carries
  its module prefix (`vga_`, `wm_`, `menu_`, `fm_`, `dsk_`, …) or is a NASM
  local label.
- **Three adapters, one binary (§39).** `SCREEN_W`/`SCREEN_H`/`ROW_BYTES` are
  VGA *reference* values, not the truth — the live screen is
  `[vid_w]`/`[vid_h]`/`[vid_stride]`. Anything that clips, centres or anchors
  to a screen edge must read those, or it is wrong on two adapters of three.
  **Look at a drawing or greying change on a 1bpp adapter before calling it
  done** (§39.4, §47): grey rounds to black there, so a disabled glyph is a
  checkerboard and a ring is dotted.
- **Every disk-visible base is 512-byte aligned.** int 13h answers a transfer
  straddling a 64KB physical boundary with error 09h, and only starting
  512-aligned prevents it. The symptom is "Disk error" on a large save.
- **Memory is two guards, not one** (docs/KERNEL-MEMORY.md): `KERN_BUDGET` is
  the kernel's whole RAM footprint, `KERN_CODE_MAX` is `.text` + `.bss` inside
  one 64KB window because offsets are 16 bits. They are relieved by different
  mechanisms. **Raising either is a decision to take with whoever asked for the
  feature, not a build fix.**
- **The package boundary is solved once, not per call site** (§20): calling out
  is a far call through an API cell that switches DS; calling in goes through
  the three-byte dispatcher in the package header, so every callback is a near
  proc with a near `ret` and a package author never writes `retf`; and
  **ES = KERNEL_SEG on entry to every callback**, so it is `[es:bx+W_W]`, never
  `[bx+W_W]` — without the override a package reads its own image at that
  offset, which assembles cleanly and runs wrong.
- **A package reaches the network through a DRIVER, not through the kernel**
  (§72): `ETHER.DRV` publishes the socket surface as `OSAPI_DRV_CALL` verbs, so
  the kernel learns nothing about TCP and a machine with no card refuses at the
  one fence. `apps/os88sock.inc` is that ABI written once; `apps/os88line.inc`
  is the line discipline Telnet and the browser share.
- **A C package that does not fit gets a second segment, not a bigger one**
  (§73.14): a function named `ovl_*` has its CODE emitted into a module that
  ships beside the package (`CWORD.OVL`) and is far-called both ways, while
  every global, literal and bss byte it names stays resident and DS-relative. Split by
  FREQUENCY — a keystroke's path stays in, a menu command's can go out — and
  never take the address of an overlay function.
- **A C package obeys four extra rules, and `tools/cc8086.py` fails the build
  on each** (§73, docs/C-TOOLCHAIN.md): **never take the address of an
  automatic** — SS ≠ DS, so `&local` is a stack offset dereferenced through the
  package segment, and every addressable object must be `static`; **no
  `movs`/`stos`/`scas`/`cmps`** — ES is the kernel's, so no struct assignment,
  no struct by value, no struct return; **no `long`, `float`, `double`,
  bit-field or anonymous union**; and **frames stay under 96 bytes**. The first
  two are one defect with two symptoms — a struct passed by value trips both —
  and both are silent without the gate.

## Performance (PERFORMANCE.md, condensed)

The target is an **IBM PC/XT — 8088 at 4.77 MHz** — and you are testing ~1000×
faster. **QEMU is exact about how much work the guest does and useless about
how long it takes.** Estimate with these, measured on a real 5150:

| | cost |
|---|---|
| any `gfx_*` drawing call, whatever it draws | **756 us** fixed |
| one 8×8 glyph cell | **~900 us** |
| one 78-cell row of text | **~71 ms** |
| an `OSAPI_*` far call / a near `call`+`ret` | 46.7 us / 11 us |
| `OSAPI_TASK_YIELD`, a full task switch | 693 us |
| one `int 13h` call, near enough whatever it moves | **~400 ms** (1–2 disk revolutions) — cost disk work in **calls**, not sectors |
| system tick | 18.2 Hz |

> **A redraw is priced by how many primitive calls it makes, not by how many
> pixels it covers.** Almost everything else follows from that sentence.

The rules that fall out:

1. **Nothing repaints more of the screen than it changed.** This is the
   architecture, not an optimisation — §11.3's clip region, §11.90/§11.91's
   damage rects, §11.92's title strip, §28.2's chunks. Part 5 of
   PERFORMANCE.md is the standing budget: **a change that reintroduces a full
   repaint is a regression against a documented number, not a neutral
   refactor.** Check yourself against that table.
2. **Nothing writes a pixel twice.** The erase-then-letter pair is the
   canonical violation and `font_run` (§6.1) is the answer: one decision per
   cell, so the line is never momentarily blank.
3. **Size every range from the slowest machine it will run on.** A constant
   sized while looking at QEMU encodes the wrong range and fails structurally,
   not proportionally: 16-bit counters lap into small plausible numbers. Fold
   into 32 bits per iteration.
4. **Measure before redesigning; a counter is not a timer.** Put a counter in a
   primitive (`font_char`, `gfx_fill`, `wm_paint_all`) — it costs one rebuild —
   then multiply by the table above and say that you did.
5. **Keeping the shape of an optimisation is not keeping the optimisation.**
   `gfx_blit4`'s first version emitted exactly the designed number of calls and
   decoded every pixel by hand inside them: nine times slower, and QEMU priced
   it identically. When you rewrite something whose *reason* is speed, verify
   the reason survived, not the structure.
6. **Refusal is a normal path.** Do not ship a feature that silently costs
   seconds on the target; ship one that greys itself with the reason (§47 —
   grey a fact, never a guess).
7. **Degrade by tier.** `OSAPI_CPU_INFO` answers `CPU_8086` for the target
   machine, which is a fact the code can test rather than a guess about speed.

Three defects are **invisible in an emulator** and cost this project bug after
bug: a **visible redraw** (seconds on real hardware), a **double-draw flash**
(anything drawn twice), and **input overrun**. None showed in a screendump;
every one was found on hardware or by counting.

That is the whole of it that applies to every change. Open PERFORMANCE.md
itself for one of four reasons — it is 6,553 lines and over half is a log of
field measurements:

- **Part 5** before touching a redraw path — the standing budget names the
  operation, what it used to cost, what it costs now and the § that owns it.
  If your path is in that table, that row is the bar.
- **Part 2** when you need a number you are going to quote. It carries the
  access-shape distinctions this summary flattens (a sector inside a coalesced
  run vs. an isolated seek; the 8088's `max(clocks, 4.34 × instruction bytes)`
  fetch floor, which is why a shorter encoding can beat a cheaper instruction).
- **Part 7** when checking a change, and **Parts 3.1/3.2** when measuring
  flicker or smoothness — those are the harness manuals.
- **Part 9** only to find where a number came from, or when taking a new field
  set. Never as briefing.

## Testing

No unit tests. Testing = boot `make test`, then drive it over QMP.

```
python3 tools/mouse.py build/qmp.sock click 180 150        # absolute click
python3 tools/mouse.py build/qmp.sock to X Y / down / up   # menus: press, drag, release
python3 tools/mouse.py --screen 640x200 build/qmp.sock ... # MUST match the adapter (§39)
python3 tools/qmp.py build/qmp.sock 'sendkey h'
python3 tools/shot.py build/qmp.sock out.png [--crop X,Y,W,H] [--zoom N]
python3 tools/qmp.py build/qmp.sock 'quit'
```

Two traps not written down elsewhere:

- **A previous session's QEMU may still be running.** `make test` then fails
  with `cannot create PID file`, but the stale instance keeps answering on
  `build/qmp.sock` — every screendump succeeds and shows the OLD kernel, which
  reads exactly like a change that did nothing. If the error scrolls past,
  `ps aux | grep qemu-system` and compare its start time against
  `build/kernel.bin`'s mtime.
- **A small change is easy to misread as "nothing happened"** in a full-screen
  dump. Crop and zoom (`shot.py --crop --zoom`) before concluding a click was
  lost.

Mouse pacing, double-click timing, the writable scratch images and when to
reset them, 86Box's config rewriting, and what each emulator can show are all
in docs/TESTING.md, per capability.

## Layout

- `boot/boot.asm` — 512-byte boot sector; geometry and kernel size injected by
  the Makefile. It relocates itself, because the kernel lands where it runs.
- `kernel/kernel.asm` — constants, the memory ladder and its guards, boot
  sequence, the API jump table, `%include`s of every module, size assertions.
  Which `.inc` owns what is the table in §4. `video.inc`, `keyboard.inc`,
  `string.inc` and `gfx.inc` are dead — still in the tree, no longer included.
- `apps/` — loadable packages; **everything here ships**. `os88api.inc` is the
  SDK, and each package's design notes are its SPEC.md section. `apps/cc/` is
  the **C** SDK (§73): `os88.h`, the runtime `crt0.asm`/`os88thunk.asm`, the
  build rules, and `ccsmoke` as the worked example.
- `drivers/` — loadable drivers (§51): same format, but `.DRV`, header version
  4, no instance record, bss shipped inside the image. `drivers/ether/` is the NIC
  and the TCP/IP stack (§72) — the largest of them, and QEMU-only to test.
- `tests/` — every package that is **not** shipped software: capability gates
  (pass/fail) and benchmarks (how fast). Built only by their own targets.
- `apps/browser/`, `apps/telnet/` — the network clients (§71, §70), over
  `os88sock.inc`. `tools/os88proxy.py` is the host-side other end and
  `os88proxygui.py` its desk-side front.
- `tools/` — host-side Python: `os88pkg.py` (validates/stamps `.bin` → `.o88`),
  `os88disk.py` (builds FAT12 images; `--verify` is a structural fsck),
  `checkdocs.py` (the doc gate every `make` runs), `qmp.py`/`mouse.py`/`shot.py`
  (test drivers), `setup-cc.sh` + `cc8086.py` (the C toolchain's fetch and its
  gate, §73).
- `docs/` — the maintained accounts. `*-PLAN.md` are design records for work
  that has landed; `FIELD-NOTES.md`/`FIELD-MACHINES.md` are what real hardware
  said.

## Package pipeline

```
apps/mines/mines.asm --nasm--> build/mines.bin (org 0)
                    --os88pkg.py--> build/mines.o88   (validated, not relocated)
build/*.o88        --os88disk.py--> build/apps*.img   (FAT12 floppy, drive B:)
```

Packages are assembled at org 0 and loaded into a paragraph-aligned heap claim.
There is **no relocation of any kind**, so `os88pkg.py` is a validator rather
than a generator. Both disks are standard FAT12 volumes (§19) that any host OS
mounts — and every byte read off one is still treated as hostile.

**Every image is built in three geometries** — 1.44MB and 720KB (QEMU), 360KB
(86Box / a real XT). Changing the boot path, the FAT driver or the disk layout
means checking all three.

**Seven images, not six.** The system and apps disks in three geometries each,
plus `build/media360.img` — `BEVERLY.MOD` is 114 of a 360KB disk's 354 clusters
and is data rather than software, so at that geometry alone it rides a disk of
its own (§24.4). The **core packages** ship on the system disk too, a second
copy and never a move (§24.3), and an application's own state goes in
`SYSTEM/APPDATA/` rather than beside the user's documents (§19.9).
