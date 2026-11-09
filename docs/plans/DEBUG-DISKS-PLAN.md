# Debug disks (SPEC.md 127)

*Contract: SPEC.md 127. Diary: [HOW-DEBUG-DISKS-WERE-BUILT.md](../HOW-DEBUG-DISKS-WERE-BUILT.md).*

## Why

A field report reached us that an ESS ES1868F in Sound Blaster mode is found,
its mixer and volume work, and MIDI and MOD playback sounds for two or three
seconds and then goes silent while the players' indicators keep moving. Every
emulator here plays it through: 86Box's ES1868 and ES1688, at 8,000, 22,050
and 44,100 Hz, on kern_big and kern_mega, with no underrun and the pitch
right. So the defect is on hardware we do not have, and the reporter has no
debugger. What they do have is a camera.

The owner's ask: a disk that boots and puts on the screen what the driver
auto-detected, on any of the four adapters, so a user can photograph it and
send it - the first of a **series** of debug disks built from our driver
source, kept up to date as the drivers grow, and shipped in the release zip.

## Decisions

| question | taken | judged against |
|---|---|---|
| what boots | **the OS, with a diagnostic package in `\AUTOSTRT`** (owner's choice) | a standalone bootdiag-style program with its own copy of the probes: it would not be the driver's code, so it would answer a different question |
| where the detail comes from | **the shipped driver source built with `-DSNDDIAG`**, a record filled by hooks at the probe and stream sites, and one more package verb | the package probing the ports itself (a second implementation); a kernel slot (resident cost on every machine) |
| what the shipped driver pays | **nothing**: every hook is a macro that expands to nothing without the define, held byte-identical by `t_debugdisk` | an always-present record (bytes on every machine for a field aid) |
| extend `make bootdiag`? | **no**: a sibling in the same family. bootdiag is for a machine that cannot boot, and runs no kernel and no driver | one disk doing both, which would have to carry a kernel it cannot assume |
| the screen | **17 rows x 78 columns of text on every adapter**, CGA's 640x200 the floor, `font_run` per changed row | a graphic face per adapter (cost, and a photograph of text is what we read) |
| the exercise | **a ten-second tone through the real stream path**, one row a second, auto-started | asking the user to open a player (the report is then a second program's behaviour) |

## Stories

- [x] **S1 The record.** `drivers/sound/snddiag.inc`: the layout (`SDG_O_*`),
  the hooks and `SNDV_DIAG` = 14 (copy, mixer read, `D8h`, idle DSP command).
  Shipped `SOUND.DRV` byte-identical; the diag build ~850 bytes larger.
- [x] **S2 SNDDIAG.O88.** Three pages - Detect, Play test, Registers - in
  17x78 text; `1 2 3 P R V Esc`. Runs on a shipped driver too, saying so.
- [x] **S3 The disks.** `debug-sound{,-120,-720,-360}.img` in `$(SHIPIMGS)`
  and `make debugdisks`; `SYSTEM.CFG` with `DW` bit 0, `\AUTOSTRT` link,
  `SNDDEBUG.TXT`. The image gates (`t_image`, `t_diskverify`, `t_canary`,
  `t_blobruns`) list them.
- [x] **S4 The gates.** `t_debugdisk` (fast): the hooks-never-written
  comparison, red on one `nop`; each image's driver, program, link and
  configuration. `snddiag` (soak, MartyPC): the disk booted with nobody at
  it, read out of the package's own text buffer.
- [x] **S5 The release.** The zip's `DEBUG/` folder (`SOUND144.IMG` ...) and
  a README section saying what to send.
- [ ] **S6 The next debug disks**, one per subsystem as the field asks: the
  mouse (MOUSECHK, draft PR #340, is the interactive half of that already), the disk
  drivers, the network card. Each follows 127.1's rules.

## Evidence

| machine | what it showed |
|---|---|
| QEMU, `-device sb16` | DSP 4.05 at 220h, IRQ 5 read from mixer 80h = 02h, HDMA 5, CT1745; play test 22,050 Hz, ten rows of ~22,000 bytes and 10-11 IRQs, no watchdog; `D8h` answers nothing (QEMU's SB16) and shows `na` |
| 86Box, Pentium + ES1868 (`vm/pentium-ess`) | DSP 3.01, IRQ 5 by the F2h probe, CT1345-compatible mixer, mixer 40h = `18 68 08 00` -> **ES1868, config port 0800h**, DSP `E7h` = `68 8B`; play test steady, `D8h` = FF every second, ESS 60h/62h/64h = 3F/3F/20 |

What 86Box cannot show: its ESS `D1h`/`D3h` are no-ops, so a voice switched
off by the chip itself can only be seen on the real one - which is what the
`D8h` column is for.
