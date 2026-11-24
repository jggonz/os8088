# JOYSTICK - one game-port driver for every game (SPEC.md 137)

The owner's brief (2026-11-23):

> Add a joystick driver (Linux's joystick driver, Windows 3.1's joystick
> driver, OS/2's game port driver). There is no joystick slot in
> os88api.inc, so each game reads port 201h itself or doesn't support one.
> One shared driver, set up once in the Control Panel, would fix that. Use
> codex image cli to generate the UI for the control panel vga/vesa big
> control panel, and also for the regular control panel. Use a worktree.

The contract is SPEC.md 137 (the driver, its verbs, its settings, its page)
and SPEC.md 31.9.4 (the two Control Panel abilities the page needs, for any
driver). The build diary is `docs/HOW-JOYSTICK-WAS-BUILT.md`. The design
reference is `docs/joystick/` (Codex mockups and their prompt).

Built by fan-out (`/story-fanout`): wave 1 is four independent agents; wave
2 starts when J1's ABI files are committed. One QA run in emulation follows
the last merge, then the fix round, then the final pass once.

Status: ` ` not started, `B` built on its branch, `I` integrated into
`joystick`, `S` seen working in the QA run, `X` deferred.

## Wave 1

| | story | owner | done means |
|---|---|---|---|
| [S] | **J1 - the driver and its ABI** (SPEC 137.1-137.5). `DRVC_JOY` = 11 in `apps/os88api.inc` and `kernel/driver.inc` (`DRVC_MAX` 11 on driver kernels); the `drv_tab` row (bit 13, title, `DRVM_IMG_JOY`, the catalog entry in `drivers/catalog.toml`); `drivers/joy/joy.asm` - attach/probe/detach, the timed sample of 137.3, calibration, dead zone, directions, presses, the five verbs, the blob; `drivers/joy/joypkg.inc`, `apps/os88joy.inc`, the `os88.h` binding. `JOYSTICK.DRV` on every driver kernel's system disks | `make`, `make mega`, kern_emu, kernsize on all three with no rung crossed, `abilock` updated; the ABI files committed FIRST (wave 2 starts from that commit); a boot that ticks the row on QEMU and shows `No joystick connected` semantics through `JOYV_INFO` |
| [I] | **J2 - live and wide driver pages** (SPEC 31.9.4). CX/BX/SI on page calls in both faces; `DSV_CPTICK` delivered about 9 Hz to the selected driver page; `OS88_DRV_CPWIDE` and the wide face hosting such a page over the whole page area | builds on all three driver kernels and kern_small unchanged; every existing driver page (HDD, network, RAM disk, CD-ROM, HTTP, EMS, sound) looks and acts as before on both faces; resident cost measured, no rung crossed |
| [I] | **J3 - the joystick icons** (SPEC 135.6). The 16x16 and 32x32 icons from `docs/joystick/` into `kernel/cpwart/` (the 16x16 sheet gains a third row, cell 32; the 32x32 sheet cell 12), `CWI16_JOY` = 32 / `CWI32_JOY` = 12 in `kernel/cpwfmt.inc`, `tools/cpwdat.py`, `t_cpwdat.py`; `DRVC_JOY` mapped to them where the wide face picks a driver page's icon; `CPWIDE.DAT` still under 16,384 | `make`, `t_cpwdat` green, the size of `CPWIDE.DAT` recorded |
| [S] | **J4 - the instrument** (SPEC 137.7). MartyPC patch: a game port in the os8088 machine configurations and a debug-server verb setting both sticks and four buttons; `tools/os88marty.py` gains `joy(...)`; docs/MARTYPC-DEBUG.md and docs/TESTING.md say so | `make marty` builds; a scripted session moves a stick and `inb(0x201)` timing shows it |

## Wave 2 (from J1's ABI commit)

| | story | owner | done means |
|---|---|---|---|
| [S] | **J5 - the Joystick page** (SPEC 137.6), compact and wide, to the mockups: live view, type, three-step calibration, dead zone, invert, swap, reset, status | a look on QEMU VGA (wide), EGA and CGA (compact), and on MartyPC with a stick moved |
| [I] | **J6 - the emulators**: C64 control ports, Apple II paddles and buttons | builds, keys still work, a stick drives them on MartyPC |
| [S] | **J7 - analog paddles**: WireBall and Arkanoid | builds, keys still work, the paddle follows the stick |
| [S] | **J8 - digital sticks**: ARCADE and 1942 | builds, keys still work |
| [I] | **J9 - the rows**: a gate package and `tests/suite.py` rows driving J4's instrument through J1's verbs | the rows registered; run once in the final pass |

## Deferred, and why

| | item | why |
|---|---|---|
| X | DOS `int 15h AH=84h` (the AT BIOS joystick service) answered from the driver | DOS programs read 201h directly and that already works on the iron; a hook is a later story |
| X | WIN16 `joyGetPos` / `joyGetDevCaps` (MMSYSTEM) | the Windows 3.1 API the brief names as a model - a WIN16 story of its own |
| X | Joystick as keys or as the mouse, for games that never adopt it | needs a kernel key-feed slot (resident bytes) - an owner decision |

## Measured

Filled as waves land: kernsize per kernel, `JOYSTICK.DRV` size, `CPWIDE.DAT`
size, a sample's cost on MartyPC's XT.

## QA

The one QA run's defect list goes here. Found before it, by J9's rows
(`rows-report.md`), and carried into the fix round:

| # | seen | expected | where | status |
|---|---|---|---|---|
| Q1 | an UNCALIBRATED stick reaches about -108..+92 of +-127: MartyPC's 0-ohm count carries a fixed ~95 us latch overhead, and auto-centre assumes a minimum of 0 | full travel near +-127 before the user calibrates, or the page telling them to calibrate | driver, SPEC 137.3 | open |
| Q2 | a verb costs 1.45-2.9 ms (door, cooking and two `pit_now`s on top of the sample), against "~1.2 ms a read" in SPEC 137.3/137.12 and the J7/J8 frame-budget notes | the figures in SPEC and the adopters' notes are the measured ones | SPEC 137.3, 137.12, 109.7, 44.13, 101.7, ARCADE-SPEC 15 | fixed (FIXDOC_SHA) |
| Q3 | WireBall's ~1.2 ms (really 1.5-2.9 ms) read may swallow a rate-hook period, so its motion clock runs a few percent slow while a stick answers (gamesa-report.md) | motion steps unaffected, or the read halved to every second frame | apps/wireball, SPEC 109.7 | fixed (FIXDOC_SHA): the loss was real (the hook counts IRQs); the read is gated to the OS tick |
| Q4 | the wide face's header strip under the Joystick page says `Settings this driver publishes.` (every driver page's generic line) | the page's own line, `Calibrate and test the game port joystick.` (the mockup) | wide face, page-shots/page.png | open |
| Q5 | after a full calibration, full right reads +119 and not +127: the X maximum was captured while Y sat at its stop (1,475), the same X stop with Y centred counts 1,440 - crosstalk between the axes in the sample or in MartyPC's model (page-report.md) | +127 at each stop whatever the other axis does | driver sample or instrument | open |
| Q6 | JOYSTICK.DRV's claim while ticked is 10,760 bytes (8,256 on disk) against the brief's "9 KB with the page" | accepted by the lead: it is heap only while ticked, no resident byte; an overlay for the calibration view (~3 KB) is the remedy if the owner wants it | page | accepted |

