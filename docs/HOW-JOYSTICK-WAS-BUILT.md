# How the joystick driver was built

A diary kept while `JOYSTICK.DRV` (SPEC.md 137) was built, for later reuse.
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

## 2026-11-24 - J1: the driver and its ABI

**What was built.** `JOYSTICK.DRV` (`drivers/joy/joy.asm`, 1,571-byte image,
1,408 bytes on disk), class `DRVC_JOY` = 11 with its `drv_tab` row after
`PC1MOUSE.DRV` (bit 13), a 20-byte settings blob under its own `SYSTEM.CFG`
key `'JS'`, `apps/os88joy.inc`, and the C binding. The ABI files were
committed first, so the five wave-2 agents could start from them.

**Decisions taken while building.**

- *The verbs and the record live in `apps/os88api.inc`, not the driver's
  header.* Every adopter already includes os88api.inc, and each package is
  built three times (kern_big, kern_emu, kern_mega rules), so a constant in
  `drivers/joy/joypkg.inc` would have cost every adopter three `-I` edits in a
  Makefile none of them owns. EMS had set the precedent. `joypkg.inc` keeps
  what only the driver and its page share.
- *Timing by the PIT in blocks.* Latching channel 0 after every read would
  have cost ~20 µs a read on the XT; reading four times per latch, and
  latching again the moment a fall is seen, keeps a fall's resolution at one
  read (~6 µs, 7 bytes of code) while the clock still does the measuring.
- *A fast channel 0 is real.* WireBall's rate bracket runs channel 0 at a
  divisor of 2048 - shorter than a sample's timeout - so a plain difference
  of two latches would be garbage inside exactly the games that want a
  stick. A difference no block can produce (past 512 counts) is now read as
  a reload and replaced by the previous block's length (SPEC 137.10).
- *Paddles are sticks with one axis.* `JOYV_READ` answers when EITHER of a
  stick's axes answers, so an Apple II paddle pair works without a type of
  its own.
- *A reader finds a stick itself.* While none of the type's axes answers,
  the sample every verb takes becomes a probe at most once a second, so a
  game started with nothing plugged in picks the stick up when it is.
- *The boot asks once.* `drv_sniff` (overlay, zero resident) asks the BIOS's
  equipment word bit 12 - POST sets it when the one-shots read idle, which a
  floating bus does not - and only then fires the port, so QEMU and every
  machine without a game port pay one `int 11h`.

**Measured** (kernsize against the branch base): kern_big +67 resident
(`.text` +33, `.bss` +24, `.cold` +10), 262 -> 195 left in the rung, no
rung crossed; kern_mega no rung crossed (image 210 -> 153 left, `.cold`
54 -> 44), footprint unchanged at 61,952; kern_small byte-identical apart
from the build number. **kern_emu crossed its rung** at first: its base had
only 16 bytes left, so the same 67 bytes took it from 95,232 to 95,744. That
went to the owner, who chose **no joystick on kern_emu** - it is the
virtual-machine kernel and no VM emulates a game port. The row, class slot,
blob and sniff are now `%ifdef DRV_JOY` (not `KERN_EMU`), `DQ_MAXREC` went
back to 13, `drivers/os88drv.inc` is byte-identical to before J1, and
kern_emu's sections came back to the base's to the byte (16 left). The 360KB
system disk went from 680 to 683 of 706 clusters.

**Tested.** `make` (fast tier), `make budget-check` (which builds
kern_small, kern_emu and kern_mega and holds all four budgets exactly),
before and after the kern_emu decision. One
QEMU boot (`make test`, kern_big, VGA): the Drivers page's Input filter lists
`Joystick` (`JOYSTICK.DRV`, ~2K, unticked - the sniff correctly left it off,
QEMU having no game port); Load brought it up (`Loaded`, 2 of 13 drivers),
and a QMP read of the driver's own memory showed the default blob (version
1, one stick two buttons, dead zone 8), every raw count `JOY_NONE` and
`joy_live` 0 - exactly what `JOYV_INFO` answers as "No joystick connected".
Not tested: a real stick (MartyPC's game port was J4's, landing in
parallel), and no row was run (the final pass does that).

