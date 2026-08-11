# The MartyPC debugger

**Full documentation: [docs/MARTYPC-DEBUG.md](../../docs/MARTYPC-DEBUG.md).**
Build it with `make marty` (or `./build.sh`); drive it with
`tools/os88marty.py`.

This directory is the *whole* of os8088's changes to
[MartyPC](https://github.com/dbalsom/martypc) — a debug server for its
headless frontend, so a host process gets memory, registers, I/O ports,
breakpoints, single-step and cycle counts on a running os8088 with **no code
in the guest at all**.

| | |
|---|---|
| `UPSTREAM` | the pinned commit. Editing it is a deliberate act, not maintenance |
| `debug_server.rs` | the new module, copied in whole |
| `patches/` | everything else: the upstream files that had to change, plus `devices/sblaster.rs`, the Sound Blaster upstream does not have, and `04-floppy-disk-timing.patch`, the platter |
| `configs/` | five IBM 5150 machine configs shaped after docs/FIELD-MACHINES.md |
| `roms/` | **gitignored, and you supply it** — see the note at the bottom |
| `build.sh` | clone at the pin, patch, stage a run tree, build |

**Adding a patch that touches a file an earlier patch already touches is the
trap here, and it has been sprung twice.** `build.sh` applies `patches/*.patch`
in glob order onto a tree reset to the pin, so patch 04 is applied to
**pin + 01 + 02 + 03** — but a bare `git diff` in `build/martypc/src` is taken
against the *pin*, so for any file 01 also edits it silently emits 01's hunks
as well and the next clean build dies with `patch does not apply`. Regenerate
against the right base:

```sh
cd build/martypc/src
git stash                                  # your changes, briefly
git checkout --force $PIN && git clean -qfd
cp ../../../tools/martypc/debug_server.rs crates/binaries/martypc_headless/src/debug_server.rs
for p in 01 02 03; do git apply ../../../tools/martypc/patches/$p-*.patch; done
git add -A && git commit -qm "pin + 01/02/03"  # <- your patch's base
git stash pop
git diff > ../../../tools/martypc/patches/NN-yours.patch
```

Then throw the temp commit away and run `make marty` — a clean build from the
pin is the only thing that proves the patch applies.

**Reach for this first** when what you are testing runs on an 8088 — all
three of SPEC.md §39's adapters, VGA mode 12h included — screenshots included (`os88marty.py shot out.png` reads the
framebuffer out of VRAM, so there is no reason to start QEMU to look at a
screen) and sound included (`MARTYPC_WAV=` captures one wav per source, and
the `os8088_5150_sb` machine has a PC speaker, an OPL2 **and** a Sound
Blaster). **Its floppy now turns** — `patches/04-floppy-disk-timing.patch`
gives the drive a platter, a data rate, a seek and a configurable interleave,
so a read costs revolutions instead of arriving instantly
(docs/MARTYPC-DEBUG.md). On the IBM ROM `tests/sysbench`'s whole raw
`int 13h` block lands within one measurement quantum of the field machine's
own report off the identical image — seven of thirteen rows exactly — and the
boot at **188 ticks against 205**, where it used to be 41
(PERFORMANCE.md Sets 35/37). What that does *not* buy is a source of truth
about the CHIP: what a real 765 puts in ST1, or whether a real drive returns
short, is the 5150's question. What the **ROM** does is reproduced, because
MartyPC runs the ROM — §18.91's `AL` bug shows here.

**The period-accurate machines need the ROM below.** Without it only the
GLaBIOS twins run, and a GLaBIOS machine is not where a disk number comes
from — that BIOS abandons a floppy operation after ~250 ms.

**What the guest WROTE to a floppy is a different question, and `flush`
answers it.** MartyPC keeps a mounted image in RAM and never writes it back —
that is the eframe frontend's Media ▸ Save Floppy As, which a headless run has
no way to reach — so the debug server grew the same `fluxfox::ImageWriter`
call as a command, and `tools/os88flush.py` is the client: `diff` for what
changed since the mount, `ls`/`get` for the volume read with no kernel code
involved, `verify` for `os88disk.py`'s structural fsck. It is the only route
to os8088's write path that is not also os8088's read path.

## The IBM BIOS is not in this tree

`roms/` is gitignored and ships empty. The BIOS is IBM's, IBM has never licensed
it for redistribution, and CONTRIBUTING.md puts the whole tree under one MIT
file — which is a grant this project cannot make for someone else's ROM. The
same reasoning already kept the IBM/Xebec hard disk controller out
(docs/MARTYPC-DEBUG.md).

Three of the eight machines use **GLaBIOS**, which MartyPC bundles, and they
build and run with nothing added. The five period-accurate ones
(`rom_set = "ibm5150_82_v4"`) need your own dump of the 27 OCT 82 5150 BIOS:

```
tools/martypc/roms/BIOS_IBM5150_27OCT82_1501476_U33.BIN
8192 bytes, md5 f453eb2df6daf21ec644d33663d85434
```

`build.sh` names the file and this checksum if it does not find one.
