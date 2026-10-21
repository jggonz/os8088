# How INSTALL was built

A diary kept while the work happened, for later reuse. Observations and
intentions are kept apart; anything called *tested* names what ran it. The
contract is SPEC.md §117 and the stories are `docs/plans/INSTALL-PLAN.md`.

## The ask (2026-10-20)

A *smart install* package called INSTALL, on the system disk's root, that
switches the hard-disk driver on, finds the drives, and walks a person through
putting os8088 on one — or upgrading what is there — using the same flow the
Control Panel's installer already runs.

## Day 1 — reading before designing

**Observed.** The Control Panel's installer is not a thin dialog over a small
routine: `drivers/hdd/inst.inc` is 4,385 lines and §52.10 is about two
thousand lines of SPEC, most of it closed field defects — a booted-from volume
offered as a target, a kernel written behind another build's boot sector, an
apps copy that threw away a finished system install, images that could not be
used as sources. Any second implementation of that walk would have to
rediscover every one of them.

**Observed.** Every drawing call in the installer goes through `hd_iorg`, which
answers *no window* and draws nothing when the Install window is not on
screen. The engine was already headless; it only lacked a caller.

**Observed.** Two doors were missing. A package has no way to switch a driver
on — the Drivers page's tick runs `cdl_load` inside `CTRL.DRV`, and the attach
half of a load is deliberately not resident. And `HDD.DRV`'s package door
reaches two XMS verbs and nothing else, in a driver whose last byte of data is
at 4,092 of a 4,096-byte rung.

**Decided.** INSTALL owns pages and words and nothing else. It reaches the
engine through one new kernel cell (`OSAPI_DRV_USE`, the tick as a door) and
one new `HDD.DRV` verb (`HDV_TOOL`, a forwarder into `HDDTOOL.DRV`), and the
forwarder's bytes are found inside the driver rather than paid for with a
fifth kilobyte on every hard-disk machine.

**Decided.** The door loads and does not record: no `SYSTEM.CFG` answer is
written for a user who never answered the Drivers page.
