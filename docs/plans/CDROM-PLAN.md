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
- [ ] **Audio (wave 2):** as a user with an audio CD, I open the **CD
  Player** and play, pause, stop, skip and eject; I see the track and the
  time; the sound comes from the drive. The package door (`cdpkg.inc`) and
  the player. On QEMU, whose drive has no audio commands, the player says so.
- [ ] **Tracks as files (wave 2):** an audio disc's root lists
  `TRACKnn.CDA`; double-clicking one opens the CD Player on that track.
- [ ] **Period machine (wave 3):** a 386 under 86Box with an audio CUE/BIN
  (`vm/386-cdrom`, `make cdaudioimg`) boots with the CD Player started from
  `\AUTOSTRT`, to look at a track playing.
- [ ] **Acceptance:** the affected soak union once and `test-full` once,
  after the last batch; budgets; the guide paid; INDEX regenerated.

## Boundaries and verification

The kernel learns a class number and a row - nothing about ISO 9660 or
ATAPI. Volumes go through `OSAPI_VOL_ADD` and `FSV_*` (§62.9), the audio
through `OSAPI_DRV_CALL` (§20.11). The drive's task file is polled; the
device control register is never written, so a hard disk sharing the
channel keeps its interrupt.

`tools/os88cdiso.py` is the reference implementation: the 8.3 mangle and the
handle encoding, written once in Python, and the fixture builder. Gates:
`cdrom` and `cdromempty` (QEMU, soak). The 8088 refusal is arithmetic
(§52.1's 8-bit bus) and has no emulator here that hosts both an IDE drive
and an 8088.

Per-wave checks are host builds (`make`, the fast tier), `kernsize` on both
kernels, and a boot to look. The soak union and `test-full` run once at the
end (CLAUDE.md, Testing).
