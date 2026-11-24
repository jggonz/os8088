# How the joystick driver was built

A diary kept while `JOYSTICK.DRV` (SPEC.md 136) was built, for later reuse.
Observations and intentions are kept apart; what was tested is named.

## 2026-11-23 - the brief, and what the tree already had

The owner asked for one joystick driver modelled on Linux's analog driver,
Windows 3.1's joystick driver and OS/2's game port driver, set up once in
the Control Panel, with the page designed for both the wide (VGA/VESA) and
the compact Control Panel using Codex image generation.

Observed before designing: no package in the tree read port 201h. The C64
emulator's joystick was a keyset, the Apple II's paddles answered centre,
ARCADE's stick was keys. MartyPC, the cycle-accurate XT this project tests
on, already emulates the IBM game port; QEMU has none.

Decisions taken at the start:

- **A driver class, not a kernel slot.** The network set the precedent
  (SPEC.md 72): a package reaches hardware the kernel does not drive through
  `OSAPI_DRV_CALL`, so a machine without the driver refuses at one fence and
  the kernel learns nothing about joysticks. The resident cost is the class,
  the row and the settings blob.
- **No sampling unless asked.** A read is a timed firing of the one-shots
  with interrupts off for at most about 2.3 ms; there is no worker, so a
  machine with the driver ticked and no game running pays only the image.
- **A missing stick is not a refusal.** Port 201h with no adapter and with an
  adapter and no stick read the same, and sticks are plugged in while the
  machine runs, so the page says `No joystick connected` instead.
- **Two new page abilities for every driver** (SPEC.md 31.9.4): a live tick
  and a wide layout, because the Windows 3.1 applet's Test view is live and
  the wide Control Panel hosted driver pages at 229x132.

## 2026-11-23 - J3, the icons (agent `art`)

Codex's two icons (16x16, 32x32) were already clean: seven colours, all of them
palette entries or the key, so they went into the sheets unedited. The 16x16
sheet grew a third row (256x48, joystick at cell 32, the other fifteen cells
key colour) and the 32x32 sheet a thirteenth cell (416x32). The converter used
to demand that a sheet's cell count equal `CWI16_N` exactly, which a 48-cell
sheet would break; it now derives the grid from N (16 across at most) and
checks the sheet's size against it, so `CWI16_N` stays 33 rather than paying
2,400 bytes for fifteen empty cells (48 cells would have taken `CPWIDE.DAT` to
17,842, over the 16,384 ceiling). Measured: `CPWIDE.DAT` 14,642 -> 15,442
bytes unpacked (+800 = 160 + 640), 5,290 -> 5,544 packed. The wide face maps
`DRVC_JOY` (11, a literal until core's `os88api.inc` lands) to them in
`cpw_recicon`. Looked at: both icons decoded back from `build/cpwide.dat` and
zoomed; not yet seen in the running panel (no driver row exists on this branch).
