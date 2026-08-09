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
| `patches/` | everything else: the upstream files that had to change, plus `devices/sblaster.rs`, the Sound Blaster upstream does not have |
| `configs/` | five IBM 5150 machine configs shaped after docs/FIELD-MACHINES.md |
| `roms/` | **gitignored, and you supply it** — see the note at the bottom |
| `build.sh` | clone at the pin, patch, stage a run tree, build |

**Reach for this first** when what you are testing runs on an 8088 — all
three of SPEC.md §39's adapters, VGA mode 12h included — screenshots included (`os88marty.py shot out.png` reads the
framebuffer out of VRAM, so there is no reason to start QEMU to look at a
screen) and sound included (`MARTYPC_WAV=` captures one wav per source, and
the `os8088_5150_sb` machine has a PC speaker, an OPL2 **and** a Sound
Blaster). **It is cycle-accurate and it is not disk-accurate**: its
floppy is 30x fast, so no figure with a disk in its path means anything here,
and it would not have caught SPEC.md §18.91's `AL` bug any more than QEMU did.
docs/MARTYPC-DEBUG.md has the long version of both.

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
