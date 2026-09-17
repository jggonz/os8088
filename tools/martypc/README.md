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
| `patches/` | everything else: the upstream files that had to change, plus `devices/sblaster.rs`, the Sound Blaster upstream does not have, `04-floppy-disk-timing.patch`, the platter, and `05-opl3-second-array.patch`, the OPL3's second register array |
| `configs/` | the machine configs (docs/MARTYPC-DEBUG.md's *The list*), the first shaped after docs/FIELD-MACHINES.md's 5150 |
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

**Several instances run side by side, and that is why `debug_server.rs`
changed rather than only the Python.** `MARTYPC_DEBUG_ADDR=127.0.0.1:0` asks
the OS for a free port under the bind and `MARTYPC_DEBUG_PORTFILE=<path>` gets
the one it picked — the only allocation that cannot race, because a client
probing for a quiet port has let go of it before the emulator binds. A bind
that fails now prints to **stderr** and exits, instead of logging at a level
that may be off and running on unreachable for ever; a **second client** is
accepted and refused with a sentence naming the one that holds it, instead of
being left in the accept backlog to hang; and `ping` reports the process's own
pid, so a launcher can prove it is talking to the emulator it started rather
than infer it from a cycle count. `os88marty.launch` does the rest — a
directory per instance, a registry, orphan reaping — and
[docs/MARTYPC-DEBUG.md](../../docs/MARTYPC-DEBUG.md)'s *Several at once* is the
account.

**Reach for this first** when what you are testing runs on an 8088 — all
three of SPEC.md §39's adapters, VGA mode 12h included — screenshots included (`os88marty.py shot out.png` reads the
framebuffer out of VRAM, so there is no reason to start QEMU to look at a
screen) and sound included (`MARTYPC_WAV=` captures one wav per source, and
the `os8088_5150_sb` machine has a PC speaker, an OPL3 **and** a Sound
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

**Every `type = "AdLib"` card is an OPL3, and has been all along.** The core
is Nuked-OPL3 (a YMF262), and its status byte reads bits 1 and 2 **clear** -
which is the OPL3 answer to SPEC.md 34.11.1's `(s2 & 06h) = 0` test - on the
stock pin too. What upstream did not do is decode the chip's second address
pair, so 38Ah/38Bh went nowhere: a driver that detected OPL3 honestly wrote
105h (`NEW`) and the whole 100h-1FFh array into the void.
`patches/05-opl3-second-array.patch` does three things, all in
`devices/adlib.rs`:

- base+2 is the second array's address register and base+3 its data register,
  and base+2 reads status as base+0 does (a YMF262 decodes only A0 on a read);
- **`NEW` is modelled on the address decode**, as DOSBox models it and a
  YMF262 behaves: with `NEW` = 0 an address written to base+2 selects the FIRST
  array, except 05h itself. So SPEC.md 34.11.2's rule - no 1xxh write but 105h
  while `NEW` = 0 - is breakable here in the way it is on the chip;
- **`MARTYPC_OPL2=1`** in the environment makes every AdLib card in that
  process an **OPL2**: status bits 1-2 read set, base+2/base+3 decode nothing.
  There is no OPL2 here otherwise, and a RAD 2.1 tune's refusal needs one.

Proven by a parked probe (no BIOS, IF=0) that runs SPEC.md 34.11.1's
detection and then plays a note on the second array's channel 0 alone, with
the AdLib WAV captured (RMS):

| run | s2 | detected | status at 38Ah | note |
|---|---|---|---|---|
| unpatched pin, second array | C0h | OPL3 | FFh | **0.0**, silent |
| patched, second array, `NEW` set | C0h | OPL3 | C0h | 3119.9 |
| patched, second array panned left, `NEW` set | C0h | OPL3 | C0h | L 3119.9 / R 0.0 - the pan is only honoured with `NEW` = 1, so 105h landed |
| patched, `NEW` never set, then the FIRST array's channel 0 muted through 388h | C0h | OPL3 | C0h | **86.7** - the "second array" writes landed on the first |
| patched, `NEW` set, the first array's channel 0 muted the same way | C0h | OPL3 | C0h | 3119.8 - the second array's note is untouched |
| `MARTYPC_OPL2=1`, second array | **C6h** | **OPL2** | FFh | **0.0** |
| `MARTYPC_OPL2=1`, the same note on the primary ports | C6h | OPL2 | FFh | 3119.9 |

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

The `_gla` and `os8088_xt_*` machines use **GLaBIOS**, which MartyPC bundles,
and they build and run with nothing added. The period-accurate ones
(`rom_set = "ibm5150_82_v4"`) need your own dump of the 27 OCT 82 5150 BIOS:

```
tools/martypc/roms/BIOS_IBM5150_27OCT82_1501476_U33.BIN
8192 bytes, md5 f453eb2df6daf21ec644d33663d85434
```

`build.sh` names the file and this checksum if it does not find one.
