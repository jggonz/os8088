# INSTALL — the setup wizard: delivery plan

The contract is SPEC.md §117. This file is the story list the waves deliver
against, and a story is checked only when the verification named under it has
actually been run. The build diary is `docs/HOW-INSTALL-WAS-BUILT.md`.

## The brief

> Create a smart install app package that encapsulates a workflow to install
> and upgrade the operating system. It should enable the hard drive driver,
> scan for available drives, present information in a user-friendly wizard
> guiding the user to get the operating system installed on their machine's
> hard drive. It should leverage the same flow that someone would need to make
> to get the machine running by going through the install flow built into the
> control panel. This should be part of the system disk root file system.
> Call it INSTALL.

## What the map said before anything was designed

- **The engine already exists and is mature.** `drivers/hdd/inst.inc` is
  4,385 lines in `HDDTOOL.DRV` (§52.10–§52.10.17): format or keep, the
  kernel's contiguous run, `BOOTHD.SYS` off the source, the apps-disk probe,
  mounted images as sources, `ASSOC.DAT` rebuilt, the MBR as the commit. Its
  drawing all goes through `hd_iorg`, which answers "no window" and draws
  nothing when the Install window is not up — so the engine already runs
  headless, and the wizard can be a second caller of it rather than a copy.
- **A package cannot switch a driver on.** The Drivers page's tick is
  `cdl_load`, `CTRL.DRV`'s expansion of `DRV_ATTACH`; the attach half is not
  resident (kernel size pass 13). `HDD.DRV` loads itself at boot when the BIOS
  knows a disk (§51.3.2) and takes itself back down when there is nothing to
  mount — which is exactly a blank disk, the commonest install target. So a
  door is needed: `OSAPI_DRV_USE` (§117.3).
- **`HDD.DRV`'s package door reaches two XMS verbs and no sector** (§52.1.4),
  and the driver is full to its 4KB rung: `hd_mbr_end` is 4,092. A forwarding
  verb has to be paid for inside the driver, or every machine with a hard disk
  pays a kilobyte for a door it uses once.
