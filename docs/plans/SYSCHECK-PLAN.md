# SYSCHECK — System Check

**Is the installed system all one release?**

System Check reads the volume a hard-disk install boots from and reports
what an in-place upgrade (SPEC.md 52.10.15) can leave broken or behind.
SPEC.md 118 is the binding contract; this file is the delivery record, and
an unchecked story here is not yet shipped behaviour. The build diary is
[docs/HOW-SYSCHECK-WAS-BUILT.md](../HOW-SYSCHECK-WAS-BUILT.md); the design
mockups are [docs/syscheck/design/](../syscheck/design/).

## Delivery stories

- [x] **Foundation:** as a maintainer I get a release manifest built from the
  system disk's own inputs (`tools/os88sysman.py`), held to every built
  system disk by a fast gate, and a `SYSCHECK.O88` that lands in `SYSTEM\`
  on the four kern_big system disks and the live media.
- [x] **Foundation:** as a user I can open `C:\SYSTEM\SYSCHECK.O88` and see
  the System Check window: the progress card, the This System card, the nine
  checks with their lamps, the findings card and the buttons, laid out for
  the adapter I have.
- [x] **Scan:** as a user the scan starts by itself, shows its progress in the
  bar and the path line, lights each check's lamp as it finishes, and leaves
  the desktop usable while it runs; Stop stops it at the next step.
- [x] **Scan:** as a user I am told about every program and driver that this
  kernel will refuse, with the format it was built for and the one the
  kernel takes.
- [x] **Scan:** as a user I am told about leftovers - `KPAD.TMP`, a program in
  two folders, a retired program or driver, a hibernation image from another
  build - and about release files that are missing, a `SYSTEM.CFG` that
  wants an absent driver, and file associations that point at nothing.
- [x] **Choose the drive:** as a user System Check asks which drive to
  check before it reads anything, with my hard disk already selected, and
  Scan asks again afterwards (SPEC.md 118.13).
- [x] **Report:** as a user Save Report writes `SYSCHECK.TXT` naming every
  finding, and a toast tells me where.
- [x] **Clean Up:** as a user Clean Up asks first, removes only the debris
  SPEC.md 118.8 lists, rebuilds a stale `ASSOC.DAT`, and scans again; what
  it cannot repair says how to (install again).
- [x] **Presentation:** as a user on Hercules or CGA the lamps are shapes
  rather than colours and the layout fits the screen; on VGA they are the
  mockup's colours.
- [ ] **Acceptance:** a hard-disk image with each kind of breakage planted is
  booted from C:, scanned, reported and cleaned up by a soak row, on every
  adapter the window claims. *Done on VGA (`syscheck`, QEMU); Hercules and
  CGA have been LOOKED AT on MartyPC (a clean system disk, every row OK) but
  no row drives a planted disk on them yet.*

## Existing facilities and boundaries

- **The scan is wake slices on the UI task** (`OSAPI_WM_ONWAKE`, SPEC.md
  74.1): it is file work, and a worker may not touch a file (SPEC.md 20.6
  rule 7). REDLINE's look was the reference; its scheduling - one blocking
  call under the gfx lock - was not.
- **The standard controls** (`apps/os88ui.inc`): buttons, the scroll bar,
  the alert and the About card. Numbers through `apps/os88dec.inc`.
- **Reading by name, listing by `OSAPI_FILE_FIND`**: a package can read a
  hidden + system file and cannot list one (SPEC.md 19.7.1), which is why
  drivers are checked against a manifest and a retired list rather than
  discovered.
- **No new kernel facility.** The one thing a slot would add - the running
  kernel's build - is answered by construction instead (SPEC.md 118.1.1),
  so the feature costs no resident byte.

## Verification discipline

Each wave: `make` (which runs the fast tier, `t_sysman` among it), a boot to
look at the window, and the suite rows named. The soak union and
`test-full` run once, after the last wave (CLAUDE.md, Testing).
