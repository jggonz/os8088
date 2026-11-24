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
