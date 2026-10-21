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
- **§116 is taken** by System Check (draft PR #286), so this is §117.

## Wave 1 — the doors (engine reachable from a package)

- [ ] `OSAPI_DRV_USE` at `0x04A5`: a rare cell, `.cold` body
      `osapi_drv_use_x` (`cpf_need`, `CPE_DRVUP`, drop when no panel is open;
      `DRVU_DONE` sends the class's `DSV_CPCLOSE` unless a panel is open);
      kern_small refuses. `CTRL.DRV` gains `CPE_DRVUP` (kern_big).
      *Verify:* `make`, `make KERN_SMALL=1`, `tools/kernsize.py` both kernels
      (no rung crossed), `t_api_abi` in the fast tier, abilock updated
      without a `PKG_FMT` bump (a cell appended at the end).
- [ ] `HDV_TOOL` (3) in `HDD.DRV`: two instructions and a tail call into
      `hd_cp_thunk`, paid for inside `hdxms.inc`. `build/hdd.bin` still 4,096
      bytes with `hd_mbr_end` ≤ `os88_drv_end`.
      *Verify:* host build, the map, `t_install`'s size assertion.
- [ ] `HDT_PKG` (10) in `HDDTOOL.DRV` with `HDI_DEVS`, `HDI_SLOTS`, `HDI_GO`,
      `HDI_APPS`, `HDI_QUIT`; `HD_ABI_VER` 7; `HDT_BUSY` busy at
      `HII_SYSDONE`; `hd_iw_shut` puts the stage back to idle; the install
      verbs refuse while the Install window is visible.
      *Verify:* host build; rows to run in the final pass named here:
      `instdeep`, `instkeep`, `inststate`, `instrest`, `hdauto`, `hddcp`.
- [ ] SDK: `OSAPI_DRV_USE`, `DRVU_*`, `HDV_TOOL`, `HDI_*`, `HDIV_*`, `HDIS_*`,
      `HDS_*`, `HDG_*` in `apps/os88api.inc`; `docs/INDEX.md` regenerated.

## Wave 2 — the wizard

- [ ] `apps/install/install.asm`: window 344 × 152 content, snapped; the nine
      pages of §117.1 with Back / Next / Cancel as one `OS88UI_BTNREC`; radio
      choices; keys (Enter, Escape, arrows); About card with the credit.
      *Verify:* boot `make test` in QEMU with `HDD=32`, launch from A:, look at
      every page; the same on `VIDEO=cga` and `VIDEO=herc` for fit and greying.
- [ ] The decisions of §117.5: boot volume → upgrade wording and preselect;
      `KERNEL.SYS` probe per lettered slot; keep/erase greying by slot; one
      drive auto-selected; engine default slot unless os8088 is found.
- [ ] Driver page: `OSAPI_DRV_CLASSK` then `OSAPI_DRV_USE`; every `DRVE_*`
      in words; `DRVU_DONE` at close and Cancel.
- [ ] Install pages: Confirm → `HDI_GO`; Apps disk → `HDI_APPS` / Skip →
      `HDI_QUIT`; Done → `OSAPI_REBOOT` AL = 1; Stopped shows the caption.

## Wave 3 — shipping and the gate

- [ ] `$(SYSINST)` on the root of the four kern_big system disks, the emu
      disk and the live media; not on kern_small's (`t_smallreq` agrees).
- [ ] `tests/unit/t_install.py` (fast): §117.8's host assertions.
- [ ] `tests/install.py` (soak, MartyPC `os8088_xt_hdd`): erase-install by the
      wizard, the host reads the volume back, boot C:, upgrade by the wizard
      keeping a planted file. Registered in `tests/suite.py`.
- [ ] §117.9 filled in with measured costs; README's install paragraph points
      at INSTALL; the diary's results section.

## Final pass (once, after the last wave)

`python3 tools/os88test.py soak -k install -k instdeep -k instkeep -k inststate
-k instrest -k hdauto -k hddcp -k instimg -k instcross` and then
`make test-full`. A failing row re-runs alone after its fix.