**J7, the analog paddle (2026-11-24).** WireBall and Arkanoid read stick A as an
ABSOLUTE position, not as key repeats. Arkanoid maps X over the paddle's travel
in `ark_do_paddle`'s worker (one `joy_read` a frame, ~1.2 ms of a ~55 ms frame);
WireBall maps Y to a target in `wb_joy`, once per presented frame in the
foreground loop (never in the rate hook, §53.2.2), and the existing physics step
slews the paddle byte toward it by at most 4 quarter rows, so the collision
extents, fractional ball and serial link see no new state. `joy_find` is asked
once, WireBall's before `OSAPI_FSX_RUN`. Rule written down: last control moved
wins (a stick change of 2+ takes the paddle; a key clears it). Sizes: Arkanoid
5,659 -> 5,870 bytes, WireBall 37,417 -> 37,657. Arithmetic cost in WireBall:
1.2 ms of a 17-22 ms frame (46-59 fps) is 6-7%, and the read can swallow up to
one 1.7 ms rate-hook period, so motion may run a few percent slow with a stick
answering. Tested: build only on the host; the game-port instrument needs
JOYSTICK.DRV, which is not on this base, so the stick path has not run.

## 2026-11-24 - J8, the digital stick in ARCADE and 1942 (agent `gamesb`)

Both games already read their controls in one place, so the stick is one
`joy_read` at the head of that place, ORed in. ARCADE's Pac-Man (`pm_keys`,
IN0 active low) and Q*bert (`qb_keys`, IN4 active high) each gained a small
table from `JOYD_*` to the cabinet's bits; 1942's `game.inc` tests the stick
beside each arrow and gives button 1 the fire and button 2 the roll. The one
design question was Q*bert's 45-degree stick: a single direction maps as its
arrow does, but a stick diagonal is ONE bit, the keypad corner it points at -
two bits at once would be two moves the cabinet cannot make - so that is a
16-entry table rather than an OR. Pac-Man's diagonal clears both bits, as two
held arrows do, and leaves the choice to the board. No coin or start is put on
a button (a stray press would spend a credit) and Q*bert and Pac-Man have no
action button to give. Measured: `arcade.bin` +189 bytes, `1942.bin` +180
(`make arcade 1942` against a pristine build). A read costs ~1.2 ms on the XT
by SPEC 137.3 (a number from the spec, not yet measured here: no driver on this
base). Tested so far: both assemble; looked at in QEMU with keys (no stick).

## 2026-11-24 - J9, the rows (agent `rows`)

Two rows and a gate package. `tests/joygate/` is a package that includes
`apps/os88joy.inc` and, on a click, runs one probe (`joy_find`, two
`joy_read`s of stick A, one of B, `JOYV_RAW`, `joy_state`, the first read and
the state timed with `pit_now`) and leaves every answer in a fixed block in its
own image, which the row reads back; a command word the row writes first picks
`OSAPI_DRV_USE` and `JOYV_PROBE`. The `joystick` soak row drives it with
`Marty.joy()` on two boots: a `_joy` machine (the boot sniff has ticked the
driver by itself, and the row asserts that) and the standard 5150 with no game
port (refused before the attach, attaches and says no stick after - the
refusal control for the first leg). The `joycal` row runs the driver's own
`joy_prep`/`joy_calpos`/`joy_dir` under unicorn on 1,190 calibration points
against a Python statement of SPEC 137.3 that shares no code with them.

What the instrument found that the spec did not say. **An uncalibrated stick
does not reach 127**: every raw count carries a fixed latch overhead
(MartyPC: 113 counts, 95 us, at 0 ohm; the 0-to-100K span is the model's 1,316
vs 1,313), and the auto-centre assumes MIN 0 / MAX 2 x centre, so full travel
answers -108 left/up and +92/+89 right/down. The row asserts the honest
number uncalibrated (past 85, right sign, right direction bit) and +-127 once
the calibration is written the way the page will (`joy_cal`'s MIN/CEN/MAX
from the measured stops). **A read costs more than 137.3 says**: with no stick
at all a read is 1,735 units (1.45 ms) and a fast stick's 2,058 (1.7 ms), so
the door plus the cooking is about 1.7 ms on a 4.77 MHz machine, not 0.4; a
slow (100K) stick reads in 3,497 units (2.9 ms). The row's bound is one
2400-baud byte (4.17 ms), which still holds. Timing noise: a timer tick in
the door adds a ~700 us task switch to one read in a few, so the row takes
the least of three. The `joycal` and `joystick` rows were broken on purpose
(the `neg al` of the left/up half of `joy_calpos` poked to nops, the
`--break` flag of each): 456 of 1,190 points and nine assertions went red.

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