**The QA run (2026-11-24, lead, merged head `0f47cadff`)**, on MartyPC with the
game port moved by `m.joy()`, the 360KB system disk:

| claim | machine | seen |
|---|---|---|
| the boot probe ticks the driver; the Joystick row is in the compact list | `os8088_5150_cga_gla_joy` | yes |
| the live view follows the stick (+39/+39), button 1's lamp lights, the read time shows (2.0 ms) | same | yes |
| a three-step calibration (upper left, lower right, centre) ends at 0/0, +127 right, -127/-127 upper left | same | yes - with the lower-right corner taken diagonally, right reads +127 (X max 1,434); see Q5 |
| dead zone +/-, Invert Y; the settings and the calibration survive a cold reset (SYSTEM.CFG) | same | yes: 10 %, Invert Y, Reset live, "Stick A found" after the reset |
| WireBall's paddle follows Y (Invert Y honoured) | same, `games360.img` | yes |
| Arkanoid's Vaus follows X; button 1 serves | same | yes |
| 1942: the plane follows the stick, button 1 fires | same, `1942-360.img` | yes |
| ARCADE Pac-Man: the stick reverses Pac-Man's default leftward run | `os8088_xt_vga_joy`, `arcade360.img` (fetched ROMs) | yes, after ~2 guest minutes of the board's self-test; Q*bert not looked at |
| C64 / Apple II | `_joy` machines | emus2's look only (C64 status-bar indicators); the Apple II paddle value was not read back in the guest |

No new defect was found by the run beyond Q1-Q5; the wide face, VESA 800x600
and Hercules are looked at with the PR's screenshots, after the fix round.
