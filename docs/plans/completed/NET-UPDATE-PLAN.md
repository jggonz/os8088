# NET-UPDATE — install and upgrade os8088 over the network

**BUILT, waves 1-7** - SPEC.md 26.10.7 (a link's parameter) and 117.10 (INSTALL over
the network) are the contract; this file is the story list and the design
record behind them. It follows `docs/plans/HTTP-MOUNT-PLAN.md`, whose drive is
the transport, and `docs/plans/INSTALL-PLAN.md`, whose wizard and engine do
the install.

The ask, in the owner's words:

- *Have install.o88 check the remote version against the installed os version
  on startup, if and only if an ethernet driver is mounted.*
- *Have the first screen select between network install and disk install
  (instead of the intro it shows today). If the network version is newer,
  show the newer version.*
- *We may add some text, downloaded with the version, that shows what is new,
  if we have room on cga for two radio lines, one or two help lines, and a few
  lines of what is new.*
- *Potential: add a startup link as an option at the end. On startup, check
  for a new version, and if one exists show the window.*
- *Actual install would be something like: mount a hardcoded web address for
  a combined os/apps disk, and then run the normal install.*

and the decisions taken while costing it:

- **A release number, bumped by the release skill**, not the build number: the
  build number is a commit count, and `main` squash-merges while `elendilon`
  does not, so a newer `main` release can carry a smaller count. Only real
  releases matter, so the release's own date is the number.
- **Put the user's own HTTP drive back** after the check, if it is cheap.
- **No once-a-day throttle**: the check at startup is one the user asked for.
- **A link carries a PARAMETER** rather than INSTALL declaring a file type to
  tell a startup launch apart: the association table is a shared, bounded
  resource (16 programs / 32 extensions) and an extension that names no real
  document would spend a slot of it. Raising that table is a separate
  decision the owner is taking elsewhere.
- **The install log stays**; INSTALL's text is trimmed instead.

## 1. What already exists, and what it decides

| need | what does it today |
|---|---|
| a drive over HTTP | `HTTPFS.DRV` (SPEC.md 122), and its package door `HFV_*` (122.4) |
| bringing a driver up from a package | `OSAPI_DRV_USE`'s `DRVU_UP` (117.3) - the Drivers page's tick |
| is an Ethernet card up | `OSAPI_DRV_CLASSK` with `DRVC_NET` |
| the install | `HDDTOOL.DRV`'s engine through `HDI_GO` (117.4). Its source is **the volume the bank left the machine on** if it carries `KERNEL.SYS` (`hd_isrc_ck`), so standing on the HTTP drive's root is the whole of choosing it |
| apps on the same volume as the system | the SYSTEM phase walks the whole source tree (52.10.13), so a combined tree is copied in one phase |
| `ASSOC.DAT` on the installed volume | built from the destination after the install (52.10.14) - on a run whose apps phase was declined too |
| a quiet refusal from an entry proc | a toast the entry raised survives the abort and `Load failed` is not said (20.12.4) |
| a program started at boot | `\AUTOSTRT\*.LNK` (26.10.5), written by `apps/os88link.inc` (26.10.6) |

So the network install is the ordinary install with a different source
volume, and its apps phase is skipped because there is nothing left to copy.

## 2. Stories

### Wave 1 - a link carries a parameter (kernel + SDK)

- [x] **A link whose target is a program may carry a parameter**: text after
      the path's NUL, inside the same 45-byte field. `LNK_V` stays 1: a kernel
      that predates it ends the path at the NUL and never reads the rest.
- [x] **`OSAPI_ARG_TEXT`** (cell `0x04D5`, a rare cell): the program reads it
      from its entry proc, read-and-clear, `OSAPI_ARG_FILE`'s shape. It points
      into the link's record on task 0's stack, which is alive for the whole
      launch, so no buffer is spent. kern_small refuses (it has no links).
- [x] `apps/os88link.inc`'s `OS88_LINK` takes an optional fifth argument, the
      parameter; `tools/os88lnk.py` writes and reads one (`--arg`).
- [x] Gate: `tests/netinst.py` leg C - INSTALL started at boot by a link
      carrying `CHECK` stays quiet when nothing is newer, and the same link
      without the parameter opens the window. INSTALL is the probe: what it
      does with the answer is the only thing a user can see of it.

### Wave 2 - the release number and the published tree (host)

- [x] **`apps/install/OS88VER.TXT`** is the release's version file, tracked:
      line 1 the release as `YYYYMMDD`, then up to four lines of what is new.
      The release skill writes it (the date it already puts in
      `v1.0.YYYYMMDD`) and commits it before it builds.
- [x] INSTALL is assembled with that first line as its own release
      (`IN_REL`), so the wizard on a disk knows the release it came from - and,
      being copied with the system (117.6), the release INSTALLED on a hard
      disk.
