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
| `roms/` | the 27 OCT 82 IBM 5150 BIOS — the ROM the calibration machine has |
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

*The BIOS in `roms/` is IBM's, and is the one file in this tree not covered by
the project's own licence.*
