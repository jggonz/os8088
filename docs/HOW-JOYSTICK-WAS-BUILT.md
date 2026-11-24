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

## 2026-11-24 - J2: live and wide driver pages (SPEC.md 31.9.4, 135.3.1)

Built: every driver page call now carries the pane - paint and tick get
CX/BX = its width and height, every call SI = the panel's window, and a key
gets DI/DX too. `DSV_CPTICK` is the first service cell past `DSV_FLOOR`,
length-tested against the header's +15 (a driver declares a `DSV_MAX` = 38
table), and `OS88_DRV_CPWIDE` (bit 14 of +10) has the wide face host the page
over its whole page area. Everything but the gate is in `CTRL.DRV` and
`CPWIDE.DRV`; kern_small is byte-identical.

Discoveries:

- **kern_emu's kernel rung had 16 bytes left**, where kern_big's had 262. The
  first gate - every second tick, while `[cp_tickp]` names a driver record -
  was 14 bytes and crossed kern_emu's rung. So the resident part is only "go
  on while `[cp_tickp]` is a driver record" (7 bytes, +8 `.text` after
  alignment), and `CTRL.DRV`'s `cp_tick_x` drops the odd ticks itself. The
  price is an odd tick that takes the lock and draws nothing. Told the lead,
  whose answer (D3) was to keep JOYSTICK.DRV off kern_emu.
- **The wide face did not need to learn where a wide page is.** `cp_origin`
  is already the one place that knows the face; answering the page area's
  origin there for a wide page made the paint, press, release, drag, key and
  tick all land right with no code of their own. The ground was the only
  real change: `cpw_page` now paints the page first and lays the hosted
  ground only when `CPG_HPAINT` says the page was not wide.
- **The live view is clipped, not refused.** The Date/Time clock refuses to
  draw when anything overlaps the panel (`wm_obscured`); a live test view
  should keep moving, so the tick arms the panel's clip (`wm_clip_set`)
  instead.

Measured: kern_big text +8 (rung 262 -> 254 left), kern_emu +8 (16 -> 8),
kern_mega +8 (image rung 210 -> 202); `CTRL.DRV` 16,735 -> 17,002 (kern_mega
16,974 -> 17,241, 167 under `MOD_MAX_KB`); `CPWIDE.DRV` +9.

Tested (QEMU, VGA, wide face), with a throwaway driver that was never
committed - it took `CDROM.DRV`'s place on a scratch copy of the system disk,
said `OS88_DRV_CPWIDE`, and drew its size and a tick counter: the page was
painted with 495x330 at the page area's corner and its frame touched the
list rule, the header and the status rule exactly; the counter advanced about
9 times a second; with Task Manager dragged over it the counter was clipped
and the other window untouched. The RAM disk page (hosted) looked as before,
its `+` acted, a typed size reached the field, and the Date/Time clock still
ticked once a second after leaving the live page.
On the compact face (`VIDEO=cga`, 1bpp) the same driver - its wide bit
ignored there - was painted with 222x132 and ticked about 9 times a second,
and the RAM disk page looked as before.
