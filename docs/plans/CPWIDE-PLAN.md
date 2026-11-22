# The wide Control Panel - plan (SPEC.md 134)

The contract is SPEC.md 134 (and 120.10 for the VESA verb). This file is the
story list: what is planned, who owns it, and what has been SEEN working.
The build diary is [HOW-CPWIDE-WAS-BUILT.md](../HOW-CPWIDE-WAS-BUILT.md).

Marks: `[ ]` planned, `[I]` integrated into `feature/cpwide` (built, `make`
green, not yet seen in the QA pass), `[S]` seen working in the QA pass.

## The owner's brief (2026-11-21)

> use codex image gen through codex cli to redesign the control panel for
> vga and vesa video modes where there is more room to show information and
> provide a more powerful control panel layout. Use a modular approach to
> determine what control panel UI to load without bloat. Color, assets,
> icons and other UI elements to enable better usability are allowed. The
> drivers view is especially in need of a redesign since we plan to grow
> the library of drivers. I also want to have a more powerful video mode
> changer and theme changer since I am going to build out more VESA video
> modes and special video drivers in the future.

## Decisions taken

- D1. **Two faces, one panel.** The compact face (SPEC.md 31) is unchanged
  and stays the only face on CGA, Hercules, EGA, VESA below 640x480 and
  `kern_small`. The wide face is a second module, `CPWIDE.DRV`, loaded BESIDE
  `CTRL.DRV` on a colour display of at least 640x480 (134.1, 134.2).
- D2. **No logic is written twice.** `CTRL.DRV` keeps every state change and
  every compact page; `CPWIDE.DRV` reaches them through one gate entry
  (134.4) and hosts the pages it does not redraw (134.3).
- D3. **Zero rung crossed** on `kern_big`, `kern_emu`, `kern_mega`
  (owner rule); `kern_small` byte-identical.
- D4. **Refusal is the compact face**, silently (134.2).
- D5. **Assets and the driver catalog are a data file**, `CPWIDE.DAT`, built
  from committed PNGs and `drivers/catalog.toml` (134.6, 134.7). A driver
  with no catalog entry still shows.
- D6. Artwork: mockups and icon sheets from Codex CLI image generation, kept
  with their exact prompts (`docs/cpwide/`, `kernel/cpwart/`).

## Wave 0 - contract and art (lead)

- [x] W0.1 SPEC.md 134 and the 120.10 stub, this plan, the diary.
- [x] W0.2 Three mockups (`docs/cpwide/`) and two icon sheets
  (`kernel/cpwart/`), with prompts.

## Wave 1 - foundation (parallel)

- [ ] **S1** (`shell`) Choose the face at open; load `CPWIDE.DRV` and
  `CPWIDE.DAT`; fall back to compact on any refusal; drop both at close.
  Done when: VGA and VESA open wide, EGA/CGA/Hercules open compact, deleting
  `CPWIDE.DRV` from the disk opens compact; `kernsize` on all three kernels
  shows no rung crossed; SPEC 134.2 has the measured bytes.
- [ ] **S2** (`shell`) The wide window: grouped page list with 16x16 icons,
  header strip with the 32x32 icon and description, status line; both
  sizes (616x408, 760x520). Every compact page and every driver page HOSTED
  in the page area and fully usable (click, release, drag, key, tick).
- [ ] **S3** (`shell`) `CPE_GATE` and its `CPG_*` opcode table (134.4).
- [ ] **S4** (`shell`) The widget kit (134.5) with one worked example page,
  documented for S7-S9.
- [ ] **S5** (`art`) `tools/cpwdat.py`, `drivers/catalog.toml` with every
  shipped driver, `CPWIDE.DAT` on every system disk of big/emu/mega in all
  four geometries plus the live media, checked against
  `kernel/cpwfmt.inc` (lead, wave 0), the
  `t_cpwdat` fast-tier gate (134.6, 134.7).
- [ ] **S6** (`vxinfo`) `VXV_INFO` in `VESA.DRV` (120.10): card facts and
  the offered-mode table by index; `VX_ABI_VER` step and abilock.

## Wave 2 - the three wide pages (parallel, after S1-S4 are integrated)

- [ ] **S7** (`drv`) The Drivers page (134.8).
- [ ] **S8** (`vid`) The Display page / mode changer, with Test-and-revert
  (134.9).
- [ ] **S9** (`thm`) The Theme page / theme changer (134.11).

## Wave 3 - QA, fixes, final pass (lead)

- [ ] Q1 One QA pass in emulation on the merged head: QEMU VGA, VESA 800x600
  and 1024x768, EGA/CGA/Hercules (compact still), `kern_mega`; MartyPC XT
  with VGA for the open time and heap.
- [ ] Q2 Fix round; re-run failing QA cases.
- [ ] Q3 Soak union of the named rows, then `test-full` once, `budget-check`.
- [ ] Q4 PR description with screenshots on every adapter.

## QA list

(filled by Q1)

## Rows

(each story names its covering rows here)