- [x] **`make nettree`** writes `build/nettree/`: the 1.44MB system disk's
      files and the 1.44MB apps disk's files as ONE tree, minus `ASSOC.DAT`
      (the engine builds the volume's own, 52.10.14), plus `OS88VER.TXT`, plus
      the `OS88.IDX` `tools/os88httpdrv.py` writes. The release skill publishes
      it to the `os8088-net` store the website serves as `/net/` (117.10.2).

### Wave 3 - INSTALL checks the network

- [x] On start, **only if a `DRVC_NET` driver is loaded**: bring `HTTPFS.DRV`
      up, remember the drive's current address, mount
      `http://os8088.com/net/`, read `OS88VER.TXT`, and compare its release
      with INSTALL's own. Afterwards the user's own address is mounted again,
      or the drive unmounted if there was none. `HTTPDRV.CFG` is never
      written.
- [x] The window is up first and says `Looking for a newer os8088 on the
      network...` while it asks, so a slow server is a sentence and not a
      frozen screen.

### Wave 4 - the first page chooses

- [x] The Welcome page becomes the **source** page: two radio rows, *From the
      network* and *From floppy disks*, a help line, and up to four lines of
      what is new. The network row is greyed with the reason when there is no
      card, the server did not answer, or its release is not newer; when it
      is newer it names the release and is the default.
- [x] Fits CGA's 344 x 136 content: 2 radio rows (24px), 1 help line, 4
      what's-new lines at the body pitch, above the buttons.

### Wave 5 - the install from the network

- [x] Choosing the network mounts the address again, stands on its root, and
      runs the ordinary pages; `HDI_GO` copies from it. The apps phase is
      skipped by itself (`HDI_QUIT`): the tree carried the apps.
- [x] The Confirm page says *copy os8088 from the network* instead of *copy
      both floppies*.

### Wave 6 - check at startup

- [x] The Done page offers **Check for updates at startup** as the second of
      two radio rows (the group the wizard already has; a check box would be
      another control in the image). Picked,
      Restart writes `\AUTOSTRT\OSCHECK.LNK` naming `INSTALL.O88` with the
      parameter `CHECK`, on the installed volume; unticked, it deletes it.
- [x] Started with `CHECK`, INSTALL asks the network before it opens a
      window. Nothing newer, or no network: one toast, and no window. Newer:
      the window, on the source page.

### Wave 7 - text trims, the gate, the final pass

- [x] The upgrade page folds into the source page; Confirm, Apps and Done lose
      the lines the pages around them already say. The log stays.
- [x] Gate `netinst` (QEMU by name: the card): a fixture tree served by
      `tools/os88httpdrv.py` with a newer `OS88VER.TXT`, the source page
      reads it, an install from it reaches Done, the volume holds the apps.
- [x] The rows this work can reach, once, after the last wave: `httpmount`,
      `httpinstall`, `install`, `autostart`, `netinst`, the link gate; then
      `test-full` once.

### Wave 8 - what the first field run found (SPEC.md 117.10.5, 117.10.6)

Booted from A:, network picked, installing to C:, with the apps disk in B:.

- [x] **The apps came off the floppy.** INSTALL stood on the HTTP drive and
      let the bank find it; the engine then looked for an apps disk in a
      drive, found B:'s and copied the apps phase from it. `HDI_GO` now
      NAMES its source in `CH` bits 7..1, and a named source skips the probe.
      `netinst` carries an apps disk with a marker in B: and fails if the
      marker arrives (and fails with the fix taken out).
- [x] **`OS88VER.TXT` landed in the root.** It is published as
      `SYSTEM\OS88VER.TXT`, so the copy is the installed machine's record of
      its release.
- [x] **The news was cut short** - a stale `OS88.IDX` after an edit on the
      server, not INSTALL. The release skill and `os88nettree.py` now say so,
      and the skill recommends four one-line points or three with one over
      two lines.
- [x] **The news shows for any release** while the network row is the pick,
      and goes with the floppy row; the floppy help line replaces it.
- [x] **The tick is kept**: a target that already has INSTALL's link opens
      the Done page on *Check for updates at startup* (`install` asserts it).
- [x] **Cancel on the first page read `HDDTOOL.DRV`** (31 sectors, measured
      with a `DISKCNT` kernel) to send `HDI_QUIT`. It is sent only to a run
      that waits now. Close after a finished install moved no sector in the
      same measurement; the field's slow close did not reproduce.
- [x] **The HTTP tick read `CTRL.DRV`** even with the drive already up; the
      class is asked first.
- [x] **Arrow keys left two picks on every radio page** - `os88ui_rad`
      draws the new dot and never removes the old one. The keyboard path
      draws the two dots a click draws.

### Wave 9 - the check asks the sockets (SPEC.md 117.10.3)

- [x] **One GET of `SYSTEM/OS88VER.TXT`** on a socket of INSTALL's own, in
      place of bringing the HTTP drive up for the check: no `CTRL.DRV` or
      `HTTPFS.DRV` read off the floppy, no index fetched. The drive is
      mounted only when the network is picked. `IN_URL` is cut into host,
      port and path by the assembler. +335 image bytes, no cluster crossed.
      `netinst` fails if the check asks for anything but that one file.

## 3. Cost, estimated before the build

| where | bytes | note |
|---|---:|---|
| kernel, kern_big | ~50-60 resident | the parameter's arm in `lnk_open`, a word of `.bss`, the cell and its body |
| kernel, kern_small | 6 | the cell, refusing |
| `INSTALL.O88` | ~700-1,000 image, less the trims | the check, the source page, the network install, the startup option |
| `HTTPFS.DRV`, `ETHER.DRV`, `HDDTOOL.DRV` | 0 | used through their published verbs |

**Measured** (SPEC.md 117.10.8): kern_big **+53** resident (+6 `.text`, +2
`.bss`, +45 `.cold`), kern_small +6, `INSTALL.O88` 8,032 -> **8,891** image
and 6,179 -> **7,031** on the disk (+852, one more 512-byte cluster), with
1,060 -> 1,519 bytes of bss.

## 4. Decided against

- **An association for the startup marker.** It worked, and it spent a slot
  of a table an everything-install already overflows (16 programs / 32
  extensions against 21 / 38) on an extension that names no document.
- **Comparing build numbers.** A commit count is per branch.
- **A once-a-day throttle.** INSTALL would still load to read the date, so it
  saves only the HTTP drive's load, and the check is one the user switched on.
- **Two addresses** (a small version tree beside the big one). One tree
  serves both: the check fetches its index once and the install reuses the
  mount.
