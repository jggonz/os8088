# CD-ROM — delivery plan

An IDE (ATAPI) CD-ROM drive as a loadable driver, `CDROM.DRV`: a data disc
opens on the desktop as a read-only drive, and an audio CD plays through the
drive's own audio output under a CD Player. SPEC.md §119 is the contract.
[Build diary](../HOW-CDROM-WAS-BUILT.md).

## Stories and waves

- [x] **Foundation:** isolate the worktree, map the OS (§51 drivers, §62.9's
  file redirector, §52's IDE transport, §54 associations), pin §119 and these
  stories, and open one draft PR.
- [x] **Kernel row (wave 1):** as a user with a CD-ROM drive, I can tick
  **CD-ROM** in the Control Panel's Drivers page. `DRVC_CD` = 9, a third file
  class beside the cable and the RAM disk, row 7 / SYSTEM.CFG bit 8, not
  wanted by default; the DOS box keeps it. **No rung crossed** on either
  kernel; kern_small unchanged.
- [x] **Data discs (wave 1):** as a user, I open the CD-ROM drive on the
  desktop and see the disc's files and folders with sensible 8.3 names; I
  can walk into folders and back up; programs launch off the disc; documents
  copy off it byte for byte; a write is refused as write-protected; an empty
  drive mounts nothing and the machine carries on.
- [x] **Audio (wave 2):** as a user with an audio CD, I open the **CD
  Player** and play, pause, stop, skip and eject; I see the track and the
  time; the sound comes from the drive. The package door (`cdpkg.inc`) and
  the player. On QEMU, whose drive has no audio commands, the player says so.
- [x] **Tracks as files (wave 2):** an audio disc's root lists
  `TRACKnn.CDA`; double-clicking one opens the CD Player on that track.
- [x] **Period machine (wave 2):** a 386 under 86Box with an audio CUE/BIN
  (`vm/386-cdrom`, `make cdaudioimg`) boots with the CD Player started from
  `\AUTOSTRT`, to look at a track playing.
- [x] **Acceptance:** the affected soak union once and `test-full` once,
  after the last batch; budgets; the guide paid; INDEX regenerated.
- [x] **CDRack (wave 4):** as a user, the player looks like the owner's 80s
  rack CD player mockups (`docs/cdrack/design/`): a seven-segment LCD with a
  track grid, picture buttons with captions, a volume slider, repeat /
  shuffle / program / remaining-time toggles and the track list. Pictures and
  both fonts are made on the host (`tools/os88cdart.py`), not drawn. Looked
  at on QEMU VGA (full, both themes, the compact player both themes), EGA
  (MID, colour) and CGA (compact, one bit); identity after a full repaint
  checked pixel for pixel.
- [x] **Themes (wave 4):** a colour theme and a black-and-white one, picked
  in Options; colour by default on EGA and VGA, black and white elsewhere and
  always on a one-bit display.
- [x] **Compact player (wave 4):** a mini player for the CGA's 200 lines, and
  on request on any screen; Expand and Options go back.
- [x] **Play, seen (wave 4):** the 386 under 86Box playing through CDRack's
  face - the latched Play, the counting LCD, the bar. The owner booted
  `vm/386-cdrom` and watched it play cleanly, after the merge: this host
  cannot send 86Box a key, and its BIOS had stopped in setup.

## Boundaries and verification

The kernel learns a class number and a row - nothing about ISO 9660 or
ATAPI. Volumes go through `OSAPI_VOL_ADD` and `FSV_*` (§62.9), the audio
through `OSAPI_DRV_CALL` (§20.11). The drive's task file is polled; the
device control register is never written, so a hard disk sharing the
channel keeps its interrupt.

`tools/os88cdiso.py` is the reference implementation: the 8.3 mangle and the
handle encoding, written once in Python, and the fixture builder. Gates:
`cdrom`, `cdromempty` and `cdromsim` (QEMU, soak); `vm/386-cdrom` (86Box,
to look at - the only emulator here that plays CD audio). The 8088 refusal is arithmetic
(§52.1's 8-bit bus) and has no emulator here that hosts both an IDE drive
and an 8088.

Per-wave checks are host builds (`make`, the fast tier), `kernsize` on both
kernels, and a boot to look. The soak union and `test-full` run once at the
end (CLAUDE.md, Testing).

## Result

Soak union (once, after the last batch): `cdrom`, `cdromsim`,
`cdromempty`, `hdtake`, `hdtakeboot`, `hdtakeblank`, `drvcall`, `drvup`,
`drvscroll`, `dosdrv`, `cpnames`, `hibernatedrv`, `rdmount`, `rdup`,
`rdmove`, `rdxms`, `syscheck`, `stikmenu`, `instkeep`, `scsize`,
`mixervga`, `mixercga`, `mixersb16` passed; `mixerhost` skipped (no
unicorn here). `install` failed - and fails identically on a pristine
`origin/main` build ("HDDTOOL.DRV is still held after Close"), so it is
main's. After an independent review's twelve fixes the three CD rows were
run again and passed, and 86Box played the disc through again. `test-full`:
62 passed, 1 skipped (`ctoolchain`, no C compiler installed).
