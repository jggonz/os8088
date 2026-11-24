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
| [ ] | **J1 - the driver and its ABI** (SPEC 137.1-137.5). `DRVC_JOY` = 11 in `apps/os88api.inc` and `kernel/driver.inc` (`DRVC_MAX` 11 on driver kernels); the `drv_tab` row (bit 13, title, `DRVM_IMG_JOY`, the catalog entry in `drivers/catalog.toml`); `drivers/joy/joy.asm` - attach/probe/detach, the timed sample of 137.3, calibration, dead zone, directions, presses, the five verbs, the blob; `drivers/joy/joypkg.inc`, `apps/os88joy.inc`, the `os88.h` binding. `JOYSTICK.DRV` on every driver kernel's system disks | `make`, `make mega`, kern_emu, kernsize on all three with no rung crossed, `abilock` updated; the ABI files committed FIRST (wave 2 starts from that commit); a boot that ticks the row on QEMU and shows `No joystick connected` semantics through `JOYV_INFO` |
| [ ] | **J2 - live and wide driver pages** (SPEC 31.9.4). CX/BX/SI on page calls in both faces; `DSV_CPTICK` delivered about 9 Hz to the selected driver page; `OS88_DRV_CPWIDE` and the wide face hosting such a page over the whole page area | builds on all three driver kernels and kern_small unchanged; every existing driver page (HDD, network, RAM disk, CD-ROM, HTTP, EMS, sound) looks and acts as before on both faces; resident cost measured, no rung crossed |
| [ ] | **J3 - the joystick icons** (SPEC 135.6). The 16x16 and 32x32 icons from `docs/joystick/` into `kernel/cpwart/` (the 16x16 sheet gains a third row, cell 32; the 32x32 sheet cell 12), `CWI16_JOY` = 32 / `CWI32_JOY` = 12 in `kernel/cpwfmt.inc`, `tools/cpwdat.py`, `t_cpwdat.py`; `DRVC_JOY` mapped to them where the wide face picks a driver page's icon; `CPWIDE.DAT` still under 16,384 | `make`, `t_cpwdat` green, the size of `CPWIDE.DAT` recorded |
| [ ] | **J4 - the instrument** (SPEC 137.7). MartyPC patch: a game port in the os8088 machine configurations and a debug-server verb setting both sticks and four buttons; `tools/os88marty.py` gains `joy(...)`; docs/MARTYPC-DEBUG.md and docs/TESTING.md say so | `make marty` builds; a scripted session moves a stick and `inb(0x201)` timing shows it |

## Wave 2 (from J1's ABI commit)

| | story | owner | done means |
|---|---|---|---|
| [ ] | **J5 - the Joystick page** (SPEC 137.6), compact and wide, to the mockups: live view, type, three-step calibration, dead zone, invert, swap, reset, status | a look on QEMU VGA (wide), EGA and CGA (compact), and on MartyPC with a stick moved |
| [ ] | **J6 - the emulators**: C64 control ports, Apple II paddles and buttons | builds, keys still work, a stick drives them on MartyPC |
| [ ] | **J7 - analog paddles**: WireBall and Arkanoid | builds, keys still work, the paddle follows the stick |
| [ ] | **J8 - digital sticks**: ARCADE and 1942 | builds, keys still work |
| [ ] | **J9 - the rows**: a gate package and `tests/suite.py` rows driving J4's instrument through J1's verbs | the rows registered; run once in the final pass |

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

The one QA run's defect list goes here.
