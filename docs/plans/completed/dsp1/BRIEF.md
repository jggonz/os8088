# Driver size pass 1: the brief every agent reads first

Branch `end-of-week`, cut from `elendilon` at `3b16b60b` on 2026-11-15.
**This is the first size pass over `drivers/`.** Twenty-one kernel size
passes have covered the kernel and its on-demand modules, and two apps size
passes have covered the packages. Apps size pass 2 left the drivers out on
purpose (docs/plans/HANDOFF-APPS-SIZE-P2.md section 2: *"the next-largest
lever after the packages and ... a separate pass"*). This is that pass.

Each agent works in its own git worktree, created by the coordinator at
`/home/user/wt/<name>` on a branch `dsp1-<name>` cut from the commit that
adds this file. Each agent writes its record to `docs/plans/completed/dsp1/<name>.md`.
The coordinator merges the branches into `end-of-week`. **Agents do not
push.**

## 0. Why the drivers, and what a byte buys here

A driver byte is paid twice:

* **Disk.** The 360KB system disk (`os8088-360.img`, 512-byte clusters) had
  **37 of 706 clusters free** at the base, and about 136 of the used
  clusters are `.DRV` files from `drivers/`. Every driver ships on all four
  system-disk geometries, and kern_mega's disks carry the same files
  (`$(MEGADRIVERS)`). The disk pays for the **file**: the lz4-packed image,
  with trailing bss stripped.
* **RAM.** A loaded driver is a heap claim of **image + bss** for as long as
  it is attached. On most machines that is the whole session: SOUND, HDD,
  NET and ETHER attach at boot when their hardware is found. A loaded driver
  is resident on the machine just as the kernel is, and the 128KB machine
  floor is a real one.

**Report both, separately.** A cut that shrinks the image but makes the file
grow (code that packs worse) is a trade, and the note says so. **Take an item
only if neither the image nor the file grows**, unless the trade is large and
one-sided; that case goes under WANTS THE OWNER with both figures.

The banner in CLAUDE.md applies word for word: **bytes, not rungs, not
clusters.** Report the bytes. Report the clusters as a consequence, never as
the reason to take an item or to leave it.

### 0.1 The base, assembled (`3b16b60b`)

`os88drv` lines from the base `make`. "image" is what RAM pays, and bss is
the trailing zeros the file does not carry. "file" is what the disk pays.

| driver | source | image | bss | file | packed |
|---|---|---:|---:|---:|---:|
| ETHER.DRV | `drivers/ether/` | 17,389 | 480 | 11,106 | 65.7% |
| PCIETHER.DRV | `drivers/pciether/` (+ `ether/ethdrv.inc`) | 17,076 | 176 | 10,918 | 64.6% |
| HDDTOOL.DRV | `drivers/hdd/` | 16,508 | - | 12,216 | 74.0% |
| SOUND.DRV | `drivers/sound/` | 10,478 | - | 8,988 | 85.8% |
| SAVER.DRV | `drivers/saver/` | 10,247 | - | 7,108 | 69.4% |
| CDROM.DRV | `drivers/cdrom/` | 9,038 | 320 | 5,233 | 60.0% |
| VESA.DRV | `drivers/vesa/` | 8,105 | 416 | 3,848 | 50.0% |
| FSCHECK.DRV | `drivers/hdd/` | 6,298 | - | 5,280 | - |
| HTTPFS.DRV | `drivers/httpfs/` | 5,553 | 432 | 3,606 | 70.4% |
| RAMDISK.DRV | `drivers/ramdisk/` | 5,400 | 112 | 4,597 | 86.9% |
| NET.DRV | `drivers/net/` | 4,185 | 64 | 3,473 | 84.3% |
| HDD.DRV | `drivers/hdd/` | 4,096 | 16 | 3,571 | 87.5% |
| THMPAL.DRV | `drivers/thmpal/` | 2,575 | - | 2,259 | 87.7% |
| RAMPAGE.DRV | `drivers/ramdisk/` | 2,438 | 80 | 2,171 | 92.1% |
| HTTPPAGE.DRV | `drivers/httpfs/` | 2,350 | 176 | 2,005 | 92.2% |
| XMEM.DRV | `drivers/xmem/` | 2,037 | - | 1,878 | 92.2% |
| USBMOUSE.DRV | `drivers/usbmouse/` | 1,521 | - | 1,423 | 93.6% |
| EMS.DRV | `drivers/ems/` | 785 | 32 | 717 | 95.2% |
| OS88NET.COM | `drivers/net/` (DOS end, SPEC.md 62.12) | - | - | 10,784 | os88sfx |
| VMMOUSE.DRV | `drivers/vmmouse/` (kern_emu) | | | | |

`docs/plans/completed/dsp1/baseline.json` is `python3 tools/os88diskuse.py --save` of
the base build: every file on every 360KB image, with its clusters. **Measure
your own base in your own worktree anyway** (section 3). The coordinator's
JSON is the reference for the final total, not your yardstick.

### 0.2 Where the bytes usually are

The kernel passes found the same few shapes every time, and nobody has looked
for them in drivers yet:

* **single-caller routines** that can be written at the call site;
* **callee-saves no caller reads** (push/pop pairs around code whose callers
  do not care);
* **shared tails**: identical exit sequences that can be one;
* **relaxed jumps** that can be short;
* **dead stores and dead reloads**;
* **tables built at assembly time that could be computed at attach**, or the
  reverse, whichever is smaller;
* **strings.** A driver carries its own error, probe and settings text. Do
  not reword it: changed wording is WANTS THE OWNER. Sharing identical
  strings, or sharing a common prefix, is fine.

**Also look at what is only needed at attach.** A driver whose attach and
probe code is a large share of its image can, in principle, give that code
back after attach. Whether the kernel allows a driver to shrink its own
claim is a question about `drivers/os88drv.inc` and the loader, so it is
**CROSS-FILE**, not yours to build. Price it in your note (attach-only
bytes, per driver) and the coordinator will decide whether to propose it to
the owner.

## 1. The rules this pass

* **Functionality: byte-identical behaviour is the bar**, refusal paths
  included. A message reworded or dropped, a probe that tries less hardware,
  a limit lowered, a settings row removed: all WANTS THE OWNER, with bytes.
* **The ABI is frozen.** Do not change `DRV_VER`, the driver header, any verb
  number, or any layout a package or the kernel reads. The package-facing
  headers inside `drivers/` are ABI because packages and the kernel
  `%include` them:
  - `ether/netpkg.inc`, `ether/tcp.inc`, `ether/inet.inc`, `ether/dns.inc`
    (Browser, Telnet, FTPD, DOS, The Wire, WireChat, Install);
  - `sound/sndpkg.inc` (Mixer, MIDIRack, C64, ARCADE, RealDMG, XTMP3, WIN16,
    SNDDIAG);
  - `ramdisk/rdabi.inc`, `ramdisk/rdpkg.inc` (The Wire);
  - `httpfs/hfpkg.inc` (Install);
  - `thmpal/thmrows.inc` (`kernel/vga12.inc`);
  - `kernel/vxabi.inc`, `kernel/xmabi.inc`, `kernel/vmmabi.inc` (shared as
    source with the kernel).

  Their **values and layouts are frozen**. A comment edit is fine. Rearranging
  a macro body without changing what it emits for any includer is fine,
  provided you prove it by building every includer and `cmp`-ing the result.
  A win that needs an ABI change goes under WANTS AN ABI BREAK, with bytes.
* **Off limits: edit nothing outside your own directories**, except as listed
  below. Off-limits paths include `drivers/os88drv.inc`, everything under
  `apps/`, `kernel/` and `boot/`, everything under `tools/`, and the
  `Makefile`. Anything you find there goes in your note's CROSS-FILE, with
  its bytes. The exceptions:
  - **SPEC.md, your drivers' own sections**, where a figure you moved is
    quoted (a size, a byte count). SPEC.md is the contract, and a stale
    figure is a doc bug. Do not move a contract sentence.
  - **`tests/`**, only where a host-side gate in the fast tier mirrors a
    figure of *your* driver and fails because you moved it (a size, an
    offset, a ratchet). Update it in the direction the ratchet turns, never
    loosen it, and say so in the note.
* **Speed.** Read the `docs/plans/completed/ksp18/BRIEF.md` section 1
  principle with "hot path" meaning *your driver's* hot paths. A driver's
  hot paths are its **ISRs** (SOUND's direct-ring and DMA ISRs, the mouse
  ISRs, ETHER's and PCIETHER's receive), its **per-byte, per-sample and
  per-packet loops** (NET's line discipline, the TCP/IP fast path, SAVER's
  per-frame drawing, RAMDISK's and XMEM's transfers, HDD's sector transfer,
  CDROM's block read), and **anything called per tick**.
  - A hot path may change only if it is **cycle-neutral or faster**. Put the
    count in the note: a unicorn harness against the base bytes, or a hand
    count in the 8088's `max(clocks, 4 x instruction bytes)` terms, shown.
  - Attach, probe, settings, error and diagnostic paths are **cold**: size
    beats speed there.
  - **No change adds a transfer call** to any operation: an `int 13h`, a
    controller command, a packet, or an XMS move. Anything on a transfer
    path names its scenario and its call count before and after.
  - **SOUND's ISR budget and the mouse ISR budgets are real-time**, so read
    the SPEC.md section for the ISR before touching it.
* **8086 only**: `cpu 8086`, `-w+error`, CLAUDE.md's Hard rules.
* **kern_mega runs these same binaries.** On kern_mega a claim is an LDT
  **selector**, not a paragraph (SPEC.md 20.18, 50.6.11), so segment
  arithmetic a driver does on a claim must stay behind `OSAPI_SEG`. The
  protected-mode arms (`CPU_F_PM` tests, the selector cells) must keep
  working. `tests/megapkg.txt`'s per-file allowance only turns one way. Run
  `make -j2 mega` at your tip.
* **Knob variants must still assemble.** Grep the Makefile for every rule
  that builds your sources: SOUND's `SNDDEF`, `NOPICOMEM` and the `SNDDIAG`
  debug disk (with `tests/unit/t_debugdisk.py` holding the shipped arm
  byte-identical to the hooks-not-taken build), any `-D` arms the other
  drivers' rules pass, and `make emu` for VMMOUSE. Build them all at your
  tip.

## 2. The seven agents and what each owns

**Ownership is by directory.** Anything you see in another agent's
directory, or in a shared file, goes in CROSS-FILE with its bytes.

**An eighth agent, `kmod`**, was added after `hdd` finished. It covers the
kernel's ON-DEMAND MODULES, which also ship as `.DRV` files on the system
disk but are cut out of the kernel build by `tools/os88mod.py` (SPEC.md 2.8).

* **Modules and sizes.** On kern_big: CTRL 15,459 (shipped plain), DISKOPS
  10,216, STIKMENU 7,926, HIBER 7,715, FSCHECK 6,298, DESK 4,432, FORMAT
  3,179, DOCK 2,232, EXTD 1,927, WIN95 1,570 and SERIAL 919. On kern_small:
  CTRL, FORMAT, DISKOPS, FILECP, FDLG and SERIAL.
* **What it owns.** The module-only code in `kernel/`. Its rule is section
  2.1 below, not section 1's directory fence.

### 2.1 `kmod`'s rules

* You own the code that assembles INTO a module image and nowhere else. Find
  it from `tools/os88mod.py`'s split and the module sections in
  `kernel/kernel.asm` and `kernel/mod.inc`.
* **The resident kernel may not move by a byte** on kern_big, kern_small,
  kern_emu or kern_mega. Prove it with `tools/kernsize.py --json` absolutes,
  and with `cmp` of each kernel's resident sections where you can. This
  excludes the build number, so compare at ONE commit, as CLAUDE.md
  explains. A module saving that needs a resident change is CROSS-FILE.
* **The module ABI (its entry table, `mod_tab` rows, the layout stamp) is
  frozen.**
* **No feature may come to need the system disk** where it did not before.
* **CTRL.DRV ships plain on kern_big on purpose** (SPEC.md 2.8,
  `os88mod.py --plain 0`), so there its image bytes are its disk bytes.

| agent | owns | drivers | start with |
|---|---|---|---|
| **ether** | `drivers/ether/`, `drivers/pciether/`, `drivers/wirezone.inc` (NET.DRV includes it too: build NET.DRV and `cmp` when you touch it) | ETHER.DRV, PCIETHER.DRV | the two largest images in `drivers/`. `ethdrv.inc` is shared by both, so a byte there counts twice. The TCP/IP stack's per-packet path is hot. The ARP, DHCP and DNS setup, the probe and the settings are cold. QEMU is its emulator (CLAUDE.md's closed list, item 5) |
| **netfs** | `drivers/net/`, `drivers/httpfs/` | NET.DRV, OS88NET.COM (+ `os88sfx.asm`), HTTPFS.DRV, HTTPPAGE.DRV | OS88NET.COM is 10.8KB of DOS program that ships as a file, so its file size is disk bytes. NET's serial line discipline is per byte, so it is hot |
| **hdd** | `drivers/hdd/` | HDD.DRV, HDDTOOL.DRV, FSCHECK.DRV | HDDTOOL is 16.5KB and almost all cold (partitioning, format, the tool's UI), so it is the largest cold target in the pass. HDD.DRV's sector transfer and its XMS fill (`hdxms.inc`) are hot. Read SPEC.md 52 first, especially rung 1's 286 gate |
| **sound** | `drivers/sound/` | SOUND.DRV (and the `snddiag/` debug-disk build) | the image packs to 85.8%, so code bytes are disk bytes here. The ISRs, the mixer and the stream are hot. The probes (SB, MPU, Covox, PicoMEM) and the quick-volume popup are cold |
| **saver** | `drivers/saver/`, `drivers/thmpal/` | SAVER.DRV, THMPAL.DRV | SAVER is mostly data: a sine table, mode state, vertex lists and strings (Makefile comment at `saver.bin`). A table that can be generated at attach is a candidate. The per-frame drawing is hot |
| **vesa** | `drivers/vesa/`, `drivers/xmem/`, `drivers/ems/`, `drivers/usbmouse/`, `drivers/vmmouse/` | VESA.DRV, XMEM.DRV, EMS.DRV, USBMOUSE.DRV, VMMOUSE.DRV | VESA's image is 8.1KB and packs to 50%, so look at what packs and what does not. XMEM's moves and the mouse ISRs are hot. VMMOUSE is kern_emu's (`make emu`) |
| **cdram** | `drivers/cdrom/`, `drivers/ramdisk/` | CDROM.DRV, RAMDISK.DRV, RAMPAGE.DRV | RAMDISK `incbin`s a `mines.o88` (`ramdisk.asm`): find out why, and what it costs the image. CDROM's ISO-9660 and ATAPI paths. Block reads are hot, the probes cold |

## 3. Method and environment

* **First command**: `cd /home/user/wt/<name> && git status`. You should be
  on `dsp1-<name>`; if you are not, stop and say so. **Never `cd` into
  `/home/user/os8088`**: that is the coordinator's tree. **Never `git
  stash`**: it is one stack for every worktree. **Never `git fetch`, rebase
  or merge.**
* **Scratch**: your own directory, `<scratchpad>/dsp1-<name>/`, never the
  shared scratchpad root and never `/tmp` directly.
* **The box**: 4 cores and 15 GB, shared by seven agents. Use `make -j2`
  every time, and never run two builds in your tree at once. The first
  `make` in a fresh worktree builds everything, which takes several minutes
  while six others do the same, so **start it first** (`make -j2 >
  <scratch>/base.log 2>&1`, run in the background, held). Then record your
  base: the `os88drv:` lines for your drivers, and `python3
  tools/os88diskuse.py --save <scratch>/base.json`.
* **Per batch**: build your drivers' targets (`make -j2 build/<x>.drv` and
  the variants), then the images that carry them
  (`make -j2 build/os8088-360.img build/os8088.img`). Compare with `python3
  tools/os88diskuse.py --diff <scratch>/base.json`. Commit only after a full
  `make -j2`, because that is the fast tier and the doc gates. One full
  `make` per commit batch is enough; do not run one after every edit.
* **Diffing**: `python3 tools/os88rdiff.py drivers/<dir>/<x>.asm <base> .`
  shows every routine you changed, what relaxed and what shifted. Unpacked
  image sizes come from `os88drv.image_unwrap` (`tools/os88drv.py`).
* **Equivalence**: "it assembled" is not equivalence. Where you change a
  path, prove it: a scratch unicorn harness (`unicorn` 2.1.4 is installed)
  run against the base and tip bytes, or a careful reading recorded in the
  note. **Verify every moved entry and fall-through by READING**, because a
  reference walk misses fall-through entries.
* **Rows: you run NO soak rows** (CLAUDE.md's Testing section: this pass is
  batches, and the coordinator runs the deduplicated union once after the
  last merge). Running `python3 tests/<row>.py` by hand counts as running a
  row. **NAME** the `tests/suite.py` rows that cover each change: grep
  `tests/suite.py` for your driver, its directory and its SPEC.md section,
  and list them in the note. The coordinator's final pass runs them.
* **One boot to LOOK is allowed**, at your tip, if you changed an attach,
  probe or ISR path. Confirm that the driver attaches (the Control Panel's
  Drivers page, or its feature working once), then stop. Choose the
  emulator by CLAUDE.md's rule: MartyPC by default; QEMU for ETHER,
  PCIETHER, the PS/2 and VMware mice, and 286+ paths.
  - For MartyPC, in your worktree: `mkdir -p build && ln -sfn
    /home/user/os8088/build/martypc build/martypc`. The coordinator is
    building it. If it is not there yet, skip the boot rather than build a
    second copy.
  - **Never kill an emulator you did not start**: `os88marty.py reap` and
    `instances` show whose is whose.
* **Never hand `/dev/null` to nasm** as `-o` or `-l`. Never `make -n` with a
  knob into `build/`.
* `apps/wireball/art/wbfront.inc`: if it shows as modified, `git checkout`
  it before you commit, and never commit it.
* **Commit in small batches** on `dsp1-<name>`, one subject per commit.
  - Messages read `<name>: <what> - <DRIVER> image -N, file -M (driver size
    pass 1)`.
  - End each with the attribution trailer the session gives you. Never put a
    model name in a commit or a file.
  - After adding or editing your note, run `python3 tools/os88index.py` and
    commit `docs/INDEX.md` with it. The coordinator re-runs it at each merge,
    so conflicts there are expected and harmless.

## 4. The note

Write `docs/plans/completed/dsp1/<name>.md`, with these sections:

1. **BASE AND TIP**: a table, per driver, of image, bss, file and clusters
   on each disk that carries it, base against tip, with deltas.
2. **TAKEN**: each item with its image and file bytes, hot or cold, cycles if
   hot, and how equivalence was shown.
3. **REFUSED**: the bytes it would have saved, and why it was refused.
4. **WANTS THE OWNER**: functionality or wording changes, with bytes.
5. **WANTS AN ABI BREAK**: with bytes and every includer named.
6. **CROSS-FILE**: shared includes, `os88drv.inc`, the loader, the
   Makefile, other agents' directories, with bytes. Include the price of
   attach-only code from section 0.2 here.
7. **ROWS TO RUN**: the `tests/suite.py` row names covering every change.
8. **LOOKED**: the one boot, if taken, and what it showed.
9. **DEFECTS FOUND**: fixed or not, and how found. A driver nobody has
   size-passed has never been read this closely. A real bug you find is worth
   more than its bytes. Fix it only if the fix is small and obviously right,
   and otherwise describe it.

**Your final message to the coordinator is ONE message**, containing:

* the branch tip;
* per driver, image and file base to tip, and the total;
* the system-disk cluster delta at 360KB;
* the WANTS THE OWNER and WANTS AN ABI BREAK items;
* the rows you name;
* any defects;
* anything that blocked you.