- **Section 116 is taken** by System Check (draft PR #286, not yet on main), so
  this is §117.

## Wave 1 — the doors (engine reachable from a package)

- [x] `OSAPI_DRV_USE` at `0x04A5`: a rare cell, `.cold` body
      `osapi_drv_use_x` (`cpf_need`, `CPE_DRVUP`, drop when no panel is open;
      `DRVU_DONE` sends the class's `DSV_CPCLOSE` unless a panel is open);
      kern_small refuses. `CTRL.DRV` gains `CPE_DRVUP` (kern_big).
      *Verify:* `make`, `make KERN_SMALL=1`, `tools/kernsize.py` both kernels
      (no rung crossed), `t_api_abi` in the fast tier, abilock updated
      without a `PKG_FMT` bump (a cell appended at the end).
- [x] `HDV_TOOL` (3) in `HDD.DRV`: two instructions and a tail call into
      `hd_cp_thunk`, paid for inside `hdxms.inc`. `build/hdd.bin` still 4,096
      bytes with `hd_mbr_end` ≤ `os88_drv_end`.
      *Verify:* host build, the map, `t_install`'s size assertion.
- [x] `HDT_PKG` (10) in `HDDTOOL.DRV` with `HDI_DEVS`, `HDI_SLOTS`, `HDI_GO`,
      `HDI_APPS`, `HDI_QUIT`; `HD_ABI_VER` 7; `HDT_BUSY` busy at
      `HII_SYSDONE`; `hd_iw_shut` puts the stage back to idle; the install
      verbs refuse while the Install window is visible.
      *Verify:* host build; rows to run in the final pass named here:
      `instdeep`, `instkeep`, `inststate`, `instrest`, `hdauto`, `hddcp`.
- [x] SDK: `OSAPI_DRV_USE`, `DRVU_*`, `HDV_TOOL`, `HDI_*`, `HDIV_*`, `HDIS_*`,
      `HDS_*`, `HDG_*` in `apps/os88api.inc`; `docs/INDEX.md` regenerated.

  *Measured at the end of the wave:* kern_big `.text` +6, `.bss` +4, `.cold`
  +67 (77 bytes, no rung crossed: image 437 left, cold 278 left);
  kern_small `.text` +6 (the cell only). `HDD.DRV` 4,096 bytes, `hd_mbr_end`
  unmoved at 4,092. `HDDTOOL.DRV` 16,499 -> 17,143 bytes, `HDTOOL_KB` 17 -> 17.
  `make` green with the fast tier 52/52. Not yet driven by a package: wave 2's
  boot is the first caller.

## Wave 2 — the wizard

- [x] `apps/install/install.asm`: window 344 × 152 content, snapped; the nine
      pages of §117.1 with Back / Next / Cancel as one `OS88UI_BTNREC`; radio
      choices; keys (Enter, Escape, arrows); About card with the credit.
      *Verify:* boot `make test` in QEMU with `HDD=32`, launch from A:, look at
      every page; the same on `VIDEO=cga` and `VIDEO=herc` for fit and greying.
- [x] The decisions of §117.5: boot volume → upgrade wording and preselect;
      `KERNEL.SYS` probe per lettered slot; keep/erase greying by slot; one
      drive auto-selected; engine default slot unless os8088 is found.
- [x] Driver page: `OSAPI_DRV_CLASSK` then `OSAPI_DRV_USE`; every `DRVE_*`
      in words; `DRVU_DONE` at close and Cancel.
- [x] Install pages: Confirm → `HDI_GO`; Apps disk → `HDI_APPS` / Skip →
      `HDI_QUIT`; Done → `OSAPI_REBOOT` AL = 1; Stopped shows the caption.

  *Verified by looking, QEMU (VGA 640x480 and a private `VIDEO=cga` tree):*
  every page; erase-install of a blank 32MB disk with the apps disk found in
  B:; boot from C:; upgrade of the booted-from volume (keep forced, Erase
  greyed) and Restart back to a desktop; the apps page with the apps disk
  swapped into A: (112 paths on the disk afterwards); Escape; CGA fit and
  greying. Hercules is looked at in the final pass. Two defects found and
  fixed on the way (the diary): BX clobbered in the entry proc, and the
  instance stamp - which added `DRVU_CALL` to the cell.

## Wave 3 — shipping and the gate

- [x] `$(SYSINST)` on the root of the four kern_big system disks, the emu
      disk and the live media; not on kern_small's. *Verified:* `t_install`
      reads every root.
- [x] `tests/unit/t_install.py` (fast, row `installabi`): the roots, both
      small disks clean, `HD_ABI_VER` in source and image, the shared
      `HDG_*`/`HDS_*` numbers, `DRVU_*` in kernel and SDK, `HDD.DRV` within
      4,096 bytes. 19 checks.
- [x] `tests/install.py` (soak, row `install`, MartyPC `os8088_xt_hdd`):
      passed in the final pass (45.5 s).
- [x] §117.9 filled in from a base build of `main` at `d2d88cd3`.
- [x] The user guide's page (`tools/guide/manuals/INSTALL_O88.md`, its
      capture script) and a pointer from `HDDTOOL_DRV.md`; `README.TXT`'s
      hard-disk paragraph leads with INSTALL (and still packs to 8,088).

## Final pass (once, after the last wave)

`python3 tools/os88test.py soak -k install -k instdeep -k instkeep -k inststate
-k instrest -k hdauto -k hddcp -k instimg -k instcross -k instassoc -k hdboot`
and then `make test-full`. Plus one look on Hercules. A failing row re-runs alone after its fix.

**Run, 2026-10-20.** Soak union, `--marty-jobs 2`: 16 of 17 passed -
`install`, `instdeep`, `instkeep`, `inststate`, `instrest`, `instcross`,
`instassoc`, `hdboot`, `hddcp`, `hdauto`, `vidxiphd`, `vidxiphdhold`,
`hmadrv`, `drvup`, `drvcall`, `coldpic`. `instimg` failed staging its
fixture (`Disk full` copying a marker into `apps360.img`, which is 354 of 354
clusters on `main` at `d2d88cd3` as well; this branch changes nothing on the
apps disk) - pre-existing, not re-run. `make test-full`: 60 passed, 1 skipped
(`ctoolchain`, no C compiler on this host). `make budget-check`: kern_big
3,072 spare, kern_small 2,560, kern_emu 3,072. Hercules looked at on
MartyPC's 5150.

## Wave 4 — the Installing page shows its work (2026-10-21)

The owner, after testing an install on 86Box's MFM XT: *"I want the
Installing step to be verbose so that we can see everything that is happening
behind the scenes."*

- [x] §117.4.1 pinned first: `HDI_GO`/`HDI_APPS` take an `HDIB_*` block - the
      caption as before, a LOG PROC, a line - and the engine calls the proc
      once a step, from inside the verb, as a package callback. The Control
      Panel's runs pass none and pay one compare a site.
- [x] The engine's lines (`drivers/hdd/ipkg.inc`, call sites in `inst.inc`):
      copy buffer, source disk, format/mount or keep, every folder and file
      with its size in one column, kept and left-alone files, boot sector,
      partition table, the commit, the apps disk, `ASSOC.DAT`, the totals.
- [x] The wizard's Installing page: a framed nine-line log that scrolls with
      `OSAPI_GFX_SCROLL` (all nine repainted if refused), kept padded so a
      repaint redraws it; on the answer, a last line of the wizard's own
      (`Finished.`, `Next: the apps disk.`, or the caption), the heading
      *click Next*, Next live - the page stays until it is read.
- [x] `tests/install.py` follows the new ending and checks the log: the
      erase-install ends `Writing ASSOC.DAT` / `Copied N files, M KB` /
      `Finished.`; the upgrade ends at its commit and its apps-disk question.
- [x] Looked at on MartyPC: the XT-IDE XT (VGA) and the GLaBIOS 5150 with
      CGA, a whole erase-install, shots during the copy and at the end.
- [x] Costs in §117.9: `HDDTOOL.DRV` +672 (an 18KB claim), INSTALL +385 image
      and +428 bss, no kernel byte.
- [x] Final pass (2026-10-21): soak `install`, `instdeep`, `instkeep`,
      `inststate`, `instrest`, `instcross`, `instassoc`, `hdboot`, `hddcp`,
      `hdauto`, `vidxiphd`, `vidxiphdhold`, `hmadrv` - 13 of 13 passed;
      `make test-full` 60 passed, 1 skipped (`ctoolchain`, no C compiler).

