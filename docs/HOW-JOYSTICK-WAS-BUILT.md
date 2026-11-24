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

## 2026-11-23 - J4, the instrument: moving a stick that does not exist

There is no joystick here and QEMU has no game port, so every row about the
driver depends on MartyPC. What was observed: MartyPC already models the IBM
adapter - four one-shots charging at IBM's `24.2us + 0.011us/ohm`, four
switches - but only a GUI could move the sticks, and its setters are gated by
the controller layout, with Y inverted. Nothing in the debug server reached it.

Decisions:

- **Patch 18 adds raw setters; the verb lives in `debug_server.rs`.** The game
  port file is untouched by patches 01-17, so the patch is one hunk and
  applies in sequence (`make marty` from a clean checkout at the pin).
- **The layout is ignored by the verb.** Every pot and switch is wired on the
  real card whichever stick is plugged in, and a driver has to cope; the layout
  only chooses the GUI. "One stick, four buttons" is therefore stick A and
  mask 0xF, not a second machine.
- **Sticks in -1..1 units (left and up fast) or raw ohms**, because a
  calibration test wants an exact time and a user-facing row wants a direction.
- **Two new machines, not a change to the standard ones.** A game port changes
  the BIOS equipment word and what REDLINE's inspector prints (it reads 201h),
  so adding it to `os8088_5150_cga_gla` would move every older row's machine.
  `os8088_5150_cga_gla_joy` and `os8088_xt_vga_joy` are the standard twin and
  the VGA XT plus the `game_port` overlay.

Discovered while measuring: `advance(cycles=)` runs in 1,192-cycle batches
(250us) - too coarse for an axis that falls in 25us - and the `out 201h`
that starts the one-shots has no clock of its own while paused. So the
timing is a guest loop (`out`, then `in al,dx / inc cx / test al,1 / jnz`)
parked with `park`, ended by a breakpoint, and the cycle counter is differenced.
An early draft of the docs said the bit rises; it is 1 from the `out` and
FALLS, which is the wrong way round to guess and is why the verb's docstring
now says so.

What was tested (a throwaway script, not a suite row, on
`os8088_5150_cga_gla_joy`, system disk booted to the desktop and paused): A.x
at 0, 25K, 50K, 75K, 100K ohm fell after 242, 1,554, 2,866, 4,178, 5,490
cycles - 1,312 per 25K (275us), and 0 to 100K was 1,099.6us against the model's
1,100us. `a=(-1,0)`/`(0,1)` were the fast ends and `(1,0)`/`(0,-1)` the slow
ones. Buttons 1 and 3 read 0xA0 on 201h, none 0xF0. One loop iteration is 41
cycles (8.6us), the resolution. Not tested: the verb's error on a machine with no game
port, kern_small, and 86Box.
