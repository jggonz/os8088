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

- [I] **S1** (`shell`) Choose the face at open; load `CPWIDE.DRV` and
  `CPWIDE.DAT`; fall back to compact on any refusal; drop both at close.
  Done when: VGA and VESA open wide, EGA/CGA/Hercules open compact, deleting
  `CPWIDE.DRV` from the disk opens compact; `kernsize` on all three kernels
  shows no rung crossed; SPEC 134.2 has the measured bytes.
- [I] **S2** (`shell`) The wide window: grouped page list with 16x16 icons,
  header strip with the 32x32 icon and description, status line; both
  sizes (616x408, 760x520). Every compact page and every driver page HOSTED
  in the page area and fully usable (click, release, drag, key, tick).
- [I] **S3** (`shell`) `CPE_GATE` and its `CPG_*` opcode table (134.4).
- [I] **S4** (`shell`) The widget kit (134.5) with one worked example page,
  documented for S7-S9.
- [I] **S5** (`art`) `tools/cpwdat.py`, `drivers/catalog.toml` with every
  shipped driver, `CPWIDE.DAT` on every system disk of big/emu/mega in all
  four geometries plus the live media, checked against
  `kernel/cpwfmt.inc` (lead, wave 0), the
  `t_cpwdat` fast-tier gate (134.6, 134.7).
- [I] **S6** (`vxinfo`) `VXV_INFO` in `VESA.DRV` (120.10): card facts and
  the offered-mode table by index; `VX_ABI_VER` step and abilock.

## Wave 2 - the three wide pages (parallel, after S1-S4 are integrated)

- [I] **S7** (`drv`) The Drivers page (134.8).
- [I] **S8** (`vid`) The Display page / mode changer, with Test-and-revert
  (134.9): `kernel/cpw_vid.inc` - adapters from `[vid_avail]`, the VESA
  table from `VXV_INFO` with depth filters and Auto, the monitor preview,
  Apply, Test... (15 s, Keep/Revert/Esc/close revert exactly), the startup
  box (`CFG_VE` alone), Force Color, the card's facts; `CPG_VIDARR` and
  `CPG_GLYPH` in `CTRL.DRV`; `cp_tickp`, 3 resident bytes; `vx_find`
  answers the deepest record of a class. Looked at in QEMU on VGA without
  VESA.DRV and on VESA 800x600 / 1024x768; the desktop arrangement radios
  were not (QEMU has one adapter).
- [I] **S9** (`thm`) The Theme page / theme changer (134.11).

## Wave 3 - QA, fixes, final pass (lead)

- [ ] Q1 One QA pass in emulation on the merged head: QEMU VGA, VESA 800x600
  and 1024x768, EGA/CGA/Hercules (compact still), `kern_mega`; MartyPC XT
  with VGA for the open time and heap.
- [ ] Q2 Fix round; re-run failing QA cases.
- [ ] Q3 Soak union of the named rows, then `test-full` once, `budget-check`.
- [ ] Q4 PR description with screenshots on every adapter.

## QA list

Q1 ran on 2026-11-22 at 878541a13 (QEMU VGA, VESA 800x600/1024x768, EGA,
CGA, Hercules via QEMU's forced renderer, kern_mega; MartyPC XT+VGA for
timing). Full report with steps and screenshots: the fan-out's
`qa-report.md`. Passed: hosted pages, Drivers, Theme, Display (but QA-4/5/6),
kern_mega, compact on EGA/CGA/Hercules/360KB, every refusal path (no DAT, no
DRV, corrupt DAT), the installer copies both files (by reading `inst.inc`).

| id | sev | defect | owner | state |
|---|---|---|---|---|
| QA-1 | high | Dock Left/Right on 640x480: the window is clamped, the face shows only "Close and reopen", and reopening picks wide again | fixa | open |
| QA-2 | med | the small state still takes presses and draws the wide layout | fixa | open |
| QA-3 | high | Theme and Display write the Drivers page's filter/selection bytes in the shared page block | fixa | open |
| QA-4 | med | Force Color on a VESA 8bpp desktop loads the EGA palette into DAC 0..63 | fixb | open |
| QA-5 | low | after that toggle, Apply/Test go live on the running row | fixb | open |
| QA-6 | med-low | VESA loaded at run time: the Display page says to load it (it attaches at the next boot) | fixb | open |
| QA-7 | cosm | "Use this mode at startup" touches a frame at 616x408 | fixb | open |
| QA-8 | cosm | Display check boxes are a filled square, Drivers' a tick | fixb | open |
| QA-9 | cosm | "Ram Disk" vs "RAM Disk", "VESA Display" vs "VESA High-Res Display" | lead | fixed: the catalog uses the system's names |
| QA-10 | doc | SPEC 134 said kern_mega has no VESA; it does | lead | fixed (134.4; 134.2 with QA-1) |

MartyPC, XT 4.77 MHz + VGA, 1.44MB disk, guest seconds from the menu
release to idle: first open wide 4.30 / compact 1.92; second open 1.90 /
1.08; switch to Drivers 2.15 / 1.14. (The 360KB system disk, the one an XT
normally boots, opens compact by the owner's decision.)

## Rows

(each story names its covering rows here)
